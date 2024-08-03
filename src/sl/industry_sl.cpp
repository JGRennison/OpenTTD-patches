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

static OldPersistentStorage _old_ind_persistent_storage;

static const NamedSaveLoad _industry_desc[] = {
	NSL("location.tile",                   SLE_CONDVAR(Industry, location.tile,              SLE_FILE_U16 | SLE_VAR_U32,       SL_MIN_VERSION,                  SLV_6)),
	NSL("location.tile",                   SLE_CONDVAR(Industry, location.tile,              SLE_UINT32,                       SLV_6,                           SL_MAX_VERSION)),
	NSL("location.w",                          SLE_VAR(Industry, location.w,                 SLE_FILE_U8 | SLE_VAR_U16)),
	NSL("location.h",                          SLE_VAR(Industry, location.h,                 SLE_FILE_U8 | SLE_VAR_U16)),
	NSL("town",                                SLE_REF(Industry, town,                       REF_TOWN)),
	NSL("neutral_station",                 SLE_CONDREF(Industry, neutral_station,            REF_STATION,                      SLV_SERVE_NEUTRAL_INDUSTRIES,    SL_MAX_VERSION)),
	NSL("", SLE_CONDNULL(2, SL_MIN_VERSION, SLV_61)), // used to be industry's produced_cargo
	NSL("produced_cargo",                  SLE_CONDARR(Industry, produced_cargo,             SLE_UINT8,                    2,  SLV_78,                          SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("produced_cargo",                  SLE_CONDARR(Industry, produced_cargo,             SLE_UINT8,                   16,  SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
	NSL("incoming_cargo_waiting",          SLE_CONDARR(Industry, incoming_cargo_waiting,     SLE_UINT16,                   3,  SLV_70,                          SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("incoming_cargo_waiting",          SLE_CONDARR(Industry, incoming_cargo_waiting,     SLE_UINT16,                  16,  SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
	NSL("produced_cargo_waiting",          SLE_CONDARR(Industry, produced_cargo_waiting,     SLE_UINT16,                   2,  SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("produced_cargo_waiting",          SLE_CONDARR(Industry, produced_cargo_waiting,     SLE_UINT16,                  16,  SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
	NSL("production_rate",                 SLE_CONDARR(Industry, production_rate,            SLE_UINT8,                    2,  SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("production_rate",                 SLE_CONDARR(Industry, production_rate,            SLE_UINT8,                   16,  SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
	NSL("", SLE_CONDNULL(3, SL_MIN_VERSION, SLV_61)), // used to be industry's accepts_cargo
	NSL("accepts_cargo",                   SLE_CONDARR(Industry, accepts_cargo,              SLE_UINT8,                    3,  SLV_78,                          SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("accepts_cargo",                   SLE_CONDARR(Industry, accepts_cargo,              SLE_UINT8,                   16,  SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
	NSL("prod_level",                          SLE_VAR(Industry, prod_level,                 SLE_UINT8)),
	NSL("this_month_production",           SLE_CONDARR(Industry, this_month_production,      SLE_FILE_U16 | SLE_VAR_U32,    2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("this_month_production",         SLE_CONDARR_X(Industry, this_month_production,      SLE_FILE_U16 | SLE_VAR_U32,   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
	NSL("this_month_production",         SLE_CONDARR_X(Industry, this_month_production,      SLE_UINT32,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),
	NSL("this_month_transported",          SLE_CONDARR(Industry, this_month_transported,     SLE_FILE_U16 | SLE_VAR_U32,    2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("this_month_transported",        SLE_CONDARR_X(Industry, this_month_transported,     SLE_FILE_U16 | SLE_VAR_U32,   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
	NSL("this_month_transported",        SLE_CONDARR_X(Industry, this_month_transported,     SLE_UINT32,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),
	NSL("last_month_pct_transported",      SLE_CONDARR(Industry, last_month_pct_transported, SLE_UINT8,                     2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("last_month_pct_transported",      SLE_CONDARR(Industry, last_month_pct_transported, SLE_UINT8,                    16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
	NSL("last_month_production",           SLE_CONDARR(Industry, last_month_production,      SLE_FILE_U16 | SLE_VAR_U32,    2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("last_month_production",         SLE_CONDARR_X(Industry, last_month_production,      SLE_FILE_U16 | SLE_VAR_U32,   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
	NSL("last_month_production",         SLE_CONDARR_X(Industry, last_month_production,      SLE_UINT32,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),
	NSL("last_month_transported",          SLE_CONDARR(Industry, last_month_transported,     SLE_FILE_U16 | SLE_VAR_U32,    2, SL_MIN_VERSION,                  SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("last_month_transported",        SLE_CONDARR_X(Industry, last_month_transported,     SLE_FILE_U16 | SLE_VAR_U32,   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 0, 0))),
	NSL("last_month_transported",        SLE_CONDARR_X(Industry, last_month_transported,     SLE_UINT32,                   16, SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_INDUSTRY_CARGO_TOTALS, 1))),

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
	NSL("",                                SLE_CONDVAR(Industry, last_cargo_accepted_at[0],  SLE_INT32,                        SLV_70,                          SLV_EXTEND_INDUSTRY_CARGO_SLOTS)),
	NSL("last_cargo_accepted_at",          SLE_CONDARR(Industry, last_cargo_accepted_at,     SLE_INT32, 16,                    SLV_EXTEND_INDUSTRY_CARGO_SLOTS, SL_MAX_VERSION)),
	NSL("selected_layout",                 SLE_CONDVAR(Industry, selected_layout,            SLE_UINT8,                        SLV_73,                          SL_MAX_VERSION)),
	NSL("exclusive_supplier",              SLE_CONDVAR(Industry, exclusive_supplier,         SLE_UINT8,                        SLV_GS_INDUSTRY_CONTROL,         SL_MAX_VERSION)),
	NSL("exclusive_consumer",              SLE_CONDVAR(Industry, exclusive_consumer,         SLE_UINT8,                        SLV_GS_INDUSTRY_CONTROL,         SL_MAX_VERSION)),

	NSL("",                               SLEG_CONDARR(_old_ind_persistent_storage.storage,  SLE_UINT32, 16,                   SLV_76,                          SLV_161)),
	NSL("psa",                             SLE_CONDREF(Industry, psa,                        REF_STORAGE,                      SLV_161,                         SL_MAX_VERSION)),

	NSL("", SLE_CONDNULL(1, SLV_82, SLV_197)), // random_triggers
	NSL("random",                          SLE_CONDVAR(Industry, random,                     SLE_UINT16,                       SLV_82,                          SL_MAX_VERSION)),
	NSL("text",                           SLE_CONDSSTR(Industry, text,                       SLE_STR | SLF_ALLOW_CONTROL,      SLV_INDUSTRY_TEXT,               SL_MAX_VERSION)),

	NSL("", SLE_CONDNULL(32, SLV_2, SLV_144)), // old reserved space
};

static void Save_INDY()
{
	SaveLoadTableData slt = SlTableHeader(_industry_desc);

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
	SaveLoadTableData slt = SlTableHeaderOrRiff(_industry_desc);

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
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(_industry_desc);

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
