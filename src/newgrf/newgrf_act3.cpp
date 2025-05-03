/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_act3.cpp NewGRF Action 0x03 handler. */

#include "../stdafx.h"

#include "../debug.h"
#include "../house.h"
#include "../newgrf_engine.h"
#include "../newgrf_badge.h"
#include "../newgrf_badge_type.h"
#include "../newgrf_cargo.h"
#include "../newgrf_house.h"
#include "../newgrf_station.h"
#include "../industrytype.h"
#include "../newgrf_canal.h"
#include "../newgrf_airporttiles.h"
#include "../newgrf_airport.h"
#include "../newgrf_object.h"
#include "../newgrf_newsignals.h"
#include "../newgrf_newlandscape.h"
#include "../error.h"
#include "../vehicle_base.h"
#include "../road.h"
#include "../newgrf_roadstop.h"
#include "newgrf_bytereader.h"
#include "newgrf_internal_vehicle.h"
#include "newgrf_internal.h"

#include "../safeguards.h"


static CargoType TranslateCargo(uint8_t feature, uint8_t ctype)
{
	/* Special cargo types for purchase list and stations */
	if ((feature == GSF_STATIONS || feature == GSF_ROADSTOPS) && ctype == 0xFE) return CargoGRFFileProps::SG_DEFAULT_NA;
	if (ctype == 0xFF) return CargoGRFFileProps::SG_PURCHASE;

	auto cargo_list = GetCargoTranslationTable(*_cur_gps.grffile);

	/* Check if the cargo type is out of bounds of the cargo translation table */
	if (ctype >= cargo_list.size()) {
		GrfMsg(1, "TranslateCargo: Cargo type {} out of range (max {}), skipping.", ctype, (unsigned int)_cur_gps.grffile->cargo_list.size() - 1);
		return INVALID_CARGO;
	}

	/* Look up the cargo label from the translation table */
	CargoLabel cl = cargo_list[ctype];
	if (cl == CT_INVALID) {
		GrfMsg(5, "TranslateCargo: Cargo type {} not available in this climate, skipping.", ctype);
		return INVALID_CARGO;
	}

	CargoType cargo_type = GetCargoTypeByLabel(cl);
	if (!IsValidCargoType(cargo_type)) {
		GrfMsg(5, "TranslateCargo: Cargo '{:c}{:c}{:c}{:c}' unsupported, skipping.", GB(cl.base(), 24, 8), GB(cl.base(), 16, 8), GB(cl.base(), 8, 8), GB(cl.base(), 0, 8));
		return INVALID_CARGO;
	}

	GrfMsg(6, "TranslateCargo: Cargo '{:c}{:c}{:c}{:c}' mapped to cargo type {}.", GB(cl.base(), 24, 8), GB(cl.base(), 16, 8), GB(cl.base(), 8, 8), GB(cl.base(), 0, 8), cargo_type);
	return cargo_type;
}

static const SpriteGroup *GetGroupByID(uint16_t groupid)
{
	if ((size_t)groupid >= _cur_gps.spritegroups.size()) return nullptr;

	const SpriteGroup *result = _cur_gps.spritegroups[groupid];
	return result;
}

static bool IsValidGroupID(uint16_t groupid, std::string_view function)
{
	if ((size_t)groupid >= _cur_gps.spritegroups.size() || _cur_gps.spritegroups[groupid] == nullptr) {
		GrfMsg(1, "{}: Spritegroup 0x{:04X} out of range or empty, skipping.", function, groupid);
		return false;
	}

	return true;
}

static void VehicleMapSpriteGroup(ByteReader &buf, uint8_t feature, uint8_t idcount)
{
	static std::vector<EngineID> last_engines; // Engine IDs are remembered in case the next action is a wagon override.
	bool wagover = false;

	/* Test for 'wagon override' flag */
	if (HasBit(idcount, 7)) {
		wagover = true;
		/* Strip off the flag */
		idcount = GB(idcount, 0, 7);

		if (last_engines.empty()) {
			GrfMsg(0, "VehicleMapSpriteGroup: WagonOverride: No engine to do override with");
			return;
		}

		GrfMsg(6, "VehicleMapSpriteGroup: WagonOverride: {} engines, {} wagons", last_engines.size(), idcount);
	} else {
		last_engines.resize(idcount);
	}

	TempBufferST<EngineID> engines(idcount);
	for (uint i = 0; i < idcount; i++) {
		Engine *e = GetNewEngine(_cur_gps.grffile, (VehicleType)feature, buf.ReadExtendedByte());
		if (e == nullptr) {
			/* No engine could be allocated?!? Deal with it. Okay,
			 * this might look bad. Also make sure this NewGRF
			 * gets disabled, as a half loaded one is bad. */
			HandleChangeInfoResult("VehicleMapSpriteGroup", CIR_INVALID_ID, (GrfSpecFeature)0, 0);
			return;
		}

		engines[i] = e->index;
		if (!wagover) last_engines[i] = engines[i];
	}

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (!IsValidGroupID(groupid, "VehicleMapSpriteGroup")) continue;

		GrfMsg(8, "VehicleMapSpriteGroup: * [{}] Cargo type 0x{:X}, group id 0x{:02X}", c, ctype, groupid);

		CargoType cargo_type = TranslateCargo(feature, ctype);
		if (!IsValidCargoType(cargo_type)) continue;

		for (uint i = 0; i < idcount; i++) {
			EngineID engine = engines[i];

			GrfMsg(7, "VehicleMapSpriteGroup: [{}] Engine {}...", i, engine);

			if (wagover) {
				SetWagonOverrideSprites(engine, cargo_type, GetGroupByID(groupid), last_engines);
			} else {
				SetCustomEngineSprites(engine, cargo_type, GetGroupByID(groupid));
			}
		}
	}

	uint16_t groupid = buf.ReadWord();
	if (!IsValidGroupID(groupid, "VehicleMapSpriteGroup")) return;

	GrfMsg(8, "-- Default group id 0x{:04X}", groupid);

	for (uint i = 0; i < idcount; i++) {
		EngineID engine = engines[i];

		if (wagover) {
			SetWagonOverrideSprites(engine, CargoGRFFileProps::SG_DEFAULT, GetGroupByID(groupid), last_engines);
		} else {
			SetCustomEngineSprites(engine, CargoGRFFileProps::SG_DEFAULT, GetGroupByID(groupid));
			SetEngineGRF(engine, _cur_gps.grffile);
		}
	}
}


static void CanalMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	TempBufferST<uint16_t> cfs(idcount);
	for (uint i = 0; i < idcount; i++) {
		cfs[i] = buf.ReadExtendedByte();
	}

	uint8_t cidcount = buf.ReadByte();
	buf.Skip(cidcount * 3);

	uint16_t groupid = buf.ReadWord();
	if (!IsValidGroupID(groupid, "CanalMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		uint16_t cf = cfs[i];

		if (cf >= CF_END) {
			GrfMsg(1, "CanalMapSpriteGroup: Canal subset {} out of range, skipping", cf);
			continue;
		}

		_water_feature[cf].grffile = _cur_gps.grffile;
		_water_feature[cf].group = GetGroupByID(groupid);
	}
}


static void StationMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	if (_cur_gps.grffile->stations.empty()) {
		GrfMsg(1, "StationMapSpriteGroup: No stations defined, skipping");
		return;
	}

	TempBufferST<uint16_t> stations(idcount);
	for (uint i = 0; i < idcount; i++) {
		stations[i] = buf.ReadExtendedByte();
	}

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (!IsValidGroupID(groupid, "StationMapSpriteGroup")) continue;

		CargoType cargo_type = TranslateCargo(GSF_STATIONS, ctype);
		if (!IsValidCargoType(cargo_type)) continue;

		for (uint i = 0; i < idcount; i++) {
			StationSpec *statspec = stations[i] >= _cur_gps.grffile->stations.size() ? nullptr : _cur_gps.grffile->stations[stations[i]].get();

			if (statspec == nullptr) {
				GrfMsg(1, "StationMapSpriteGroup: Station with ID 0x{:X} undefined, skipping", stations[i]);
				continue;
			}

			statspec->grf_prop.SetSpriteGroup(cargo_type, GetGroupByID(groupid));
		}
	}

	uint16_t groupid = buf.ReadWord();
	if (!IsValidGroupID(groupid, "StationMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		StationSpec *statspec = stations[i] >= _cur_gps.grffile->stations.size() ? nullptr : _cur_gps.grffile->stations[stations[i]].get();

		if (statspec == nullptr) {
			GrfMsg(1, "StationMapSpriteGroup: Station with ID 0x{:X} undefined, skipping", stations[i]);
			continue;
		}

		if (statspec->grf_prop.HasGrfFile()) {
			GrfMsg(1, "StationMapSpriteGroup: Station with ID 0x{:X} mapped multiple times, skipping", stations[i]);
			continue;
		}

		statspec->grf_prop.SetSpriteGroup(CargoGRFFileProps::SG_DEFAULT, GetGroupByID(groupid));
		statspec->grf_prop.SetGRFFile(_cur_gps.grffile);
		statspec->grf_prop.local_id = stations[i];
		StationClass::Assign(statspec);
	}
}


static void TownHouseMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	if (_cur_gps.grffile->housespec.empty()) {
		GrfMsg(1, "TownHouseMapSpriteGroup: No houses defined, skipping");
		return;
	}

	TempBufferST<uint16_t> houses(idcount);
	for (uint i = 0; i < idcount; i++) {
		houses[i] = buf.ReadExtendedByte();
	}

	auto set_sprite_group = [&houses, idcount](StandardSpriteGroup key, uint16_t groupid) {
		if (!IsValidGroupID(groupid, "TownHouseMapSpriteGroup")) return;

		for (uint i = 0; i < idcount; i++) {
			HouseSpec *hs = houses[i] >= _cur_gps.grffile->housespec.size() ? nullptr : _cur_gps.grffile->housespec[houses[i]].get();

			if (hs == nullptr) {
				GrfMsg(1, "TownHouseMapSpriteGroup: House {} undefined, skipping.", houses[i]);
				continue;
			}

			hs->grf_prop.SetSpriteGroup(key, GetGroupByID(groupid));
		}
	};

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (ctype == 0xFF) {
			set_sprite_group(StandardSpriteGroup::Purchase, groupid);
		} else {
			GrfMsg(1, "TownHouseMapSpriteGroup: Invalid cargo bitnum {} for houses, skipping.", ctype);
		}
	}
	set_sprite_group(StandardSpriteGroup::Default, buf.ReadWord());
}

static void IndustryMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	if (_cur_gps.grffile->industryspec.empty()) {
		GrfMsg(1, "IndustryMapSpriteGroup: No industries defined, skipping");
		return;
	}

	TempBufferST<uint16_t> industries(idcount);
	for (uint i = 0; i < idcount; i++) {
		industries[i] = buf.ReadExtendedByte();
	}

	auto set_sprite_group = [&industries, idcount](StandardSpriteGroup key, uint16_t groupid) {
		if (!IsValidGroupID(groupid, "IndustryMapSpriteGroup")) return;

		for (uint i = 0; i < idcount; i++) {
			IndustrySpec *indsp = industries[i] >= _cur_gps.grffile->industryspec.size() ? nullptr : _cur_gps.grffile->industryspec[industries[i]].get();

			if (indsp == nullptr) {
				GrfMsg(1, "IndustryMapSpriteGroup: Industry {} undefined, skipping", industries[i]);
				continue;
			}

			indsp->grf_prop.SetSpriteGroup(key, GetGroupByID(groupid));
		}
	};

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (ctype == 0xFF) {
			set_sprite_group(StandardSpriteGroup::Purchase, groupid);
		} else {
			GrfMsg(1, "IndustryMapSpriteGroup: Invalid cargo bitnum {} for industries, skipping.", ctype);
		}
	}
	set_sprite_group(StandardSpriteGroup::Default, buf.ReadWord());
}

static void IndustrytileMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	if (_cur_gps.grffile->indtspec.empty()) {
		GrfMsg(1, "IndustrytileMapSpriteGroup: No industry tiles defined, skipping");
		return;
	}

	TempBufferST<uint16_t> indtiles(idcount);
	for (uint i = 0; i < idcount; i++) {
		indtiles[i] = buf.ReadExtendedByte();
	}

	auto set_sprite_group = [&indtiles, idcount](StandardSpriteGroup key, uint16_t groupid) {
		if (!IsValidGroupID(groupid, "IndustrytileMapSpriteGroup")) return;

		for (uint i = 0; i < idcount; i++) {
			IndustryTileSpec *indtsp = indtiles[i] >= _cur_gps.grffile->indtspec.size() ? nullptr : _cur_gps.grffile->indtspec[indtiles[i]].get();

			if (indtsp == nullptr) {
				GrfMsg(1, "IndustrytileMapSpriteGroup: Industry tile {} undefined, skipping", indtiles[i]);
				continue;
			}

			indtsp->grf_prop.SetSpriteGroup(key, GetGroupByID(groupid));
		}
	};

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (ctype == 0xFF) {
			set_sprite_group(StandardSpriteGroup::Purchase, groupid);
		} else {
			GrfMsg(1, "IndustrytileMapSpriteGroup: Invalid cargo bitnum {} for industry tiles, skipping.", ctype);
		}
	}
	set_sprite_group(StandardSpriteGroup::Default, buf.ReadWord());
}

static void CargoMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	TempBufferST<uint16_t> cargoes(idcount);
	for (uint i = 0; i < idcount; i++) {
		cargoes[i] = buf.ReadExtendedByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8_t cidcount = buf.ReadByte();
	buf.Skip(cidcount * 3);

	uint16_t groupid = buf.ReadWord();
	if (!IsValidGroupID(groupid, "CargoMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		uint16_t cargo_type = cargoes[i];

		if (cargo_type >= NUM_CARGO) {
			GrfMsg(1, "CargoMapSpriteGroup: Cargo ID {} out of range, skipping", cargo_type);
			continue;
		}

		CargoSpec *cs = CargoSpec::Get(cargo_type);
		cs->grffile = _cur_gps.grffile;
		cs->group = GetGroupByID(groupid);
	}
}

static void SignalsMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	TempBufferST<uint16_t> ids(idcount);
	for (uint i = 0; i < idcount; i++) {
		ids[i] = buf.ReadExtendedByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8_t cidcount = buf.ReadByte();
	buf.Skip(cidcount * 3);

	uint16_t groupid = buf.ReadWord();
	if (!IsValidGroupID(groupid, "SignalsMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		uint16_t id = ids[i];

		switch (id) {
			case NSA3ID_CUSTOM_SIGNALS:
				_cur_gps.grffile->new_signals_group = GetGroupByID(groupid);
				if (!HasBit(_cur_gps.grffile->new_signal_ctrl_flags, NSCF_GROUPSET)) {
					SetBit(_cur_gps.grffile->new_signal_ctrl_flags, NSCF_GROUPSET);
					_new_signals_grfs.push_back(_cur_gps.grffile);
				}
				break;

			default:
				GrfMsg(1, "SignalsMapSpriteGroup: ID not implemented: {}", id);
			break;
		}
	}
}

static void ObjectMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	if (_cur_gps.grffile->objectspec.empty()) {
		GrfMsg(1, "ObjectMapSpriteGroup: No object tiles defined, skipping");
		return;
	}

	TempBufferST<uint16_t> objects(idcount);
	for (uint i = 0; i < idcount; i++) {
		objects[i] = buf.ReadExtendedByte();
	}

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (!IsValidGroupID(groupid, "ObjectMapSpriteGroup")) continue;

		/* The only valid option here is purchase list sprite groups. */
		if (ctype != 0xFF) {
			GrfMsg(1, "ObjectMapSpriteGroup: Invalid cargo bitnum {} for objects, skipping.", ctype);
			continue;
		}

		for (uint i = 0; i < idcount; i++) {
			ObjectSpec *spec = (objects[i] >= _cur_gps.grffile->objectspec.size()) ? nullptr : _cur_gps.grffile->objectspec[objects[i]].get();

			if (spec == nullptr) {
				GrfMsg(1, "ObjectMapSpriteGroup: Object with ID 0x{:X} undefined, skipping", objects[i]);
				continue;
			}

			spec->grf_prop.SetSpriteGroup(StandardSpriteGroup::Purchase, GetGroupByID(groupid));
		}
	}

	uint16_t groupid = buf.ReadWord();
	if (!IsValidGroupID(groupid, "ObjectMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		ObjectSpec *spec = (objects[i] >= _cur_gps.grffile->objectspec.size()) ? nullptr : _cur_gps.grffile->objectspec[objects[i]].get();

		if (spec == nullptr) {
			GrfMsg(1, "ObjectMapSpriteGroup: Object with ID 0x{:X} undefined, skipping", objects[i]);
			continue;
		}

		if (spec->grf_prop.HasGrfFile()) {
			GrfMsg(1, "ObjectMapSpriteGroup: Object with ID 0x{:X} mapped multiple times, skipping", objects[i]);
			continue;
		}

		spec->grf_prop.SetSpriteGroup(StandardSpriteGroup::Default, GetGroupByID(groupid));
		spec->grf_prop.SetGRFFile(_cur_gps.grffile);
		spec->grf_prop.local_id = objects[i];
	}
}

static void RailTypeMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	TempBufferST<uint8_t> railtypes(idcount);
	for (uint i = 0; i < idcount; i++) {
		uint16_t id = buf.ReadExtendedByte();
		railtypes[i] = id < RAILTYPE_END ? _cur_gps.grffile->railtype_map[id] : INVALID_RAILTYPE;
	}

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (!IsValidGroupID(groupid, "RailTypeMapSpriteGroup")) continue;

		if (ctype >= RTSG_END) continue;

		extern RailTypeInfo _railtypes[RAILTYPE_END];
		for (uint i = 0; i < idcount; i++) {
			if (railtypes[i] != INVALID_RAILTYPE) {
				RailTypeInfo *rti = &_railtypes[railtypes[i]];

				rti->grffile[ctype] = _cur_gps.grffile;
				rti->group[ctype] = GetGroupByID(groupid);
			}
		}
	}

	/* Railtypes do not use the default group. */
	buf.ReadWord();
}

static void RoadTypeMapSpriteGroup(ByteReader &buf, uint8_t idcount, RoadTramType rtt)
{
	std::array<RoadType, ROADTYPE_END> &type_map = (rtt == RTT_TRAM) ? _cur_gps.grffile->tramtype_map : _cur_gps.grffile->roadtype_map;

	TempBufferST<uint8_t> roadtypes(idcount);
	for (uint i = 0; i < idcount; i++) {
		uint16_t id = buf.ReadExtendedByte();
		roadtypes[i] = id < ROADTYPE_END ? type_map[id] : INVALID_ROADTYPE;
	}

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (!IsValidGroupID(groupid, "RoadTypeMapSpriteGroup")) continue;

		if (ctype >= ROTSG_END) continue;

		extern RoadTypeInfo _roadtypes[ROADTYPE_END];
		for (uint i = 0; i < idcount; i++) {
			if (roadtypes[i] != INVALID_ROADTYPE) {
				RoadTypeInfo *rti = &_roadtypes[roadtypes[i]];

				rti->grffile[ctype] = _cur_gps.grffile;
				rti->group[ctype] = GetGroupByID(groupid);
			}
		}
	}

	/* Roadtypes do not use the default group. */
	buf.ReadWord();
}

static void AirportMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	if (_cur_gps.grffile->airportspec.empty()) {
		GrfMsg(1, "AirportMapSpriteGroup: No airports defined, skipping");
		return;
	}

	TempBufferST<uint16_t> airports(idcount);
	for (uint i = 0; i < idcount; i++) {
		airports[i] = buf.ReadExtendedByte();
	}

	auto set_sprite_group = [&airports, idcount](StandardSpriteGroup key, uint16_t groupid) {
		if (!IsValidGroupID(groupid, "AirportMapSpriteGroup")) return;

		for (uint i = 0; i < idcount; i++) {
			AirportSpec *as = airports[i] >= _cur_gps.grffile->airportspec.size() ? nullptr : _cur_gps.grffile->airportspec[airports[i]].get();

			if (as == nullptr) {
				GrfMsg(1, "AirportMapSpriteGroup: Airport {} undefined, skipping", airports[i]);
				continue;
			}

			as->grf_prop.SetSpriteGroup(key, GetGroupByID(groupid));
		}
	};

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (ctype == 0xFF) {
			set_sprite_group(StandardSpriteGroup::Purchase, groupid);
		} else {
			GrfMsg(1, "AirportMapSpriteGroup: Invalid cargo bitnum {} for airports, skipping.", ctype);
		}
	}
	set_sprite_group(StandardSpriteGroup::Default, buf.ReadWord());
}

static void AirportTileMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	if (_cur_gps.grffile->airtspec.empty()) {
		GrfMsg(1, "AirportTileMapSpriteGroup: No airport tiles defined, skipping");
		return;
	}

	TempBufferST<uint16_t> airptiles(idcount);
	for (uint i = 0; i < idcount; i++) {
		airptiles[i] = buf.ReadExtendedByte();
	}

	auto set_sprite_group = [&airptiles, idcount](StandardSpriteGroup key, uint16_t groupid) {
		if (!IsValidGroupID(groupid, "AirportTileMapSpriteGroup")) return;

		for (uint i = 0; i < idcount; i++) {
			AirportTileSpec *airtsp = airptiles[i] >= _cur_gps.grffile->airtspec.size() ? nullptr : _cur_gps.grffile->airtspec[airptiles[i]].get();

			if (airtsp == nullptr) {
				GrfMsg(1, "AirportTileMapSpriteGroup: Airport tile {} undefined, skipping", airptiles[i]);
				continue;
			}

			airtsp->grf_prop.SetSpriteGroup(key, GetGroupByID(groupid));
		}
	};

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (ctype == 0xFF) {
			set_sprite_group(StandardSpriteGroup::Purchase, groupid);
		} else {
			GrfMsg(1, "AirportTileMapSpriteGroup: Invalid cargo bitnum {} for airport tiles, skipping.", ctype);
		}
	}
	set_sprite_group(StandardSpriteGroup::Default, buf.ReadWord());
}

static void RoadStopMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	TempBufferST<uint16_t> roadstops(idcount);
	for (uint i = 0; i < idcount; i++) {
		roadstops[i] = buf.ReadExtendedByte();
	}

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (!IsValidGroupID(groupid, "RoadStopMapSpriteGroup")) continue;

		CargoType cargo_type = TranslateCargo(GSF_ROADSTOPS, ctype);
		if (!IsValidCargoType(cargo_type)) continue;

		for (uint i = 0; i < idcount; i++) {
			RoadStopSpec *roadstopspec = (roadstops[i] >= _cur_gps.grffile->roadstops.size()) ? nullptr : _cur_gps.grffile->roadstops[roadstops[i]].get();

			if (roadstopspec == nullptr) {
				GrfMsg(1, "RoadStopMapSpriteGroup: Road stop with ID 0x{:X} does not exist, skipping", roadstops[i]);
				continue;
			}

			roadstopspec->grf_prop.SetSpriteGroup(cargo_type, GetGroupByID(groupid));
		}
	}

	uint16_t groupid = buf.ReadWord();
	if (!IsValidGroupID(groupid, "RoadStopMapSpriteGroup")) return;

	if (_cur_gps.grffile->roadstops.empty()) {
		GrfMsg(0, "RoadStopMapSpriteGroup: No roadstops defined, skipping.");
		return;
	}

	for (uint i = 0; i < idcount; i++) {
		RoadStopSpec *roadstopspec = (roadstops[i] >= _cur_gps.grffile->roadstops.size()) ? nullptr : _cur_gps.grffile->roadstops[roadstops[i]].get();

		if (roadstopspec == nullptr) {
			GrfMsg(1, "RoadStopMapSpriteGroup: Road stop with ID 0x{:X} does not exist, skipping.", roadstops[i]);
			continue;
		}

		if (roadstopspec->grf_prop.HasGrfFile()) {
			GrfMsg(1, "RoadStopMapSpriteGroup: Road stop with ID 0x{:X} mapped multiple times, skipping", roadstops[i]);
			continue;
		}

		roadstopspec->grf_prop.SetSpriteGroup(CargoGRFFileProps::SG_DEFAULT, GetGroupByID(groupid));
		roadstopspec->grf_prop.SetGRFFile(_cur_gps.grffile);
		roadstopspec->grf_prop.local_id = roadstops[i];
		RoadStopClass::Assign(roadstopspec);
	}
}

static void BadgeMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	if (_cur_gps.grffile->badge_map.empty()) {
		GrfMsg(1, "BadgeMapSpriteGroup: No badges defined, skipping");
		return;
	}

	std::vector<uint16_t> local_ids;
	local_ids.reserve(idcount);
	for (uint i = 0; i < idcount; i++) {
		local_ids.push_back(buf.ReadExtendedByte());
	}

	uint8_t cidcount = buf.ReadByte();
	for (uint c = 0; c < cidcount; c++) {
		uint8_t ctype = buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (!IsValidGroupID(groupid, "BadgeMapSpriteGroup")) continue;

		if (ctype >= GSF_END) continue;

		for (const auto &local_id : local_ids) {
			auto found = _cur_gps.grffile->badge_map.find(local_id);
			if (found == std::end(_cur_gps.grffile->badge_map)) {
				GrfMsg(1, "BadgeMapSpriteGroup: Badge {} undefined, skipping", local_id);
				continue;
			}

			auto &badge = *GetBadge(found->second);
			badge.grf_prop.SetSpriteGroup(static_cast<GrfSpecFeature>(ctype), _cur_gps.spritegroups[groupid]);
		}
	}

	uint16_t groupid = buf.ReadWord();
	if (!IsValidGroupID(groupid, "BadgeMapSpriteGroup")) return;

	for (auto &local_id : local_ids) {
		auto found = _cur_gps.grffile->badge_map.find(local_id);
		if (found == std::end(_cur_gps.grffile->badge_map)) {
			GrfMsg(1, "BadgeMapSpriteGroup: Badge {} undefined, skipping", local_id);
			continue;
		}

		auto &badge = *GetBadge(found->second);
		badge.grf_prop.SetSpriteGroup(GSF_DEFAULT, _cur_gps.spritegroups[groupid]);
		badge.grf_prop.SetGRFFile(_cur_gps.grffile);
		badge.grf_prop.local_id = local_id;
	}
}

static void NewLandscapeMapSpriteGroup(ByteReader &buf, uint8_t idcount)
{
	TempBufferST<uint16_t> ids(idcount);
	for (uint i = 0; i < idcount; i++) {
		ids[i] = buf.ReadExtendedByte();
	}

	/* Skip the cargo type section, we only care about the default group */
	uint8_t cidcount = buf.ReadByte();
	buf.Skip(cidcount * 3);

	uint16_t groupid = buf.ReadWord();
	if (!IsValidGroupID(groupid, "NewLandscapeMapSpriteGroup")) return;

	for (uint i = 0; i < idcount; i++) {
		uint16_t id = ids[i];

		switch (id) {
			case NLA3ID_CUSTOM_ROCKS:
				_cur_gps.grffile->new_rocks_group = GetGroupByID(groupid);
				if (!HasBit(_cur_gps.grffile->new_landscape_ctrl_flags, NLCF_ROCKS_SET)) {
					SetBit(_cur_gps.grffile->new_landscape_ctrl_flags, NLCF_ROCKS_SET);
					_new_landscape_rocks_grfs.push_back(_cur_gps.grffile);
				}
				break;

			default:
				GrfMsg(1, "NewLandscapeMapSpriteGroup: ID not implemented: {}", id);
			break;
		}
	}
}

/* Action 0x03 */
static void FeatureMapSpriteGroup(ByteReader &buf)
{
	/* <03> <feature> <n-id> <ids>... <num-cid> [<cargo-type> <cid>]... <def-cid>
	 * id-list    := [<id>] [id-list]
	 * cargo-list := <cargo-type> <cid> [cargo-list]
	 *
	 * B feature       see action 0
	 * B n-id          bits 0-6: how many IDs this definition applies to
	 *                 bit 7: if set, this is a wagon override definition (see below)
	 * E ids           the IDs for which this definition applies
	 * B num-cid       number of cargo IDs (sprite group IDs) in this definition
	 *                 can be zero, in that case the def-cid is used always
	 * B cargo-type    type of this cargo type (e.g. mail=2, wood=7, see below)
	 * W cid           cargo ID (sprite group ID) for this type of cargo
	 * W def-cid       default cargo ID (sprite group ID) */

	GrfSpecFeatureRef feature_ref = ReadFeature(buf.ReadByte());
	GrfSpecFeature feature = feature_ref.id;
	uint8_t idcount = buf.ReadByte();

	if (feature >= GSF_END) {
		GrfMsg(1, "FeatureMapSpriteGroup: Unsupported feature {}, skipping", GetFeatureString(feature_ref));
		return;
	}

	/* If idcount is zero, this is a feature callback */
	if (idcount == 0) {
		/* Skip number of cargo ids? */
		buf.ReadByte();
		uint16_t groupid = buf.ReadWord();
		if (!IsValidGroupID(groupid, "FeatureMapSpriteGroup")) return;

		GrfMsg(6, "FeatureMapSpriteGroup: Adding generic feature callback for feature {}", GetFeatureString(feature_ref));

		AddGenericCallback(feature, _cur_gps.grffile, GetGroupByID(groupid));
		return;
	}

	/* Mark the feature as used by the grf (generic callbacks do not count) */
	SetBit(_cur_gps.grffile->grf_features, feature);

	GrfMsg(6, "FeatureMapSpriteGroup: Feature {}, {} ids", GetFeatureString(feature_ref), idcount);

	switch (feature) {
		case GSF_TRAINS:
		case GSF_ROADVEHICLES:
		case GSF_SHIPS:
		case GSF_AIRCRAFT:
			VehicleMapSpriteGroup(buf, feature, idcount);
			return;

		case GSF_CANALS:
			CanalMapSpriteGroup(buf, idcount);
			return;

		case GSF_STATIONS:
			StationMapSpriteGroup(buf, idcount);
			return;

		case GSF_HOUSES:
			TownHouseMapSpriteGroup(buf, idcount);
			return;

		case GSF_INDUSTRIES:
			IndustryMapSpriteGroup(buf, idcount);
			return;

		case GSF_INDUSTRYTILES:
			IndustrytileMapSpriteGroup(buf, idcount);
			return;

		case GSF_CARGOES:
			CargoMapSpriteGroup(buf, idcount);
			return;

		case GSF_AIRPORTS:
			AirportMapSpriteGroup(buf, idcount);
			return;

		case GSF_SIGNALS:
			SignalsMapSpriteGroup(buf, idcount);
			break;

		case GSF_OBJECTS:
			ObjectMapSpriteGroup(buf, idcount);
			break;

		case GSF_RAILTYPES:
			RailTypeMapSpriteGroup(buf, idcount);
			break;

		case GSF_ROADTYPES:
			RoadTypeMapSpriteGroup(buf, idcount, RTT_ROAD);
			break;

		case GSF_TRAMTYPES:
			RoadTypeMapSpriteGroup(buf, idcount, RTT_TRAM);
			break;

		case GSF_AIRPORTTILES:
			AirportTileMapSpriteGroup(buf, idcount);
			return;

		case GSF_ROADSTOPS:
			RoadStopMapSpriteGroup(buf, idcount);
			return;

		case GSF_BADGES:
			BadgeMapSpriteGroup(buf, idcount);
			break;

		case GSF_NEWLANDSCAPE:
			NewLandscapeMapSpriteGroup(buf, idcount);
			return;

		default:
			GrfMsg(1, "FeatureMapSpriteGroup: Unsupported feature {}, skipping", GetFeatureString(feature_ref));
			return;
	}
}

template <> void GrfActionHandler<0x03>::FileScan(ByteReader &) { }
template <> void GrfActionHandler<0x03>::SafetyScan(ByteReader &buf) { GRFUnsafe(buf); }
template <> void GrfActionHandler<0x03>::LabelScan(ByteReader &) { }
template <> void GrfActionHandler<0x03>::Init(ByteReader &) { }
template <> void GrfActionHandler<0x03>::Reserve(ByteReader &) { }
template <> void GrfActionHandler<0x03>::Activation(ByteReader &buf) { FeatureMapSpriteGroup(buf); }
