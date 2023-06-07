/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file economy_sl.cpp Code handling saving and loading of economy data */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/economy_sl_compat.h"

#include "../economy_func.h"
#include "../economy_base.h"

#include "../safeguards.h"

namespace upstream_sl {

static const SaveLoad _economy_desc[] = {
	SLE_CONDVAR(Economy, old_max_loan_unround,          SLE_FILE_I32 | SLE_VAR_I64,  SL_MIN_VERSION, SLV_65),
	SLE_CONDVAR(Economy, old_max_loan_unround,          SLE_INT64,                  SLV_65, SLV_126),
	SLE_CONDVAR(Economy, old_max_loan_unround_fract,    SLE_UINT16,                 SLV_70, SLV_126),
	SLE_CONDVAR(Economy, inflation_prices,              SLE_UINT64,                SLV_126, SL_MAX_VERSION),
	SLE_CONDVAR(Economy, inflation_payment,             SLE_UINT64,                SLV_126, SL_MAX_VERSION),
	    SLE_VAR(Economy, fluct,                         SLE_INT16),
	    SLE_VAR(Economy, interest_rate,                 SLE_UINT8),
	    SLE_VAR(Economy, infl_amount,                   SLE_UINT8),
	    SLE_VAR(Economy, infl_amount_pr,                SLE_UINT8),
	SLE_CONDVAR(Economy, industry_daily_change_counter, SLE_UINT32,                SLV_102, SL_MAX_VERSION),
};

/** Economy variables */
struct ECMYChunkHandler : ChunkHandler {
	ECMYChunkHandler() : ChunkHandler('ECMY', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_economy_desc);

		SlSetArrayIndex(0);
		SlObject(&_economy, _economy_desc);
	}


	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_economy_desc, _economy_sl_compat);

		if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() == -1) return;
		SlObject(&_economy, slt);
		if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() != -1) SlErrorCorrupt("Too many ECMY entries");

		StartupIndustryDailyChanges(IsSavegameVersionBefore(SLV_102));  // old savegames will need to be initialized
	}
};

static const SaveLoad _cargopayment_desc[] = {
	    SLE_REF(CargoPayment, front,           REF_VEHICLE),
	    SLE_VAR(CargoPayment, route_profit,    SLE_INT64),
	    SLE_VAR(CargoPayment, visual_profit,   SLE_INT64),
	SLE_CONDVAR(CargoPayment, visual_transfer, SLE_INT64, SLV_181, SL_MAX_VERSION),
};

struct CAPYChunkHandler : ChunkHandler {
	CAPYChunkHandler() : ChunkHandler('CAPY', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_cargopayment_desc);

		for (CargoPayment *cp : CargoPayment::Iterate()) {
			SlSetArrayIndex(cp->index);
			SlObject(cp, _cargopayment_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_cargopayment_desc, _cargopayment_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			CargoPayment *cp = new (index) CargoPayment();
			SlObject(cp, slt);
		}
	}

	void FixPointers() const override
	{
		for (CargoPayment *cp : CargoPayment::Iterate()) {
			SlObject(cp, _cargopayment_desc);
		}
	}
};

static const CAPYChunkHandler CAPY;
static const ECMYChunkHandler ECMY;
static const ChunkHandlerRef economy_chunk_handlers[] = {
	CAPY,
	ECMY,
};

extern const ChunkHandlerTable _economy_chunk_handlers(economy_chunk_handlers);

}
