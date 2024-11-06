/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file industry_sl.cpp Code handling saving and loading of industries */

#include "../stdafx.h"
#include "../industry.h"

#include "saveload.h"
#include "newgrf_sl.h"

#include "../safeguards.h"

void OldIndustryAccepted::Reset()
{
	this->old_cargo.fill(INVALID_CARGO);
	this->old_waiting.fill(0);
	this->old_last_accepted.fill(0);
}

void OldIndustryProduced::Reset()
{
	this->old_cargo.fill(INVALID_CARGO);
	this->old_waiting.fill(0);
	this->old_rate.fill(0);
	this->old_this_month_production.fill(0);
	this->old_this_month_transported.fill(0);
	this->old_last_month_production.fill(0);
	this->old_this_month_production.fill(0);
}

OldIndustryAccepted _old_industry_accepted;
OldIndustryProduced _old_industry_produced;

void LoadMoveOldAcceptsProduced(Industry *i)
{
	i->accepted_cargo_count = 0;
	for (uint8_t j = 0; j < _old_industry_accepted.old_cargo.size(); j++) {
		if (_old_industry_accepted.old_cargo[j] != INVALID_CARGO) i->accepted_cargo_count = j + 1;
	}
	i->accepted = std::make_unique<Industry::AcceptedCargo[]>(i->accepted_cargo_count);
	for (uint8_t j = 0; j < i->accepted_cargo_count; j++) {
		Industry::AcceptedCargo &a = i->accepted[j];
		a.cargo = _old_industry_accepted.old_cargo[j];
		a.waiting = _old_industry_accepted.old_waiting[j];
		a.last_accepted = _old_industry_accepted.old_last_accepted[j];
	}

	i->produced_cargo_count = 0;
	for (uint8_t j = 0; j < _old_industry_produced.old_cargo.size(); j++) {
		if (_old_industry_produced.old_cargo[j] != INVALID_CARGO) i->produced_cargo_count = j + 1;
	}
	i->produced = std::make_unique<Industry::ProducedCargo[]>(i->produced_cargo_count);
	for (uint8_t j = 0; j < i->produced_cargo_count; j++) {
		Industry::ProducedCargo &p = i->produced[j];
		p.cargo = _old_industry_produced.old_cargo[j];
		p.waiting = _old_industry_produced.old_waiting[j];
		p.rate = _old_industry_produced.old_rate[j];
		p.history[THIS_MONTH].production = _old_industry_produced.old_this_month_production[j];
		p.history[THIS_MONTH].transported = _old_industry_produced.old_this_month_transported[j];
		p.history[LAST_MONTH].production = _old_industry_produced.old_last_month_production[j];
		p.history[LAST_MONTH].transported = _old_industry_produced.old_last_month_transported[j];
	}
}

static OldPersistentStorage _old_ind_persistent_storage;

struct IndustryAcceptedStructHandler final : public TypedSaveLoadStructHandler<IndustryAcceptedStructHandler, Industry> {
	NamedSaveLoadTable GetDescription() const override
	{
		static const NamedSaveLoad _industry_accepted_desc[] = {
			NSL("cargo",           SLE_VAR(Industry::AcceptedCargo, cargo,         SLE_UINT8)),
			NSL("waiting",         SLE_VAR(Industry::AcceptedCargo, waiting,       SLE_UINT16)),
			NSL("last_accepted",   SLE_VAR(Industry::AcceptedCargo, last_accepted, SLE_INT32)),
		};
		return _industry_accepted_desc;
	}

	void Save(Industry *i) const override
	{
		SlSetStructListLength(i->accepted_cargo_count);
		for (Industry::AcceptedCargo &a : i->Accepted()) {
			SlObjectSaveFiltered(&a, this->GetLoadDescription());
		}
	}

	void Load(Industry *i) const override
	{
		size_t len = SlGetStructListLength(INDUSTRY_NUM_INPUTS);
		i->accepted_cargo_count = static_cast<uint8_t>(len);
		i->accepted = std::make_unique<Industry::AcceptedCargo[]>(i->accepted_cargo_count);
		for (Industry::AcceptedCargo &a : i->Accepted()) {
			SlObjectLoadFiltered(&a, this->GetLoadDescription());
		}
	}
};

struct IndustryProducedHistoryStructHandler final : public TypedSaveLoadStructHandler<IndustryProducedHistoryStructHandler, Industry::ProducedCargo> {
	NamedSaveLoadTable GetDescription() const override
	{
		static const NamedSaveLoad _industry_produced_history_desc[] = {
			NSL("production",  SLE_CONDVAR_X(Industry::ProducedHistory, production,  SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
			NSL("production",  SLE_CONDVAR_X(Industry::ProducedHistory, production,  SLE_UINT32,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),
			NSL("transported", SLE_CONDVAR_X(Industry::ProducedHistory, transported, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
			NSL("transported", SLE_CONDVAR_X(Industry::ProducedHistory, transported, SLE_UINT32,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),
		};
		return _industry_produced_history_desc;
	}

	void Save(Industry::ProducedCargo *p) const override
	{
		if (p->cargo == INVALID_CARGO) {
			/* Don't save any history if cargo slot isn't used. */
			SlSetStructListLength(0);
			return;
		}

		SlSetStructListLength(p->history.size());

		for (Industry::ProducedHistory &h : p->history) {
			SlObject(&h, this->GetLoadDescription());
		}
	}

	void Load(Industry::ProducedCargo *p) const override
	{
		size_t len = SlGetStructListLength(p->history.size());

		for (Industry::ProducedHistory &h : p->history) {
			if (--len > p->history.size()) break; // unsigned so wraps after hitting zero.
			SlObject(&h, this->GetLoadDescription());
		}
	}
};

struct IndustryProducedStructHandler final : public TypedSaveLoadStructHandler<IndustryProducedStructHandler, Industry> {
	NamedSaveLoadTable GetDescription() const override
	{
		static const NamedSaveLoad _industry_produced_desc[] = {
			NSL("cargo",           SLE_VAR(Industry::ProducedCargo, cargo,         SLE_UINT8)),
			NSL("waiting",         SLE_VAR(Industry::ProducedCargo, waiting,       SLE_UINT16)),
			NSL("rate",            SLE_VAR(Industry::ProducedCargo, rate,          SLE_UINT8)),
			NSLT_STRUCTLIST<IndustryProducedHistoryStructHandler>("history"),
		};
		return _industry_produced_desc;
	}

	void Save(Industry *i) const override
	{
		SlSetStructListLength(i->produced_cargo_count);
		for (Industry::ProducedCargo &a : i->Produced()) {
			SlObjectSaveFiltered(&a, this->GetLoadDescription());
		}
	}

	void Load(Industry *i) const override
	{
		size_t len = SlGetStructListLength(INDUSTRY_NUM_OUTPUTS);
		i->produced_cargo_count = static_cast<uint8_t>(len);
		i->produced = std::make_unique<Industry::ProducedCargo[]>(i->produced_cargo_count);
		for (Industry::ProducedCargo &a : i->Produced()) {
			SlObjectLoadFiltered(&a, this->GetLoadDescription());
		}
	}
};

static std::vector<NamedSaveLoad> MakeIndustryDesc()
{
	std::vector<NamedSaveLoad> output;

	output.insert(output.end(), {
		NSL("location.tile",                   SLE_CONDVAR(Industry, location.tile,              SLE_FILE_U16 | SLE_VAR_U32,       SL_MIN_VERSION,                  SLV_6)),
		NSL("location.tile",                   SLE_CONDVAR(Industry, location.tile,              SLE_UINT32,                       SLV_6,                           SL_MAX_VERSION)),
		NSL("location.w",                          SLE_VAR(Industry, location.w,                 SLE_FILE_U8 | SLE_VAR_U16)),
		NSL("location.h",                          SLE_VAR(Industry, location.h,                 SLE_FILE_U8 | SLE_VAR_U16)),
		NSL("town",                                SLE_REF(Industry, town,                       REF_TOWN)),
		NSL("neutral_station",                 SLE_CONDREF(Industry, neutral_station,            REF_STATION,                      SLV_SERVE_NEUTRAL_INDUSTRIES,    SL_MAX_VERSION)),
	});
	if (SlXvIsFeatureMissing(XSLFI_INDUSTRY_CARGO_REORGANISE)) {
		output.insert(output.end(), {
			NSL("", SLE_CONDNULL(2, SL_MIN_VERSION, SLV_61)), // used to be industry's produced_cargo
			NSL("produced_cargo",                  SLEG_CONDARR(_old_industry_produced.old_cargo,                      SLE_UINT8,                     2, SLV_78,                          SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("produced_cargo",                  SLEG_CONDARR(_old_industry_produced.old_cargo,                      SLE_UINT8,                    16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
			NSL("incoming_cargo_waiting",          SLEG_CONDARR(_old_industry_accepted.old_waiting,                    SLE_UINT16,                    3, SLV_70,                          SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("incoming_cargo_waiting",          SLEG_CONDARR(_old_industry_accepted.old_waiting,                    SLE_UINT16,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
			NSL("produced_cargo_waiting",          SLEG_CONDARR(_old_industry_produced.old_waiting,                    SLE_UINT16,                    2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("produced_cargo_waiting",          SLEG_CONDARR(_old_industry_produced.old_waiting,                    SLE_UINT16,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
			NSL("production_rate",                 SLEG_CONDARR(_old_industry_produced.old_rate,                       SLE_UINT8,                     2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("production_rate",                 SLEG_CONDARR(_old_industry_produced.old_rate,                       SLE_UINT8,                    16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
			NSL("", SLE_CONDNULL(3, SL_MIN_VERSION, SLV_61)), // used to be industry's accepts_cargo
			NSL("accepts_cargo",                   SLEG_CONDARR(_old_industry_accepted.old_cargo,                      SLE_UINT8,                     3, SLV_78,                          SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("accepts_cargo",                   SLEG_CONDARR(_old_industry_accepted.old_cargo,                      SLE_UINT8,                    16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
		});
	}
	output.insert(output.end(), {
		NSL("prod_level",                           SLE_VAR(Industry, prod_level,                 SLE_UINT8)),
	});
	if (SlXvIsFeatureMissing(XSLFI_INDUSTRY_CARGO_REORGANISE)) {
		output.insert(output.end(), {
			NSL("this_month_production",           SLEG_CONDARR(_old_industry_produced.old_this_month_production,      SLE_FILE_U16 | SLE_VAR_U32,    2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("this_month_production",         SLEG_CONDARR_X(_old_industry_produced.old_this_month_production,      SLE_FILE_U16 | SLE_VAR_U32,   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
			NSL("this_month_production",         SLEG_CONDARR_X(_old_industry_produced.old_this_month_production,      SLE_UINT32,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),
			NSL("this_month_transported",          SLEG_CONDARR(_old_industry_produced.old_this_month_transported,     SLE_FILE_U16 | SLE_VAR_U32,    2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("this_month_transported",        SLEG_CONDARR_X(_old_industry_produced.old_this_month_transported,     SLE_FILE_U16 | SLE_VAR_U32,   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
			NSL("this_month_transported",        SLEG_CONDARR_X(_old_industry_produced.old_this_month_transported,     SLE_UINT32,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),
			NSL("last_month_pct_transported",      SLE_CONDNULL( 2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("last_month_pct_transported",      SLE_CONDNULL(16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
			NSL("last_month_production",           SLEG_CONDARR(_old_industry_produced.old_last_month_production,      SLE_FILE_U16 | SLE_VAR_U32,    2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("last_month_production",         SLEG_CONDARR_X(_old_industry_produced.old_last_month_production,      SLE_FILE_U16 | SLE_VAR_U32,   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
			NSL("last_month_production",         SLEG_CONDARR_X(_old_industry_produced.old_last_month_production,      SLE_UINT32,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),
			NSL("last_month_transported",          SLEG_CONDARR(_old_industry_produced.old_last_month_transported,     SLE_FILE_U16 | SLE_VAR_U32,    2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("last_month_transported",        SLEG_CONDARR_X(_old_industry_produced.old_last_month_transported,     SLE_FILE_U16 | SLE_VAR_U32,   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
			NSL("last_month_transported",        SLEG_CONDARR_X(_old_industry_produced.old_last_month_transported,     SLE_UINT32,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),
		});
	}
	output.insert(output.end(), {
		NSL("counter",                             SLE_VAR(Industry, counter,                    SLE_UINT16)),

		NSL("type",                                SLE_VAR(Industry, type,                       SLE_UINT8)),
		NSL("owner",                               SLE_VAR(Industry, owner,                      SLE_UINT8)),
		NSL("random_colour",                       SLE_VAR(Industry, random_colour,              SLE_UINT8)),
		NSL("last_prod_year",                  SLE_CONDVAR(Industry, last_prod_year,             SLE_FILE_U8 | SLE_VAR_I32,        SL_MIN_VERSION,                  SLV_31)),
		NSL("last_prod_year",                  SLE_CONDVAR(Industry, last_prod_year,             SLE_INT32,                        SLV_31,                          SL_MAX_VERSION)),
		NSL("was_cargo_delivered",                 SLE_VAR(Industry, was_cargo_delivered,        SLE_UINT8)),
		NSL("ctlflags",                        SLE_CONDVAR(Industry, ctlflags,                   SLE_UINT8,                        SLV_GS_INDUSTRY_CONTROL,         SL_MAX_VERSION)),

		NSL("founder",                         SLE_CONDVAR(Industry, founder,                    SLE_UINT8,                        SLV_70,                          SL_MAX_VERSION)),
		NSL("construction_date",               SLE_CONDVAR(Industry, construction_date,          SLE_INT32,                        SLV_70,                          SL_MAX_VERSION)),
		NSL("construction_type",               SLE_CONDVAR(Industry, construction_type,          SLE_UINT8,                        SLV_70,                          SL_MAX_VERSION)),
	});
	if (SlXvIsFeatureMissing(XSLFI_INDUSTRY_CARGO_REORGANISE)) {
		output.insert(output.end(), {
			NSL("",                                SLEG_CONDVAR(_old_industry_accepted.old_last_accepted[0],  SLE_INT32,                        SLV_70,                          SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
			NSL("last_cargo_accepted_at",          SLEG_CONDARR(_old_industry_accepted.old_last_accepted,     SLE_INT32, 16,                    SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
		});
	}
	output.insert(output.end(), {
		NSL("selected_layout",                 SLE_CONDVAR(Industry, selected_layout,            SLE_UINT8,                        SLV_73,                          SL_MAX_VERSION)),
		NSL("exclusive_supplier",              SLE_CONDVAR(Industry, exclusive_supplier,         SLE_UINT8,                        SLV_GS_INDUSTRY_CONTROL,         SL_MAX_VERSION)),
		NSL("exclusive_consumer",              SLE_CONDVAR(Industry, exclusive_consumer,         SLE_UINT8,                        SLV_GS_INDUSTRY_CONTROL,         SL_MAX_VERSION)),

		NSL("",                               SLEG_CONDARR(_old_ind_persistent_storage.storage,  SLE_UINT32, 16,                   SLV_76,                          SLV_161)),
		NSL("psa",                             SLE_CONDREF(Industry, psa,                        REF_STORAGE,                      SLV_161,                         SL_MAX_VERSION)),

		NSL("", SLE_CONDNULL(1, SLV_82, SLV_197)), // random_triggers
		NSL("random",                          SLE_CONDVAR(Industry, random,                     SLE_UINT16,                       SLV_82,                          SL_MAX_VERSION)),
		NSL("text",                           SLE_CONDSSTR(Industry, text,                       SLE_STR | SLF_ALLOW_CONTROL,      SLV_INDUSTRY_TEXT,               SL_MAX_VERSION)),

		NSL("", SLE_CONDNULL(32, SLV_2, SLV_144)), // old reserved space
	});
	if (SlXvIsFeaturePresent(XSLFI_INDUSTRY_CARGO_REORGANISE)) {
		output.insert(output.end(), {
			NSLT_STRUCTLIST<IndustryAcceptedStructHandler>("accepted"),
			NSLT_STRUCTLIST<IndustryProducedStructHandler>("produced"),
		});
	}

	return output;
}

static void Save_INDY()
{
	std::vector<NamedSaveLoad> industry_desc = MakeIndustryDesc();
	SaveLoadTableData slt = SlTableHeader(industry_desc);

	/* Write the industries */
	for (Industry *ind : Industry::Iterate()) {
		SlSetArrayIndex(ind->index);
		SlObjectSaveFiltered(ind, slt);
	}
}

static void Save_IIDS()
{
	Save_NewGRFMapping(_industry_mngr);
}

static void Save_TIDS()
{
	Save_NewGRFMapping(_industile_mngr);
}

static void Load_INDY()
{
	std::vector<NamedSaveLoad> industry_desc = MakeIndustryDesc();
	SaveLoadTableData slt = SlTableHeaderOrRiff(industry_desc);

	_old_industry_accepted.Reset();
	_old_industry_produced.Reset();
	Industry::ResetIndustryCounts();

	int index;
	while ((index = SlIterateArray()) != -1) {
		Industry *i = new (index) Industry();
		SlObjectLoadFiltered(i, slt);

		/* Before savegame version 161, persistent storages were not stored in a pool. */
		if (IsSavegameVersionBefore(SLV_161) && !IsSavegameVersionBefore(SLV_76)) {
			/* Store the old persistent storage. The GRFID will be added later. */
			assert(PersistentStorage::CanAllocateItem());
			i->psa = new PersistentStorage(0, 0, 0);
			std::copy(std::begin(_old_ind_persistent_storage.storage), std::end(_old_ind_persistent_storage.storage), std::begin(i->psa->storage));
		}
		if (SlXvIsFeatureMissing(XSLFI_INDUSTRY_CARGO_REORGANISE)) {
			LoadMoveOldAcceptsProduced(i);
		}
		Industry::IncIndustryTypeCount(i->type);
	}
}

static void Load_IIDS()
{
	Load_NewGRFMapping(_industry_mngr);
}

static void Load_TIDS()
{
	Load_NewGRFMapping(_industile_mngr);
}

static void Ptrs_INDY()
{
	std::vector<NamedSaveLoad> industry_desc = MakeIndustryDesc();
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(industry_desc);

	for (Industry *i : Industry::Iterate()) {
		SlObjectPtrOrNullFiltered(i, slt);
	}
}

/** Description of the data to save and load in #IndustryBuildData. */
static const NamedSaveLoad _industry_builder_desc[] = {
	NSL("wanted_inds", SLEG_VAR(_industry_builder.wanted_inds, SLE_UINT32)),
};

/** Save industry builder. */
static void Save_IBLD()
{
	SlSaveTableObjectChunk(_industry_builder_desc);
}

/** Load industry builder. */
static void Load_IBLD()
{
	SlLoadTableOrRiffFiltered(_industry_builder_desc);
}

/** Description of the data to save and load in #IndustryTypeBuildData. */
static const NamedSaveLoad _industrytype_builder_desc[] = {
	NSL("probability",  SLE_VAR(IndustryTypeBuildData, probability,  SLE_UINT32)),
	NSL("min_number",   SLE_VAR(IndustryTypeBuildData, min_number,   SLE_UINT8)),
	NSL("target_count", SLE_VAR(IndustryTypeBuildData, target_count, SLE_UINT16)),
	NSL("max_wait",     SLE_VAR(IndustryTypeBuildData, max_wait,     SLE_UINT16)),
	NSL("wait_count",   SLE_VAR(IndustryTypeBuildData, wait_count,   SLE_UINT16)),
};

/** Save industry-type build data. */
static void Save_ITBL()
{
	SaveLoadTableData sld = SlTableHeader(_industrytype_builder_desc);

	for (int i = 0; i < NUM_INDUSTRYTYPES; i++) {
		SlSetArrayIndex(i);
		SlObjectSaveFiltered(_industry_builder.builddata + i, sld);
	}
}

/** Load industry-type build data. */
static void Load_ITBL()
{
	SaveLoadTableData sld = SlTableHeaderOrRiff(_industrytype_builder_desc);

	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		_industry_builder.builddata[it].Reset();
	}
	int index;
	while ((index = SlIterateArray()) != -1) {
		if ((uint)index >= NUM_INDUSTRYTYPES) SlErrorCorrupt("Too many industry builder datas");
		SlObjectLoadFiltered(_industry_builder.builddata + index, sld);
	}
}

static const ChunkHandler industry_chunk_handlers[] = {
	{ 'INDY', Save_INDY,     Load_INDY,     Ptrs_INDY, nullptr, CH_TABLE },
	{ 'IIDS', Save_IIDS,     Load_IIDS,     nullptr,   nullptr, CH_TABLE },
	{ 'TIDS', Save_TIDS,     Load_TIDS,     nullptr,   nullptr, CH_TABLE },
	{ 'IBLD', Save_IBLD,     Load_IBLD,     nullptr,   nullptr, CH_TABLE  },
	{ 'ITBL', Save_ITBL,     Load_ITBL,     nullptr,   nullptr, CH_TABLE },
};

extern const ChunkHandlerTable _industry_chunk_handlers(industry_chunk_handlers);
