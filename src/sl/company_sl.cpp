/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file company_sl.cpp Code handling saving and loading of company data */

#include "../stdafx.h"
#include "../company_func.h"
#include "../company_manager_face.h"
#include "../fios.h"
#include "../load_check.h"
#include "../tunnelbridge_map.h"
#include "../tunnelbridge.h"
#include "../station_base.h"
#include "../settings_func.h"
#include "../strings_func.h"
#include "../network/network.h"
#include "../network/network_func.h"
#include "../network/network_server.h"
#include "../3rdparty/monocypher/monocypher.h"

#include "saveload.h"
#include "saveload_buffer.h"

#include "table/strings.h"

#include "../safeguards.h"

/**
 * Converts an old company manager's face format to the new company manager's face format
 *
 * Meaning of the bits in the old face (some bits are used in several times):
 * - 4 and 5: chin
 * - 6 to 9: eyebrows
 * - 10 to 13: nose
 * - 13 to 15: lips (also moustache for males)
 * - 16 to 19: hair
 * - 20 to 22: eye colour
 * - 20 to 27: tie, ear rings etc.
 * - 28 to 30: glasses
 * - 19, 26 and 27: race (bit 27 set and bit 19 equal to bit 26 = black, otherwise white)
 * - 31: gender (0 = male, 1 = female)
 *
 * @param face the face in the old format
 * @return the face in the new format
 */
CompanyManagerFace ConvertFromOldCompanyManagerFace(uint32_t face)
{
	CompanyManagerFace cmf = 0;
	GenderEthnicity ge = GE_WM;

	if (HasBit(face, 31)) SetBit(ge, GENDER_FEMALE);
	if (HasBit(face, 27) && (HasBit(face, 26) == HasBit(face, 19))) SetBit(ge, ETHNICITY_BLACK);

	SetCompanyManagerFaceBits(cmf, CMFV_GEN_ETHN,    ge, ge);
	SetCompanyManagerFaceBits(cmf, CMFV_HAS_GLASSES, ge, GB(face, 28, 3) <= 1);
	SetCompanyManagerFaceBits(cmf, CMFV_EYE_COLOUR,  ge, HasBit(ge, ETHNICITY_BLACK) ? 0 : ClampU(GB(face, 20, 3), 5, 7) - 5);
	SetCompanyManagerFaceBits(cmf, CMFV_CHIN,        ge, ScaleCompanyManagerFaceValue(CMFV_CHIN,     ge, GB(face,  4, 2)));
	SetCompanyManagerFaceBits(cmf, CMFV_EYEBROWS,    ge, ScaleCompanyManagerFaceValue(CMFV_EYEBROWS, ge, GB(face,  6, 4)));
	SetCompanyManagerFaceBits(cmf, CMFV_HAIR,        ge, ScaleCompanyManagerFaceValue(CMFV_HAIR,     ge, GB(face, 16, 4)));
	SetCompanyManagerFaceBits(cmf, CMFV_JACKET,      ge, ScaleCompanyManagerFaceValue(CMFV_JACKET,   ge, GB(face, 20, 2)));
	SetCompanyManagerFaceBits(cmf, CMFV_COLLAR,      ge, ScaleCompanyManagerFaceValue(CMFV_COLLAR,   ge, GB(face, 22, 2)));
	SetCompanyManagerFaceBits(cmf, CMFV_GLASSES,     ge, GB(face, 28, 1));

	uint lips = GB(face, 10, 4);
	if (!HasBit(ge, GENDER_FEMALE) && lips < 4) {
		SetCompanyManagerFaceBits(cmf, CMFV_HAS_MOUSTACHE, ge, true);
		SetCompanyManagerFaceBits(cmf, CMFV_MOUSTACHE,     ge, std::max(lips, 1U) - 1);
	} else {
		if (!HasBit(ge, GENDER_FEMALE)) {
			lips = lips * 15 / 16;
			lips -= 3;
			if (HasBit(ge, ETHNICITY_BLACK) && lips > 8) lips = 0;
		} else {
			lips = ScaleCompanyManagerFaceValue(CMFV_LIPS, ge, lips);
		}
		SetCompanyManagerFaceBits(cmf, CMFV_LIPS, ge, lips);

		uint nose = GB(face, 13, 3);
		if (ge == GE_WF) {
			nose = (nose * 3 >> 3) * 3 >> 2; // There is 'hole' in the nose sprites for females
		} else {
			nose = ScaleCompanyManagerFaceValue(CMFV_NOSE, ge, nose);
		}
		SetCompanyManagerFaceBits(cmf, CMFV_NOSE, ge, nose);
	}

	uint tie_earring = GB(face, 24, 4);
	if (!HasBit(ge, GENDER_FEMALE) || tie_earring < 3) { // Not all females have an earring
		if (HasBit(ge, GENDER_FEMALE)) SetCompanyManagerFaceBits(cmf, CMFV_HAS_TIE_EARRING, ge, true);
		SetCompanyManagerFaceBits(cmf, CMFV_TIE_EARRING, ge, HasBit(ge, GENDER_FEMALE) ? tie_earring : ScaleCompanyManagerFaceValue(CMFV_TIE_EARRING, ge, tie_earring / 2));
	}

	return cmf;
}

/** Rebuilding of company statistics after loading a savegame. */
void AfterLoadCompanyStats()
{
	/* Reset infrastructure statistics to zero. */
	for (Company *c : Company::Iterate()) c->infrastructure = {};

	/* Collect airport count. */
	for (const Station *st : Station::Iterate()) {
		if ((st->facilities & FACIL_AIRPORT) && Company::IsValidID(st->owner)) {
			Company::Get(st->owner)->infrastructure.airport++;
		}
	}

	Company *c;
	for (TileIndex tile = 0; tile < MapSize(); tile++) {
		switch (GetTileType(tile)) {
			case MP_RAILWAY:
				c = Company::GetIfValid(GetTileOwner(tile));
				if (c != nullptr) {
					uint pieces = 1;
					if (IsPlainRail(tile)) {
						TrackBits bits = GetTrackBits(tile);
						if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
							c->infrastructure.rail[GetSecondaryRailType(tile)]++;
						} else {
							pieces = CountBits(bits);
							if (TracksOverlap(bits)) pieces *= pieces;
						}
					}
					c->infrastructure.rail[GetRailType(tile)] += pieces;

					if (HasSignals(tile)) c->infrastructure.signal += CountBits(GetPresentSignals(tile));
				}
				break;

			case MP_ROAD: {
				if (IsLevelCrossing(tile)) {
					c = Company::GetIfValid(GetTileOwner(tile));
					if (c != nullptr) c->infrastructure.rail[GetRailType(tile)] += LEVELCROSSING_TRACKBIT_FACTOR;
				}

				/* Iterate all present road types as each can have a different owner. */
				for (RoadTramType rtt : _roadtramtypes) {
					RoadType rt = GetRoadType(tile, rtt);
					if (rt == INVALID_ROADTYPE) continue;
					c = Company::GetIfValid(IsRoadDepot(tile) ? GetTileOwner(tile) : GetRoadOwner(tile, rtt));
					/* A level crossings and depots have two road bits. */
					if (c != nullptr) c->infrastructure.road[rt] += IsNormalRoad(tile) ? CountBits(GetRoadBits(tile, rtt)) : 2;
				}
				break;
			}

			case MP_STATION:
				c = Company::GetIfValid(GetTileOwner(tile));
				if (c != nullptr && GetStationType(tile) != STATION_AIRPORT && !IsBuoy(tile)) c->infrastructure.station++;

				switch (GetStationType(tile)) {
					case STATION_RAIL:
					case STATION_WAYPOINT:
						if (c != nullptr && !IsStationTileBlocked(tile)) c->infrastructure.rail[GetRailType(tile)]++;
						break;

					case STATION_BUS:
					case STATION_TRUCK:
					case STATION_ROADWAYPOINT: {
						/* Iterate all present road types as each can have a different owner. */
						for (RoadTramType rtt : _roadtramtypes) {
							RoadType rt = GetRoadType(tile, rtt);
							if (rt == INVALID_ROADTYPE) continue;
							c = Company::GetIfValid(GetRoadOwner(tile, rtt));
							if (c != nullptr) c->infrastructure.road[rt] += 2; // A road stop has two road bits.
						}
						break;
					}

					case STATION_DOCK:
					case STATION_BUOY:
						if (GetWaterClass(tile) == WATER_CLASS_CANAL) {
							if (c != nullptr) c->infrastructure.water++;
						}
						break;

					default:
						break;
				}
				break;

			case MP_WATER:
				if (IsShipDepot(tile) || IsLock(tile)) {
					c = Company::GetIfValid(GetTileOwner(tile));
					if (c != nullptr) {
						if (IsShipDepot(tile)) c->infrastructure.water += LOCK_DEPOT_TILE_FACTOR;
						if (IsLock(tile) && GetLockPart(tile) == LOCK_PART_MIDDLE) {
							/* The middle tile specifies the owner of the lock. */
							c->infrastructure.water += 3 * LOCK_DEPOT_TILE_FACTOR; // the middle tile specifies the owner of the
							break; // do not count the middle tile as canal
						}
					}
				}
				[[fallthrough]];

			case MP_OBJECT:
				if (GetWaterClass(tile) == WATER_CLASS_CANAL) {
					c = Company::GetIfValid(GetTileOwner(tile));
					if (c != nullptr) c->infrastructure.water++;
				}
				break;

			case MP_TUNNELBRIDGE: {
				/* Only count the tunnel/bridge if we're on the western end tile. */
				if (GetTunnelBridgeDirection(tile) < DIAGDIR_SW) {
					TileIndex other_end = GetOtherTunnelBridgeEnd(tile);

					/* Count each tunnel/bridge TUNNELBRIDGE_TRACKBIT_FACTOR times to simulate
					 * the higher structural maintenance needs, and don't forget the end tiles. */
					const uint middle_len = GetTunnelBridgeLength(tile, other_end) * TUNNELBRIDGE_TRACKBIT_FACTOR;

					switch (GetTunnelBridgeTransportType(tile)) {
						case TRANSPORT_RAIL:
							AddRailTunnelBridgeInfrastructure(tile, other_end);
							break;

						case TRANSPORT_ROAD: {
							AddRoadTunnelBridgeInfrastructure(tile, other_end);
							break;
						}

						case TRANSPORT_WATER:
							c = Company::GetIfValid(GetTileOwner(tile));
							if (c != nullptr) c->infrastructure.water += middle_len + (2 * TUNNELBRIDGE_TRACKBIT_FACTOR);
							break;

						default:
							break;
					}
				}
				break;
			}

			default:
				break;
		}
	}
}

static const NamedSaveLoad _company_settings_desc[] = {
	/* Engine renewal settings */
	NSL("", SLE_CONDNULL(512, SLV_16, SLV_19)),
	NSL("engine_renew_list",                            SLE_CONDREF(CompanyProperties, engine_renew_list,                          REF_ENGINE_RENEWS,   SLV_19, SL_MAX_VERSION)),
	NSL("settings.engine_renew",                        SLE_CONDVAR(CompanyProperties, settings.engine_renew,                      SLE_BOOL,            SLV_16, SL_MAX_VERSION)),
	NSL("settings.engine_renew_months",                 SLE_CONDVAR(CompanyProperties, settings.engine_renew_months,               SLE_INT16,           SLV_16, SL_MAX_VERSION)),
	NSL("settings.engine_renew_money",                  SLE_CONDVAR(CompanyProperties, settings.engine_renew_money,                SLE_UINT32,          SLV_16, SL_MAX_VERSION)),
	NSL("settings.renew_keep_length",                   SLE_CONDVAR(CompanyProperties, settings.renew_keep_length,                 SLE_BOOL,             SLV_2, SL_MAX_VERSION)),

	/* Default vehicle settings */
	NSL("settings.vehicle.servint_ispercent",           SLE_CONDVAR(CompanyProperties, settings.vehicle.servint_ispercent,         SLE_BOOL,           SLV_120, SL_MAX_VERSION)),
	NSL("settings.vehicle.servint_trains",              SLE_CONDVAR(CompanyProperties, settings.vehicle.servint_trains,            SLE_UINT16,         SLV_120, SL_MAX_VERSION)),
	NSL("settings.vehicle.servint_roadveh",             SLE_CONDVAR(CompanyProperties, settings.vehicle.servint_roadveh,           SLE_UINT16,         SLV_120, SL_MAX_VERSION)),
	NSL("settings.vehicle.servint_aircraft",            SLE_CONDVAR(CompanyProperties, settings.vehicle.servint_aircraft,          SLE_UINT16,         SLV_120, SL_MAX_VERSION)),
	NSL("settings.vehicle.servint_ships",               SLE_CONDVAR(CompanyProperties, settings.vehicle.servint_ships,             SLE_UINT16,         SLV_120, SL_MAX_VERSION)),
	NSL("settings.vehicle.auto_timetable_by_default", SLE_CONDVAR_X(CompanyProperties, settings.vehicle.auto_timetable_by_default, SLE_BOOL,    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AUTO_TIMETABLE, 2, 2))),

	NSL("", SLE_CONDNULL(63, SLV_2, SLV_144)), // old reserved space
};

static const NamedSaveLoad _company_economy_desc[] = {
	/* these were changed to 64-bit in savegame format 2 */
	NSL("income",              SLE_CONDVAR(CompanyEconomyEntry, income,              SLE_FILE_I32 | SLE_VAR_I64, SL_MIN_VERSION, SLV_2)),
	NSL("income",              SLE_CONDVAR(CompanyEconomyEntry, income,              SLE_INT64,                  SLV_2, SL_MAX_VERSION)),
	NSL("expenses",            SLE_CONDVAR(CompanyEconomyEntry, expenses,            SLE_FILE_I32 | SLE_VAR_I64, SL_MIN_VERSION, SLV_2)),
	NSL("expenses",            SLE_CONDVAR(CompanyEconomyEntry, expenses,            SLE_INT64,                  SLV_2, SL_MAX_VERSION)),
	NSL("company_value",       SLE_CONDVAR(CompanyEconomyEntry, company_value,       SLE_FILE_I32 | SLE_VAR_I64, SL_MIN_VERSION, SLV_2)),
	NSL("company_value",       SLE_CONDVAR(CompanyEconomyEntry, company_value,       SLE_INT64,                  SLV_2, SL_MAX_VERSION)),

	NSL("",                    SLE_CONDVAR(CompanyEconomyEntry, delivered_cargo[NUM_CARGO - 1], SLE_INT32,       SL_MIN_VERSION, SLV_170)),
	NSL("delivered_cargo",     SLE_CONDARR(CompanyEconomyEntry, delivered_cargo,     SLE_UINT32, 32,           SLV_170, SLV_EXTEND_CARGOTYPES)),
	NSL("delivered_cargo",     SLE_CONDARR(CompanyEconomyEntry, delivered_cargo,     SLE_UINT32, NUM_CARGO,    SLV_EXTEND_CARGOTYPES, SL_MAX_VERSION)),
	NSL("performance_history",     SLE_VAR(CompanyEconomyEntry, performance_history, SLE_INT32)),
};

/* We do need to read this single value, as the bigger it gets, the more data is stored */
struct CompanyOldAI {
	uint8_t num_build_rec;
};

static const SaveLoad _company_ai_desc[] = {
	SLE_CONDNULL(2,  SL_MIN_VERSION, SLV_107),
	SLE_CONDNULL(2,  SL_MIN_VERSION, SLV_13),
	SLE_CONDNULL(4, SLV_13, SLV_107),
	SLE_CONDNULL(8,  SL_MIN_VERSION, SLV_107),
	 SLE_CONDVAR(CompanyOldAI, num_build_rec, SLE_UINT8, SL_MIN_VERSION, SLV_107),
	SLE_CONDNULL(3,  SL_MIN_VERSION, SLV_107),

	SLE_CONDNULL(2,  SL_MIN_VERSION,  SLV_6),
	SLE_CONDNULL(4,  SLV_6, SLV_107),
	SLE_CONDNULL(2,  SL_MIN_VERSION,  SLV_6),
	SLE_CONDNULL(4,  SLV_6, SLV_107),
	SLE_CONDNULL(2,  SL_MIN_VERSION, SLV_107),

	SLE_CONDNULL(2,  SL_MIN_VERSION,  SLV_6),
	SLE_CONDNULL(4,  SLV_6, SLV_107),
	SLE_CONDNULL(2,  SL_MIN_VERSION,  SLV_6),
	SLE_CONDNULL(4,  SLV_6, SLV_107),
	SLE_CONDNULL(2,  SL_MIN_VERSION, SLV_107),

	SLE_CONDNULL(2,  SL_MIN_VERSION, SLV_69),
	SLE_CONDNULL(4,  SLV_69, SLV_107),

	SLE_CONDNULL(18, SL_MIN_VERSION, SLV_107),
	SLE_CONDNULL(20, SL_MIN_VERSION, SLV_107),
	SLE_CONDNULL(32, SL_MIN_VERSION, SLV_107),

	SLE_CONDNULL(64, SLV_2, SLV_107),
};

static const SaveLoad _company_ai_build_rec_desc[] = {
	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_6),
	SLE_CONDNULL(4, SLV_6, SLV_107),
	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_6),
	SLE_CONDNULL(4, SLV_6, SLV_107),
	SLE_CONDNULL(8, SL_MIN_VERSION, SLV_107),
};

static const NamedSaveLoad _company_livery_desc[] = {
	NSL("in_use",  SLE_CONDVAR(Livery, in_use,  SLE_UINT8, SLV_34, SL_MAX_VERSION)),
	NSL("colour1", SLE_CONDVAR(Livery, colour1, SLE_UINT8, SLV_34, SL_MAX_VERSION)),
	NSL("colour2", SLE_CONDVAR(Livery, colour2, SLE_UINT8, SLV_34, SL_MAX_VERSION)),
};

static void LoadLiveries(CompanyProperties *c, uint num_liveries, const SaveLoadTable &slt)
{
	bool update_in_use = IsSavegameVersionBefore(SLV_GROUP_LIVERIES);

	for (uint i = 0; i < num_liveries; i++) {
		SlObjectLoadFiltered(&c->livery[i], slt);
		if (update_in_use && i != LS_DEFAULT) {
			if (c->livery[i].in_use == 0) {
				c->livery[i].colour1 = c->livery[LS_DEFAULT].colour1;
				c->livery[i].colour2 = c->livery[LS_DEFAULT].colour2;
			} else {
				c->livery[i].in_use = 3;
			}
		}
	}

	if (num_liveries < LS_END) {
		/* We want to insert some liveries somewhere in between. This means some have to be moved. */
		memmove(&c->livery[LS_FREIGHT_WAGON], &c->livery[LS_PASSENGER_WAGON_MONORAIL], (LS_END - LS_FREIGHT_WAGON) * sizeof(c->livery[0]));
		c->livery[LS_PASSENGER_WAGON_MONORAIL] = c->livery[LS_MONORAIL];
		c->livery[LS_PASSENGER_WAGON_MAGLEV]   = c->livery[LS_MAGLEV];
	}

	if (num_liveries == LS_END - 4) {
		/* Copy bus/truck liveries over to trams */
		c->livery[LS_PASSENGER_TRAM] = c->livery[LS_BUS];
		c->livery[LS_FREIGHT_TRAM]   = c->livery[LS_TRUCK];
	}
}

struct CompanySettingsStructHandler final : public TypedSaveLoadStructHandler<CompanySettingsStructHandler, CompanyProperties> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _company_settings_desc;
	}

	void Save(CompanyProperties *cprops) const override
	{
		SlObjectSaveFiltered(cprops, this->GetLoadDescription());
	}

	void Load(CompanyProperties *cprops) const override
	{
		SlObjectLoadFiltered(cprops, this->GetLoadDescription());
	}

	void LoadCheck(CompanyProperties *cprops) const override { this->Load(cprops); }

	void FixPointers(CompanyProperties *cprops) const override
	{
		SlObjectPtrOrNullFiltered(cprops, this->GetLoadDescription());
	}
};

struct CompanyExtraSettingsStructHandler final : public TypedSaveLoadStructHandler<CompanyExtraSettingsStructHandler, CompanyProperties> {
	std::vector<NamedSaveLoad> settings_desc;

	CompanyExtraSettingsStructHandler()
	{
		extern std::vector<NamedSaveLoad> FillPlyrExtraSettingsDesc();
		this->settings_desc = FillPlyrExtraSettingsDesc();
	}

	NamedSaveLoadTable GetDescription() const override
	{
		return this->settings_desc;
	}

	void Save(CompanyProperties *cprops) const override
	{
		SlObjectSaveFiltered(&(cprops->settings), this->GetLoadDescription());
	}

	void Load(CompanyProperties *cprops) const override
	{
		SlObjectLoadFiltered(&(cprops->settings), this->GetLoadDescription());
	}

	void LoadCheck(CompanyProperties *cprops) const override { this->Load(cprops); }
};

struct CompanyCurEconomyStructHandler final : public TypedSaveLoadStructHandler<CompanyCurEconomyStructHandler, CompanyProperties> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _company_economy_desc;
	}

	void Save(CompanyProperties *cprops) const override
	{
		SlObjectSaveFiltered(&cprops->cur_economy, this->GetLoadDescription());
	}

	void Load(CompanyProperties *cprops) const override
	{
		SlObjectLoadFiltered(&cprops->cur_economy, this->GetLoadDescription());
	}

	void LoadCheck(CompanyProperties *cprops) const override { this->Load(cprops); }
};

struct CompanyOldEconomyStructHandler final : public TypedSaveLoadStructHandler<CompanyOldEconomyStructHandler, CompanyProperties> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _company_economy_desc;
	}

	void Save(CompanyProperties *cprops) const override
	{
		SlSetStructListLength(cprops->num_valid_stat_ent);
		for (int i = 0; i < cprops->num_valid_stat_ent; i++) {
			SlObjectSaveFiltered(&cprops->old_economy[i], this->GetLoadDescription());
		}
	}

	void Load(CompanyProperties *cprops) const override
	{
		cprops->num_valid_stat_ent = static_cast<uint8_t>(SlGetStructListLength(lengthof(cprops->old_economy)));

		for (int i = 0; i < cprops->num_valid_stat_ent; i++) {
			SlObjectLoadFiltered(&cprops->old_economy[i], this->GetLoadDescription());
		}
	}

	void LoadCheck(CompanyProperties *cprops) const override { this->Load(cprops); }
};

struct CompanyLiveriesStructHandler final : public TypedSaveLoadStructHandler<CompanyLiveriesStructHandler, CompanyProperties> {
	NamedSaveLoadTable GetDescription() const override
	{
		return _company_livery_desc;
	}

	void Save(CompanyProperties *cprops) const override
	{
		SlSetStructListLength(LS_END);
		for (int i = 0; i < LS_END; i++) {
			SlObjectSaveFiltered(&cprops->livery[i], this->GetLoadDescription());
		}
	}

	void Load(CompanyProperties *cprops) const override
	{
		uint num_liveries = static_cast<uint>(SlGetStructListLength(LS_END));
		LoadLiveries(cprops, num_liveries, this->GetLoadDescription());
	}

	void LoadCheck(CompanyProperties *cprops) const override { this->Load(cprops); }
};

struct CompanyAllowListStructHandler final : public TypedSaveLoadStructHandler<CompanyAllowListStructHandler, CompanyProperties> {
public:
	struct KeyWrapper {
		std::string key;
	};

	NamedSaveLoadTable GetDescription() const override
	{
		static const NamedSaveLoad description[] = {
			NSLT("key", SLE_SSTR(KeyWrapper, key, SLE_STR)),
		};
		return description;
	}

	void Save(CompanyProperties *cprops) const override
	{
		SlSetStructListLength(cprops->allow_list.size());
		for (std::string &str : cprops->allow_list) {
			SlObjectSaveFiltered(&str, this->GetLoadDescription());
		}
	}

	void Load(CompanyProperties *cprops) const override
	{
		size_t num_keys = SlGetStructListLength(UINT32_MAX);
		cprops->allow_list.clear();
		cprops->allow_list.resize(num_keys);
		for (std::string &str : cprops->allow_list) {
			SlObjectLoadFiltered(&str, this->GetLoadDescription());
		}
	}

	void LoadCheck(CompanyProperties *cprops) const override { this->Load(cprops); }
};

/* Save/load of companies */
static const NamedSaveLoad _company_desc[] = {
	NSL("name_2",                           SLE_VAR(CompanyProperties, name_2,                SLE_UINT32)),
	NSL("name_1",                           SLE_VAR(CompanyProperties, name_1,                SLE_STRINGID)),
	NSL("name",                        SLE_CONDSSTR(CompanyProperties, name,                  SLE_STR | SLF_ALLOW_CONTROL, SLV_84, SL_MAX_VERSION)),

	NSL("president_name_1",                 SLE_VAR(CompanyProperties, president_name_1,      SLE_STRINGID)),
	NSL("president_name_2",                 SLE_VAR(CompanyProperties, president_name_2,      SLE_UINT32)),
	NSL("president_name",              SLE_CONDSSTR(CompanyProperties, president_name,        SLE_STR | SLF_ALLOW_CONTROL, SLV_84, SL_MAX_VERSION)),

	NSL("face",                             SLE_VAR(CompanyProperties, face,                  SLE_UINT32)),

	/* money was changed to a 64 bit field in savegame version 1. */
	NSL("money",                        SLE_CONDVAR(CompanyProperties, money,                 SLE_VAR_I64 | SLE_FILE_I32,  SL_MIN_VERSION, SLV_1)),
	NSL("money",                        SLE_CONDVAR(CompanyProperties, money,                 SLE_INT64,                   SLV_1, SL_MAX_VERSION)),

	NSL("current_loan",                 SLE_CONDVAR(CompanyProperties, current_loan,          SLE_VAR_I64 | SLE_FILE_I32,  SL_MIN_VERSION, SLV_65)),
	NSL("current_loan",                 SLE_CONDVAR(CompanyProperties, current_loan,          SLE_INT64,                   SLV_65, SL_MAX_VERSION)),

	NSL("colour",                           SLE_VAR(CompanyProperties, colour,                SLE_UINT8)),
	NSL("money_fraction",                   SLE_VAR(CompanyProperties, money_fraction,        SLE_UINT8)),
	NSL("",                            SLE_CONDNULL(1,  SL_MIN_VERSION,  SLV_58)), ///< avail_railtypes
	NSL("block_preview",                    SLE_VAR(CompanyProperties, block_preview,         SLE_UINT8)),

	NSL("",                            SLE_CONDNULL(2,  SL_MIN_VERSION,  SLV_94)), ///< cargo_types
	NSL("",                            SLE_CONDNULL(4, SLV_94, SLV_170)), ///< cargo_types
	NSL("location_of_HQ",               SLE_CONDVAR(CompanyProperties, location_of_HQ,        SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION,  SLV_6)),
	NSL("location_of_HQ",               SLE_CONDVAR(CompanyProperties, location_of_HQ,        SLE_UINT32,                  SLV_6, SL_MAX_VERSION)),
	NSL("last_build_coordinate",        SLE_CONDVAR(CompanyProperties, last_build_coordinate, SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION,  SLV_6)),
	NSL("last_build_coordinate",        SLE_CONDVAR(CompanyProperties, last_build_coordinate, SLE_UINT32,                  SLV_6, SL_MAX_VERSION)),
	NSL("inaugurated_year",             SLE_CONDVAR(CompanyProperties, inaugurated_year,      SLE_FILE_U8  | SLE_VAR_I32,  SL_MIN_VERSION, SLV_31)),
	NSL("inaugurated_year",             SLE_CONDVAR(CompanyProperties, inaugurated_year,      SLE_INT32,                   SLV_31, SL_MAX_VERSION)),
	NSL("display_inaugurated_period", SLE_CONDVAR_X(CompanyProperties, display_inaugurated_period, SLE_INT32,   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 6))),
	NSL("age_years",                  SLE_CONDVAR_X(CompanyProperties, age_years,             SLE_INT32,        SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 6))),

	NSL("share_owners",                     SLE_ARR(CompanyProperties, share_owners,          SLE_UINT8, 4)),

	NSL("",                                 SLE_VAR(CompanyProperties, num_valid_stat_ent,    SLE_UINT8)), // Not required in table format

	NSL("months_of_bankruptcy",             SLE_VAR(CompanyProperties, months_of_bankruptcy,  SLE_UINT8)),
	NSL("bankrupt_last_asked",        SLE_CONDVAR_X(CompanyProperties, bankrupt_last_asked,   SLE_UINT8,        SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_BANKRUPTCY_EXTRA))),
	NSL("bankrupt_flags",             SLE_CONDVAR_X(CompanyProperties, bankrupt_flags,        SLE_UINT8,        SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_BANKRUPTCY_EXTRA, 2))),
	NSL("bankrupt_asked",               SLE_CONDVAR(CompanyProperties, bankrupt_asked,        SLE_FILE_U8  | SLE_VAR_U16,  SL_MIN_VERSION, SLV_104)),
	NSL("bankrupt_asked",               SLE_CONDVAR(CompanyProperties, bankrupt_asked,        SLE_UINT16,                  SLV_104, SL_MAX_VERSION)),
	NSL("bankrupt_timeout",                 SLE_VAR(CompanyProperties, bankrupt_timeout,      SLE_INT16)),
	NSL("bankrupt_value",               SLE_CONDVAR(CompanyProperties, bankrupt_value,        SLE_VAR_I64 | SLE_FILE_I32,  SL_MIN_VERSION, SLV_65)),
	NSL("bankrupt_value",               SLE_CONDVAR(CompanyProperties, bankrupt_value,        SLE_INT64,                   SLV_65, SL_MAX_VERSION)),

	/* yearly expenses was changed to 64-bit in savegame version 2. */
	NSL("yearly_expenses",              SLE_CONDARR(CompanyProperties, yearly_expenses,       SLE_FILE_I32 | SLE_VAR_I64, 3 * 13, SL_MIN_VERSION, SLV_2)),
	NSL("yearly_expenses",            SLE_CONDARR_X(CompanyProperties, yearly_expenses,       SLE_INT64, 3 * 13,           SLV_2, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INFRA_SHARING, 0, 0))),
	NSL("yearly_expenses",            SLE_CONDARR_X(CompanyProperties, yearly_expenses,       SLE_INT64, 3 * 15,           SLV_2, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INFRA_SHARING))),

	NSL("is_ai",                        SLE_CONDVAR(CompanyProperties, is_ai,                 SLE_BOOL,                    SLV_2, SL_MAX_VERSION)),
	NSL("",                            SLE_CONDNULL(1, SLV_107, SLV_112)), ///< is_noai
	NSL("",                            SLE_CONDNULL(1, SLV_4, SLV_100)),

	NSL("terraform_limit",              SLE_CONDVAR(CompanyProperties, terraform_limit,       SLE_UINT32,                SLV_156, SL_MAX_VERSION)),
	NSL("clear_limit",                  SLE_CONDVAR(CompanyProperties, clear_limit,           SLE_UINT32,                SLV_156, SL_MAX_VERSION)),
	NSL("tree_limit",                   SLE_CONDVAR(CompanyProperties, tree_limit,            SLE_UINT32,                SLV_175, SL_MAX_VERSION)),
	NSL("purchase_land_limit",        SLE_CONDVAR_X(CompanyProperties, purchase_land_limit, SLE_UINT32,         SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_BUY_LAND_RATE_LIMIT))),
	NSL("build_object_limit",         SLE_CONDVAR_X(CompanyProperties, build_object_limit,  SLE_UINT32,         SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_BUILD_OBJECT_RATE_LIMIT))),

	NSLT_STRUCT<CompanySettingsStructHandler>("settings"),
	NSLT_STRUCT<CompanyExtraSettingsStructHandler>("extra_settings"),
	NSLT_STRUCT<CompanyCurEconomyStructHandler>("cur_economy"),
	NSLT_STRUCTLIST<CompanyOldEconomyStructHandler>("old_economy"),
	NSLT_STRUCTLIST<CompanyLiveriesStructHandler>("liveries"),
	NSLT_STRUCTLIST<CompanyAllowListStructHandler>("allow_list"),
};

struct PLYRNonTableHelper {
	std::vector<SaveLoad> liveries_desc;
	std::vector<SaveLoad> economy_desc;
	std::vector<SaveLoad> settings_desc;

	void Setup()
	{
		this->liveries_desc = SlFilterNamedSaveLoadTable(_company_livery_desc);
		this->economy_desc = SlFilterNamedSaveLoadTable(_company_economy_desc);
		this->settings_desc = SlFilterNamedSaveLoadTable(_company_settings_desc);
	}

	void Load_PLYR_common(Company *c, CompanyProperties *cprops);
};

void PLYRNonTableHelper::Load_PLYR_common(Company *c, CompanyProperties *cprops)
{
	SlObjectLoadFiltered(cprops, settings_desc);

	/* Keep backwards compatible for savegames, so load the old AI block */
	if (IsSavegameVersionBefore(SLV_107) && cprops->is_ai) {
		CompanyOldAI old_ai;
		char nothing;

		SlObject(&old_ai, _company_ai_desc);
		for (uint i = 0; i != old_ai.num_build_rec; i++) {
			SlObject(&nothing, _company_ai_build_rec_desc);
		}
	}

	/* Write economy */
	SlObjectLoadFiltered(&cprops->cur_economy, this->economy_desc);

	/* Write old economy entries. */
	if (cprops->num_valid_stat_ent > lengthof(cprops->old_economy)) SlErrorCorrupt("Too many old economy entries");
	for (uint i = 0; i < cprops->num_valid_stat_ent; i++) {
		SlObjectLoadFiltered(&cprops->old_economy[i], this->economy_desc);
	}

	/* Write each livery entry. */
	uint num_liveries = IsSavegameVersionBefore(SLV_63) ? LS_END - 4 : (IsSavegameVersionBefore(SLV_85) ? LS_END - 2: LS_END);

	if (c != nullptr) {
		LoadLiveries(cprops, num_liveries, this->liveries_desc);
	} else {
		/* Skip liveries */
		Livery dummy_livery;
		for (uint i = 0; i < num_liveries; i++) {
			SlObjectLoadFiltered(&dummy_livery, this->liveries_desc);
		}
	}
}

static void Save_PLYR()
{
	SaveLoadTableData slt = SlTableHeader(_company_desc);

	for (Company *c : Company::Iterate()) {
		CompanyProperties *cprops = c;
		SlSetArrayIndex(c->index);
		SlObjectSaveFiltered(cprops, slt);
	}
}

static void Load_PLYR()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_company_desc);

	PLYRNonTableHelper helper;
	if (!SlIsTableChunk()) helper.Setup();

	int index;
	while ((index = SlIterateArray()) != -1) {
		Company *c = new (index) Company();
		CompanyProperties *cprops = c;
		SetDefaultCompanySettings(c->index);
		SlObjectLoadFiltered(cprops, slt);
		if (!SlIsTableChunk()) {
			helper.Load_PLYR_common(c, cprops);
		}
		_company_colours[index] = (Colours)c->colour;

		/* settings moved from game settings to company settings */
		if (SlXvIsFeaturePresent(XSLFI_AUTO_TIMETABLE, 1, 2)) {
			c->settings.auto_timetable_separation_rate = _settings_game.order.old_timetable_separation_rate;
		}
		if (SlXvIsFeaturePresent(XSLFI_AUTO_TIMETABLE, 1, 3)) {
			c->settings.vehicle.auto_separation_by_default = _settings_game.order.old_timetable_separation;
		}
	}
}

static void Check_PLYR()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_company_desc);

	PLYRNonTableHelper helper;
	if (!SlIsTableChunk()) helper.Setup();

	int index;
	while ((index = SlIterateArray()) != -1) {
		std::unique_ptr<CompanyProperties> cprops = std::make_unique<CompanyProperties>();
		SlObjectLoadFiltered(cprops.get(), slt);
		if (!SlIsTableChunk()) {
			helper.Load_PLYR_common(nullptr, cprops.get());
		}

		/* We do not load old custom names */
		if (IsSavegameVersionBefore(SLV_84)) {
			if (GetStringTab(cprops->name_1) == TEXT_TAB_OLD_CUSTOM) {
				cprops->name_1 = STR_GAME_SAVELOAD_NOT_AVAILABLE;
			}

			if (GetStringTab(cprops->president_name_1) == TEXT_TAB_OLD_CUSTOM) {
				cprops->president_name_1 = STR_GAME_SAVELOAD_NOT_AVAILABLE;
			}
		}

		if (cprops->name.empty() && !IsInsideMM(cprops->name_1, SPECSTR_COMPANY_NAME_START, SPECSTR_COMPANY_NAME_LAST + 1) &&
				cprops->name_1 != STR_GAME_SAVELOAD_NOT_AVAILABLE && cprops->name_1 != STR_SV_UNNAMED &&
				cprops->name_1 != SPECSTR_ANDCO_NAME && cprops->name_1 != SPECSTR_PRESIDENT_NAME &&
				cprops->name_1 != SPECSTR_SILLY_NAME) {
			cprops->name_1 = STR_GAME_SAVELOAD_NOT_AVAILABLE;
		}

		if (_load_check_data.companies.count(index) == 0) {
			_load_check_data.companies[index] = std::move(cprops);
		}
	}
}

static void Ptrs_PLYR()
{
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(_company_settings_desc);

	for (Company *c : Company::Iterate()) {
		CompanyProperties *cprops = c;
		SlObjectPtrOrNullFiltered(cprops, slt);
	}
}

extern void LoadSettingsPlyx(bool skip);

static void Load_PLYX()
{
	LoadSettingsPlyx(false);
}

static void Check_PLYX()
{
	LoadSettingsPlyx(true);
}

static void Load_PLYP()
{
	size_t size = SlGetFieldLength();
	CompanyMask invalid_mask = 0;
	if (SlXvIsFeaturePresent(XSLFI_COMPANY_PW, 2)) {
		if (size <= 2) return;
		invalid_mask = SlReadUint16();
		size -= 2;
	}
	if (size <= 16 + 24 + 16 || (_networking && !_network_server)) {
		SlSkipBytes(size);
		return;
	}
	if (!_network_server) {
		extern CompanyMask _saved_PLYP_invalid_mask;
		extern std::vector<uint8_t> _saved_PLYP_data;

		_saved_PLYP_invalid_mask = invalid_mask;
		_saved_PLYP_data.resize(size);
		ReadBuffer::GetCurrent()->CopyBytes(_saved_PLYP_data.data(), _saved_PLYP_data.size());
		return;
	}

	std::array<uint8_t, 16> token;
	ReadBuffer::GetCurrent()->CopyBytes(token.data(), 16);
	if (token != _network_company_password_storage_token) {
		Debug(sl, 2, "Skipping encrypted company passwords");
		SlSkipBytes(size - 16);
		return;
	}

	std::array<uint8_t, 16> mac;
	std::array<uint8_t, 24> nonce;
	ReadBuffer::GetCurrent()->CopyBytes(nonce.data(), 24);
	ReadBuffer::GetCurrent()->CopyBytes(mac.data(), 16);

	std::vector<uint8_t> buffer(size - 16 - 24 - 16);
	ReadBuffer::GetCurrent()->CopyBytes(buffer.data(), buffer.size());

	if (crypto_aead_unlock(buffer.data(), mac.data(), _network_company_password_storage_key.data(), nonce.data(), nullptr, 0, buffer.data(), buffer.size()) == 0) {
		SlLoadFromBuffer(buffer.data(), buffer.size(), [invalid_mask]() {
			_network_company_server_id.resize(SlReadUint32());
			ReadBuffer::GetCurrent()->CopyBytes((uint8_t *)_network_company_server_id.data(), _network_company_server_id.size());

			while (true) {
				uint16_t cid = SlReadUint16();
				if (cid >= MAX_COMPANIES) break;
				std::string password;
				password.resize(SlReadUint32());
				ReadBuffer::GetCurrent()->CopyBytes((uint8_t *)password.data(), password.size());
				if (!HasBit(invalid_mask, cid)) {
					NetworkServerSetCompanyPassword((CompanyID)cid, password, true);
				}
			}

			ReadBuffer::GetCurrent()->SkipBytes(SlReadByte()); // Skip padding
		});
		Debug(sl, 2, "Decrypted company passwords");
	} else {
		Debug(sl, 2, "Failed to decrypt company passwords");
	}
}

static void Save_PLYP()
{
	if ((_networking && !_network_server) || IsNetworkServerSave()) {
		SlSetLength(0);
		return;
	}
	if (!_network_server) {
		extern CompanyMask _saved_PLYP_invalid_mask;
		extern std::vector<uint8_t> _saved_PLYP_data;

		if (_saved_PLYP_data.empty()) {
			SlSetLength(0);
		} else {
			SlSetLength(2 + _saved_PLYP_data.size());
			SlWriteUint16(_saved_PLYP_invalid_mask);
			MemoryDumper::GetCurrent()->CopyBytes((const uint8_t *)_saved_PLYP_data.data(), _saved_PLYP_data.size());
		}
		return;
	}

	std::vector<uint8_t> buffer = SlSaveToVector([]() {
		SlWriteUint32((uint32_t)_network_company_server_id.size());
		MemoryDumper::GetCurrent()->CopyBytes((const uint8_t *)_network_company_server_id.data(), _network_company_server_id.size());

		for (const Company *c : Company::Iterate()) {
			SlWriteUint16(c->index);

			const std::string &password = _network_company_states[c->index].password;
			SlWriteUint32((uint32_t)password.size());
			MemoryDumper::GetCurrent()->CopyBytes((const uint8_t *)password.data(), password.size());
		}

		SlWriteUint16(0xFFFF);

		/* Add some random length padding to not make it too obvious from the length whether passwords are set or not */
		std::array<uint8_t, 256> padding;
		RandomBytesWithFallback(padding);
		SlWriteByte(padding[0]);
		MemoryDumper::GetCurrent()->CopyBytes(padding.data() + 1, padding[0]);
	});


	/* Message authentication code */
	std::array<uint8_t, 16> mac;

	/* Use only once per key: random */
	std::array<uint8_t, 24> nonce;
	RandomBytesWithFallback(nonce);

	/* Encrypt in place */
	crypto_aead_lock(buffer.data(), mac.data(), _network_company_password_storage_key.data(), nonce.data(), nullptr, 0, buffer.data(), buffer.size());

	SlSetLength(2 + 16 + 24 + 16 + buffer.size());
	SlWriteUint16(0); // Invalid mask
	static_assert(_network_company_password_storage_token.size() == 16);
	MemoryDumper::GetCurrent()->CopyBytes(_network_company_password_storage_token.data(), 16);
	MemoryDumper::GetCurrent()->CopyBytes(nonce);
	MemoryDumper::GetCurrent()->CopyBytes(mac);
	MemoryDumper::GetCurrent()->CopyBytes(buffer);
}

static const ChunkHandler company_chunk_handlers[] = {
	{ 'PLYR', Save_PLYR, Load_PLYR, Ptrs_PLYR, Check_PLYR, CH_TABLE },
	{ 'PLYX', nullptr,   Load_PLYX, nullptr,   Check_PLYX, CH_READONLY },
	{ 'PLYP', Save_PLYP, Load_PLYP, nullptr,      nullptr, CH_RIFF  },
};

extern const ChunkHandlerTable _company_chunk_handlers(company_chunk_handlers);
