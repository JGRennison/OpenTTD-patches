/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train_speed_adaptation.cpp Code handling saving and loading of data for train speed adaptation */

#include "../stdafx.h"
#include "../train_speed_adaptation.h"
#include "saveload.h"

using SignalSpeedType = std::pair<const SignalSpeedKey, SignalSpeedValue>;

static const SaveLoad _train_speed_adaptation_map_desc[] = {
	SLE_CONDVAR_X(SignalSpeedType, first.signal_track,           SLE_FILE_U8  | SLE_VAR_U16,    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TRAIN_SPEED_ADAPTATION, 1, 1)),
	SLE_CONDVAR_X(SignalSpeedType, first.signal_track,           SLE_UINT16,                    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TRAIN_SPEED_ADAPTATION, 2)),
	SLE_VAR(SignalSpeedType, first.last_passing_train_dir, SLE_UINT8),
	SLE_VAR(SignalSpeedType, second.train_speed,           SLE_UINT16),
	SLE_VAR(SignalSpeedType, second.time_stamp,            SLE_UINT64),
};

static std::vector<SaveLoad> _filtered_train_speed_adaptation_map_desc = SlFilterObject(_train_speed_adaptation_map_desc);

static void Load_TSAS()
{
	_filtered_train_speed_adaptation_map_desc = SlFilterObject(_train_speed_adaptation_map_desc);
	int index;
	SignalSpeedType data;
	while ((index = SlIterateArray()) != -1) {
		const_cast<SignalSpeedKey &>(data.first).signal_tile = index;
		SlObjectLoadFiltered(&data, _filtered_train_speed_adaptation_map_desc);
		_signal_speeds.insert(data);
	}
	_filtered_train_speed_adaptation_map_desc.clear();
}

static void RealSave_TSAS(SignalSpeedType *data)
{
	SlObjectSaveFiltered(data, _filtered_train_speed_adaptation_map_desc);
}

static void Save_TSAS()
{
	_filtered_train_speed_adaptation_map_desc = SlFilterObject(_train_speed_adaptation_map_desc);
	for (auto &it : _signal_speeds) {
		SlSetArrayIndex(it.first.signal_tile);
		SignalSpeedType *data = &it;
		SlAutolength((AutolengthProc*) RealSave_TSAS, data);
	}
	_filtered_train_speed_adaptation_map_desc.clear();
}

extern const ChunkHandler train_speed_adaptation_chunk_handlers[] = {
	{ 'TSAS', Save_TSAS, Load_TSAS, nullptr, nullptr, CH_SPARSE_ARRAY },
};

extern const ChunkHandlerTable _train_speed_adaptation_chunk_handlers(train_speed_adaptation_chunk_handlers);
