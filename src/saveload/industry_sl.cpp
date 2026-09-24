/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file industry_sl.cpp Code handling saving and loading of industries. */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/industry_sl_compat.h"

#include "../industry.h"
#include "newgrf_sl.h"

#include "../safeguards.h"

extern OldIndustryAccepted _old_industry_accepted;
extern OldIndustryProduced _old_industry_produced;
extern void LoadMoveOldAcceptsProduced(Industry *i);
extern void LoadSetIndustryHistoryValidMask(Industry *i, bool extended_history);

namespace upstream_sl {

class SlIndustryAcceptedHistory : public DefaultSaveLoadHandler<SlIndustryAcceptedHistory, Industry::AcceptedCargo> {
public:
	static inline const SaveLoad description[] = {
		 SLE_VAR(Industry::AcceptedHistory, accepted, VarFileType::U16 | VarMemType::U32),
		 SLE_VAR(Industry::AcceptedHistory, waiting, VarTypes::U16),
	};
	static inline const SaveLoadCompatTable compat_description = {};

	void Save(Industry::AcceptedCargo *a) const override
	{
		NOT_REACHED();
	}

	void Load(Industry::AcceptedCargo *a) const override
	{
		size_t len = SlGetStructListLength(UINT32_MAX);
		if (len == 0) return;

		auto &history = a->GetOrCreateHistory();

		if (len > history.size()) {
			/* Truncate larger history */
			for (auto &h : history) {
				SlObject(&h, this->GetLoadDescription());
			}
			Industry::AcceptedHistory tmp{};
			for (size_t i = 0; i < len - history.size(); i++) {
				SlObject(&tmp, this->GetDescription());
			}
			return;
		}

		for (auto &h : history) {
			if (--len > history.size()) break; // unsigned so wraps after hitting zero.
			SlObject(&h, this->GetLoadDescription());
		}
	}
};

class SlIndustryAccepted : public DefaultSaveLoadHandler<SlIndustryAccepted, Industry> {
public:
	static inline const SaveLoad description[] = {
		 SLE_VAR(Industry::AcceptedCargo, cargo, VarTypes::U8),
		 SLE_VAR(Industry::AcceptedCargo, waiting, VarTypes::U16),
		 SLE_VAR(Industry::AcceptedCargo, last_accepted, VarTypes::I32),
		SLE_CONDVAR(Industry::AcceptedCargo, accumulated_waiting, VarTypes::U32, SaveLoadVersion::IndustryAcceptedHistory, SaveLoadVersion::MaxVersion),
		SLEG_CONDSTRUCTLIST("history", SlIndustryAcceptedHistory, SaveLoadVersion::IndustryAcceptedHistory, SaveLoadVersion::MaxVersion),
	};
	static inline const SaveLoadCompatTable compat_description = {};

	void Save(Industry *i) const override
	{
		NOT_REACHED();
	}

	void Load(Industry *i) const override
	{
		size_t len = SlGetStructListLength(INDUSTRY_NUM_INPUTS);

		i->accepted_cargo_count = static_cast<uint8_t>(len);
		i->accepted = std::make_unique<Industry::AcceptedCargo[]>(i->accepted_cargo_count);

		for (size_t j = 0; j < len; j++) {
			Industry::AcceptedCargo &a = i->accepted[j];
			SlObject(&a, this->GetLoadDescription());
		}
	}
};

class SlIndustryProducedHistory : public DefaultSaveLoadHandler<SlIndustryProducedHistory, Industry::ProducedCargo> {
public:
	static inline const SaveLoad description[] = {
		 SLE_VAR(Industry::ProducedHistory, production, VarFileType::U16 | VarMemType::U32),
		 SLE_VAR(Industry::ProducedHistory, transported, VarFileType::U16 | VarMemType::U32),
	};
	static inline const SaveLoadCompatTable compat_description = {};

	void Save(Industry::ProducedCargo *p) const override
	{
		NOT_REACHED();
	}

	void Load(Industry::ProducedCargo *p) const override
	{
		size_t len = SlGetStructListLength(UINT32_MAX);
		if (len > p->history.size()) {
			/* Truncate larger history */
			for (auto &h : p->history) {
				SlObject(&h, this->GetLoadDescription());
			}
			Industry::ProducedHistory tmp{};
			for (size_t i = 0; i < len - p->history.size(); i++) {
				SlObject(&tmp, this->GetDescription());
			}
			return;
		}

		for (auto &h : p->history) {
			if (--len > p->history.size()) break; // unsigned so wraps after hitting zero.
			SlObject(&h, this->GetLoadDescription());
		}
	}
};

class SlIndustryProduced : public DefaultSaveLoadHandler<SlIndustryProduced, Industry> {
public:
	static inline const SaveLoad description[] = {
		 SLE_VAR(Industry::ProducedCargo, cargo, VarTypes::U8),
		 SLE_VAR(Industry::ProducedCargo, waiting, VarTypes::U16),
		 SLE_VAR(Industry::ProducedCargo, rate, VarTypes::U8),
		SLEG_STRUCTLIST("history", SlIndustryProducedHistory),
	};
	static inline const SaveLoadCompatTable compat_description = {};

	void Save(Industry *i) const override
	{
		NOT_REACHED();
	}

	void Load(Industry *i) const override
	{
		size_t len = SlGetStructListLength(INDUSTRY_NUM_OUTPUTS);

		i->produced_cargo_count = static_cast<uint8_t>(len);
		i->produced = std::make_unique<Industry::ProducedCargo[]>(i->produced_cargo_count);

		for (size_t j = 0; j < len; j++) {
			Industry::ProducedCargo &p = i->produced[j];
			SlObject(&p, this->GetLoadDescription());
		}
	}
};

static OldPersistentStorage _old_ind_persistent_storage;

static const SaveLoad _industry_desc[] = {
	SLE_CONDVAR(Industry, location.tile, VarFileType::U16 | VarMemType::U32, SaveLoadVersion::MinVersion, SaveLoadVersion::MultipleRoadStops),
	SLE_CONDVAR(Industry, location.tile, VarTypes::U32, SaveLoadVersion::MultipleRoadStops, SaveLoadVersion::MaxVersion),
	    SLE_VAR(Industry, location.w,                 VarFileType::U8 | VarMemType::U16),
	    SLE_VAR(Industry, location.h,                 VarFileType::U8 | VarMemType::U16),
	    SLE_REF(Industry, town,                       SLRefType::Town),
	SLE_CONDREF(Industry, neutral_station, SLRefType::Station, SaveLoadVersion::ServeNeutralIndustries, SaveLoadVersion::MaxVersion),
	SLEG_CONDARR("produced_cargo", _old_industry_produced.old_cargo, VarTypes::U8, INDUSTRY_ORIGINAL_NUM_OUTPUTS, SaveLoadVersion::StoreIndustryCargo, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("produced_cargo", _old_industry_produced.old_cargo, VarTypes::U8, INDUSTRY_NUM_OUTPUTS, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),
	SLEG_CONDARR("incoming_cargo_waiting", _old_industry_accepted.old_waiting, VarTypes::U16, INDUSTRY_ORIGINAL_NUM_INPUTS, SaveLoadVersion::CargoPaymentOverflow, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("incoming_cargo_waiting", _old_industry_accepted.old_waiting, VarTypes::U16, INDUSTRY_NUM_INPUTS, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),
	SLEG_CONDARR("produced_cargo_waiting", _old_industry_produced.old_waiting, VarTypes::U16, INDUSTRY_ORIGINAL_NUM_OUTPUTS, SaveLoadVersion::MinVersion, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("produced_cargo_waiting", _old_industry_produced.old_waiting, VarTypes::U16, INDUSTRY_NUM_OUTPUTS, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),
	SLEG_CONDARR("production_rate", _old_industry_produced.old_rate, VarTypes::U8, INDUSTRY_ORIGINAL_NUM_OUTPUTS, SaveLoadVersion::MinVersion, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("production_rate", _old_industry_produced.old_rate, VarTypes::U8, INDUSTRY_NUM_OUTPUTS, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),
	SLEG_CONDARR("accepts_cargo", _old_industry_accepted.old_cargo, VarTypes::U8, INDUSTRY_ORIGINAL_NUM_INPUTS, SaveLoadVersion::StoreIndustryCargo, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("accepts_cargo", _old_industry_accepted.old_cargo, VarTypes::U8, INDUSTRY_NUM_INPUTS, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),
	    SLE_VAR(Industry, prod_level,                 VarTypes::U8),
	SLEG_CONDARR("this_month_production", _old_industry_produced.old_this_month_production, VarFileType::U16 | VarMemType::U32, INDUSTRY_ORIGINAL_NUM_OUTPUTS, SaveLoadVersion::MinVersion, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("this_month_production", _old_industry_produced.old_this_month_production, VarFileType::U16 | VarMemType::U32, INDUSTRY_NUM_OUTPUTS, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),
	SLEG_CONDARR("this_month_transported", _old_industry_produced.old_this_month_transported, VarFileType::U16 | VarMemType::U32, INDUSTRY_ORIGINAL_NUM_OUTPUTS, SaveLoadVersion::MinVersion, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("this_month_transported", _old_industry_produced.old_this_month_transported, VarFileType::U16 | VarMemType::U32, INDUSTRY_NUM_OUTPUTS, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),
	SLEG_CONDARR("last_month_production", _old_industry_produced.old_last_month_production, VarFileType::U16 | VarMemType::U32, INDUSTRY_ORIGINAL_NUM_OUTPUTS, SaveLoadVersion::MinVersion, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("last_month_production", _old_industry_produced.old_last_month_production, VarFileType::U16 | VarMemType::U32, INDUSTRY_NUM_OUTPUTS, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),
	SLEG_CONDARR("last_month_transported", _old_industry_produced.old_last_month_transported, VarFileType::U16 | VarMemType::U32, INDUSTRY_ORIGINAL_NUM_OUTPUTS, SaveLoadVersion::MinVersion, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("last_month_transported", _old_industry_produced.old_last_month_transported, VarFileType::U16 | VarMemType::U32, INDUSTRY_NUM_OUTPUTS, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),

	    SLE_VAR(Industry, counter,                    VarTypes::U16),

	    SLE_VAR(Industry, type,                       VarTypes::U8),
	    SLE_VAR(Industry, owner,                      VarTypes::U8),
	    SLE_VAR(Industry, random_colour,              VarTypes::U8),
	SLE_CONDVAR(Industry, last_prod_year, VarFileType::U8 | VarMemType::I32, SaveLoadVersion::MinVersion, SaveLoadVersion::BigDates),
	SLE_CONDVAR(Industry, last_prod_year, VarTypes::I32, SaveLoadVersion::BigDates, SaveLoadVersion::MaxVersion),
	    SLE_VAR(Industry, was_cargo_delivered,        VarTypes::U8),
	SLE_CONDVAR(Industry, ctlflags, VarTypes::U8, SaveLoadVersion::GSIndustryControl, SaveLoadVersion::MaxVersion),

	SLE_CONDVAR(Industry, founder, VarTypes::U8, SaveLoadVersion::CargoPaymentOverflow, SaveLoadVersion::MaxVersion),
	SLE_CONDVAR(Industry, construction_date, VarTypes::I32, SaveLoadVersion::CargoPaymentOverflow, SaveLoadVersion::MaxVersion),
	SLE_CONDVAR(Industry, construction_type, VarTypes::U8, SaveLoadVersion::CargoPaymentOverflow, SaveLoadVersion::MaxVersion),
	SLEG_CONDVAR("last_cargo_accepted_at[0]", _old_industry_accepted.old_last_accepted[0], VarTypes::I32, SaveLoadVersion::CargoPaymentOverflow, SaveLoadVersion::ExtendIndustryCargoSlots),
	SLEG_CONDARR("last_cargo_accepted_at", _old_industry_accepted.old_last_accepted, VarTypes::I32, 16, SaveLoadVersion::ExtendIndustryCargoSlots, SaveLoadVersion::IndustryCargoReorganise),
	SLE_CONDVAR(Industry, selected_layout, VarTypes::U8, SaveLoadVersion::NewGRFIndustryLayout, SaveLoadVersion::MaxVersion),
	SLE_CONDVAR(Industry, exclusive_supplier, VarTypes::U8, SaveLoadVersion::GSIndustryControl, SaveLoadVersion::MaxVersion),
	SLE_CONDVAR(Industry, exclusive_consumer, VarTypes::U8, SaveLoadVersion::GSIndustryControl, SaveLoadVersion::MaxVersion),

	SLEG_CONDARR("storage", _old_ind_persistent_storage.storage, VarFileType::U32 | VarMemType::I32, 16, SaveLoadVersion::NewGRFPersistentStorage, SaveLoadVersion::PersistentStoragePool),
	SLE_CONDREF(Industry, psa, SLRefType::Storage, SaveLoadVersion::PersistentStoragePool, SaveLoadVersion::MaxVersion),

	SLE_CONDVAR(Industry, random, VarTypes::U16, SaveLoadVersion::NewGRFIndustryRandomTriggers, SaveLoadVersion::MaxVersion),
	SLE_CONDSSTR(Industry, text, VarTypes::STR | StringValidationSetting::AllowControlCode, SaveLoadVersion::IndustryText, SaveLoadVersion::MaxVersion),

	SLE_CONDVAR(Industry, valid_history, VarTypes::U64, SaveLoadVersion::IndustryNumValidHistory, SaveLoadVersion::MaxVersion),

	SLEG_CONDSTRUCTLIST("accepted", SlIndustryAccepted, SaveLoadVersion::IndustryCargoReorganise, SaveLoadVersion::MaxVersion),
	SLEG_CONDSTRUCTLIST("produced", SlIndustryProduced, SaveLoadVersion::IndustryCargoReorganise, SaveLoadVersion::MaxVersion),
};

struct INDYChunkHandler : ChunkHandler {
	INDYChunkHandler() : ChunkHandler("INDY", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_industry_desc);

		/* Write the industries */
		for (Industry *ind : Industry::Iterate()) {
			SlSetArrayIndex(ind->index);
			SlObject(ind, _industry_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_industry_desc, _industry_sl_compat);

		int index;

		_old_industry_accepted.Reset();
		_old_industry_produced.Reset();

		while ((index = SlIterateArray()) != -1) {
			Industry *i = Industry::CreateAtIndex(IndustryID(index));
			SlObject(i, slt);

			/* Before savegame version 161, persistent storages were not stored in a pool. */
			if (IsSavegameVersionBefore(SaveLoadVersion::PersistentStoragePool) && !IsSavegameVersionBefore(SaveLoadVersion::NewGRFPersistentStorage)) {
				/* Store the old persistent storage. The GRFID will be added later. */
				assert(PersistentStorage::CanAllocateItem());
				i->psa = PersistentStorage::Create(GrfID{}, GrfSpecFeature::Invalid, TileIndex{});
				std::copy(std::begin(_old_ind_persistent_storage.storage), std::end(_old_ind_persistent_storage.storage), std::begin(i->psa->storage));
			}
			if (IsSavegameVersionBefore(SaveLoadVersion::IndustryCargoReorganise)) {
				LoadMoveOldAcceptsProduced(i);
			}

			if (IsSavegameVersionBefore(SaveLoadVersion::IndustryNumValidHistory)) {
				LoadSetIndustryHistoryValidMask(i, !IsSavegameVersionBefore(SaveLoadVersion::ProductionHistory));
			}
		}
	}

	void FixPointers() const override
	{
		for (Industry *i : Industry::Iterate()) {
			SlObject(i, _industry_desc);
		}
	}
};

struct IIDSChunkHandler : NewGRFMappingChunkHandler {
	IIDSChunkHandler() : NewGRFMappingChunkHandler("IIDS", _industry_mngr) {}
};

struct TIDSChunkHandler : NewGRFMappingChunkHandler {
	TIDSChunkHandler() : NewGRFMappingChunkHandler("TIDS", _industile_mngr) {}
};

/** Description of the data to save and load in #IndustryBuildData. */
static const SaveLoad _industry_builder_desc[] = {
	SLEG_VAR("wanted_inds", _industry_builder.wanted_inds, VarTypes::U32),
};

/** Industry builder. */
struct IBLDChunkHandler : ChunkHandler {
	IBLDChunkHandler() : ChunkHandler("IBLD", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_industry_builder_desc);

		SlSetArrayIndex(0);
		SlGlobList(_industry_builder_desc);
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_industry_builder_desc, _industry_builder_sl_compat);

		if (!IsSavegameVersionBefore(SaveLoadVersion::RiffToArray) && SlIterateArray() == -1) return;
		SlGlobList(slt);
		if (!IsSavegameVersionBefore(SaveLoadVersion::RiffToArray) && SlIterateArray() != -1) SlErrorCorrupt("Too many IBLD entries");
	}
};

/** Description of the data to save and load in #IndustryTypeBuildData. */
static const SaveLoad _industrytype_builder_desc[] = {
	SLE_VAR(IndustryTypeBuildData, probability,  VarTypes::U32),
	SLE_VAR(IndustryTypeBuildData, min_number,   VarTypes::U8),
	SLE_VAR(IndustryTypeBuildData, target_count, VarTypes::U16),
	SLE_VAR(IndustryTypeBuildData, max_wait,     VarTypes::U16),
	SLE_VAR(IndustryTypeBuildData, wait_count,   VarTypes::U16),
};

/** Industry-type build data. */
struct ITBLChunkHandler : ChunkHandler {
	ITBLChunkHandler() : ChunkHandler("ITBL", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_industrytype_builder_desc);

		for (int i = 0; i < NUM_INDUSTRYTYPES; i++) {
			SlSetArrayIndex(i);
			SlObject(_industry_builder.builddata + i, _industrytype_builder_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_industrytype_builder_desc, _industrytype_builder_sl_compat);

		for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
			_industry_builder.builddata[it].Reset();
		}
		int index;
		while ((index = SlIterateArray()) != -1) {
			if ((uint)index >= NUM_INDUSTRYTYPES) SlErrorCorrupt("Too many industry builder datas");
			SlObject(_industry_builder.builddata + index, slt);
		}
	}
};

static const INDYChunkHandler INDY;
static const IIDSChunkHandler IIDS;
static const TIDSChunkHandler TIDS;
static const IBLDChunkHandler IBLD;
static const ITBLChunkHandler ITBL;
static const ChunkHandlerRef industry_chunk_handlers[] = {
	INDY,
	IIDS,
	TIDS,
	IBLD,
	ITBL,
};

extern const ChunkHandlerTable _industry_chunk_handlers(industry_chunk_handlers);

}
