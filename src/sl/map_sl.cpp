/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file map_sl.cpp Code handling saving and loading of map */

#include "../stdafx.h"
#include "../map_func.h"
#include "../core/bitmath_func.hpp"
#include "../core/endian_func.hpp"
#include "../core/endian_type.hpp"
#include "../fios.h"
#include "../load_check.h"
#include <array>

#include "saveload.h"
#include "saveload_buffer.h"

#include "../safeguards.h"

static uint32 _map_dim_x;
static uint32 _map_dim_y;

extern bool _sl_maybe_chillpp;

static const SaveLoad _map_dimensions[] = {
	SLEG_CONDVAR(_map_dim_x, SLE_UINT32, SLV_6, SL_MAX_VERSION),
	SLEG_CONDVAR(_map_dim_y, SLE_UINT32, SLV_6, SL_MAX_VERSION),
};

static void Save_MAPS()
{
	_map_dim_x = MapSizeX();
	_map_dim_y = MapSizeY();
	SlGlobList(_map_dimensions);
}

static void Load_MAPS()
{
	SlGlobList(_map_dimensions);
	if (!ValidateMapSize(_map_dim_x, _map_dim_y)) {
		SlErrorCorruptFmt("Invalid map size: %u x %u", _map_dim_x, _map_dim_y);
	}
	AllocateMap(_map_dim_x, _map_dim_y);
}

static void Check_MAPS()
{
	SlGlobList(_map_dimensions);
	_load_check_data.map_size_x = _map_dim_x;
	_load_check_data.map_size_y = _map_dim_y;
}

static const uint MAP_SL_BUF_SIZE = 4096;

static void Load_MAPT()
{
	std::array<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].type = buf[j];
	}
}

static void Check_MAPH_common()
{
	if (_sl_maybe_chillpp && (SlGetFieldLength() == 0 || SlGetFieldLength() == (size_t)_map_dim_x * (size_t)_map_dim_y * 2)) {
		_sl_maybe_chillpp = false;
		extern void SlXvChillPPSpecialSavegameVersions();
		SlXvChillPPSpecialSavegameVersions();
	}
}

static void Check_MAPH()
{
	Check_MAPH_common();
	SlSkipBytes(SlGetFieldLength());
}

static void Load_MAPH()
{
	Check_MAPH_common();
	if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) {
		if (SlGetFieldLength() != 0) {
			_sl_xv_feature_versions[XSLFI_HEIGHT_8_BIT] = 2;
			std::array<uint16, MAP_SL_BUF_SIZE> buf;
			TileIndex size = MapSize();

			for (TileIndex i = 0; i != size;) {
				SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT16);
				for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].height = buf[j];
			}
		}
		return;
	}

	std::array<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].height = buf[j];
	}
}

static void Load_MAP1()
{
	std::array<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m1 = buf[j];
	}
}

static void Load_MAP2()
{
	std::array<uint16, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf.data(), MAP_SL_BUF_SIZE,
			/* In those versions the m2 was 8 bits */
			IsSavegameVersionBefore(SLV_5) ? SLE_FILE_U8 | SLE_VAR_U16 : SLE_UINT16
		);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m2 = buf[j];
	}
}

static void Load_MAP3()
{
	std::array<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m3 = buf[j];
	}
}

static void Load_MAP4()
{
	std::array<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m4 = buf[j];
	}
}

static void Load_MAP5()
{
	std::array<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m5 = buf[j];
	}
}

static void Load_MAP6()
{
	std::array<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	if (IsSavegameVersionBefore(SLV_42)) {
		for (TileIndex i = 0; i != size;) {
			/* 1024, otherwise we overflow on 64x64 maps! */
			SlArray(buf.data(), 1024, SLE_UINT8);
			for (uint j = 0; j != 1024; j++) {
				_me[i++].m6 = GB(buf[j], 0, 2);
				_me[i++].m6 = GB(buf[j], 2, 2);
				_me[i++].m6 = GB(buf[j], 4, 2);
				_me[i++].m6 = GB(buf[j], 6, 2);
			}
		}
	} else {
		for (TileIndex i = 0; i != size;) {
			SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _me[i++].m6 = buf[j];
		}
	}
}

static void Load_MAP7()
{
	std::array<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _me[i++].m7 = buf[j];
	}
}

static void Load_MAP8()
{
	std::array<uint16, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf.data(), MAP_SL_BUF_SIZE, SLE_UINT16);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _me[i++].m8 = buf[j];
	}
}

static void Load_WMAP()
{
	static_assert(sizeof(Tile) == 8);
	static_assert(sizeof(TileExtended) == 4);
	assert(_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 1 || _sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 2);

	ReadBuffer *reader = ReadBuffer::GetCurrent();
	const TileIndex size = MapSize();

#if TTD_ENDIAN == TTD_LITTLE_ENDIAN
	reader->CopyBytes((byte *) _m, size * 8);
#else
	for (TileIndex i = 0; i != size; i++) {
		reader->CheckBytes(8);
		_m[i].type = reader->RawReadByte();
		_m[i].height = reader->RawReadByte();
		uint16 m2 = reader->RawReadByte();
		m2 |= ((uint16) reader->RawReadByte()) << 8;
		_m[i].m2 = m2;
		_m[i].m1 = reader->RawReadByte();
		_m[i].m3 = reader->RawReadByte();
		_m[i].m4 = reader->RawReadByte();
		_m[i].m5 = reader->RawReadByte();
	}
#endif

	if (_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 1) {
		for (TileIndex i = 0; i != size; i++) {
			reader->CheckBytes(2);
			_me[i].m6 = reader->RawReadByte();
			_me[i].m7 = reader->RawReadByte();
		}
	} else if (_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 2) {
#if TTD_ENDIAN == TTD_LITTLE_ENDIAN
		reader->CopyBytes((byte *) _me, size * 4);
#else
		for (TileIndex i = 0; i != size; i++) {
			reader->CheckBytes(4);
			_me[i].m6 = reader->RawReadByte();
			_me[i].m7 = reader->RawReadByte();
			uint16 m8 = reader->RawReadByte();
			m8 |= ((uint16) reader->RawReadByte()) << 8;
			_me[i].m8 = m8;
		}
#endif
	} else {
		NOT_REACHED();
	}
}

static void Save_WMAP()
{
	static_assert(sizeof(Tile) == 8);
	static_assert(sizeof(TileExtended) == 4);
	assert(_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 2);

	MemoryDumper *dumper = MemoryDumper::GetCurrent();
	const TileIndex size = MapSize();
	SlSetLength(size * 12);

#if TTD_ENDIAN == TTD_LITTLE_ENDIAN
	dumper->CopyBytes((byte *) _m, size * 8);
	dumper->CopyBytes((byte *) _me, size * 4);
#else
	for (TileIndex i = 0; i != size; i++) {
		dumper->CheckBytes(8);
		dumper->RawWriteByte(_m[i].type);
		dumper->RawWriteByte(_m[i].height);
		dumper->RawWriteByte(GB(_m[i].m2, 0, 8));
		dumper->RawWriteByte(GB(_m[i].m2, 8, 8));
		dumper->RawWriteByte(_m[i].m1);
		dumper->RawWriteByte(_m[i].m3);
		dumper->RawWriteByte(_m[i].m4);
		dumper->RawWriteByte(_m[i].m5);
	}
	for (TileIndex i = 0; i != size; i++) {
		dumper->CheckBytes(4);
		dumper->RawWriteByte(_me[i].m6);
		dumper->RawWriteByte(_me[i].m7);
		dumper->RawWriteByte(GB(_me[i].m8, 0, 8));
		dumper->RawWriteByte(GB(_me[i].m8, 8, 8));
	}
#endif
}

struct MAPT {
	typedef uint8 FieldT;
	static const FieldT &GetField(TileIndex t) { return _m[t].type; }
};

struct MAPH {
	typedef uint8 FieldT;
	static const FieldT &GetField(TileIndex t) { return _m[t].height; }
};

struct MAP1 {
	typedef uint8 FieldT;
	static const FieldT &GetField(TileIndex t) { return _m[t].m1; }
};

struct MAP2 {
	typedef uint16 FieldT;
	static const FieldT &GetField(TileIndex t) { return _m[t].m2; }
};

struct MAP3 {
	typedef uint8 FieldT;
	static const FieldT &GetField(TileIndex t) { return _m[t].m3; }
};

struct MAP4 {
	typedef uint8 FieldT;
	static const FieldT &GetField(TileIndex t) { return _m[t].m4; }
};

struct MAP5 {
	typedef uint8 FieldT;
	static const FieldT &GetField(TileIndex t) { return _m[t].m5; }
};

struct MAP6 {
	typedef uint8 FieldT;
	static const FieldT &GetField(TileIndex t) { return _me[t].m6; }
};

struct MAP7 {
	typedef uint8 FieldT;
	static const FieldT &GetField(TileIndex t) { return _me[t].m7; }
};

struct MAP8 {
	typedef uint16 FieldT;
	static const FieldT &GetField(TileIndex t) { return _me[t].m8; }
};

template <typename T>
struct MAP_VarType {};

template <>
struct MAP_VarType<uint8>
{
	static const VarType var_type = SLE_UINT8;
};

template <>
struct MAP_VarType<uint16>
{
	static const VarType var_type = SLE_UINT16;
};

template <typename T>
static void Save_MAP()
{
	assert(_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 0);

	std::array<typename T::FieldT, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	SlSetLength(size * sizeof(typename T::FieldT));
	for (TileIndex i = 0; i != size;) {
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) buf[j] = T::GetField(i++);
		SlArray(buf.data(), MAP_SL_BUF_SIZE, MAP_VarType<typename T::FieldT>::var_type);
	}
}

static ChunkSaveLoadSpecialOpResult Special_WMAP(uint32 chunk_id, ChunkSaveLoadSpecialOp op)
{
	switch (op) {
		case CSLSO_SHOULD_SAVE_CHUNK:
			if (_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 0) return CSLSOR_DONT_SAVE_CHUNK;
			break;

		default:
			break;
	}
	return CSLSOR_NONE;
}

static ChunkSaveLoadSpecialOpResult Special_MAP_Chunks(uint32 chunk_id, ChunkSaveLoadSpecialOp op)
{
	switch (op) {
		case CSLSO_SHOULD_SAVE_CHUNK:
			if (_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] != 0) return CSLSOR_DONT_SAVE_CHUNK;
			break;

		default:
			break;
	}
	return CSLSOR_NONE;
}

static const ChunkHandler map_chunk_handlers[] = {
	{ 'MAPS', Save_MAPS,      Load_MAPS, nullptr, Check_MAPS, CH_RIFF },
	{ 'MAPT', Save_MAP<MAPT>, Load_MAPT, nullptr, nullptr,    CH_RIFF, Special_MAP_Chunks },
	{ 'MAPH', Save_MAP<MAPH>, Load_MAPH, nullptr, Check_MAPH, CH_RIFF, Special_MAP_Chunks },
	{ 'MAPO', Save_MAP<MAP1>, Load_MAP1, nullptr, nullptr,    CH_RIFF, Special_MAP_Chunks },
	{ 'MAP2', Save_MAP<MAP2>, Load_MAP2, nullptr, nullptr,    CH_RIFF, Special_MAP_Chunks },
	{ 'M3LO', Save_MAP<MAP3>, Load_MAP3, nullptr, nullptr,    CH_RIFF, Special_MAP_Chunks },
	{ 'M3HI', Save_MAP<MAP4>, Load_MAP4, nullptr, nullptr,    CH_RIFF, Special_MAP_Chunks },
	{ 'MAP5', Save_MAP<MAP5>, Load_MAP5, nullptr, nullptr,    CH_RIFF, Special_MAP_Chunks },
	{ 'MAPE', Save_MAP<MAP6>, Load_MAP6, nullptr, nullptr,    CH_RIFF, Special_MAP_Chunks },
	{ 'MAP7', Save_MAP<MAP7>, Load_MAP7, nullptr, nullptr,    CH_RIFF, Special_MAP_Chunks },
	{ 'MAP8', Save_MAP<MAP8>, Load_MAP8, nullptr, nullptr,    CH_RIFF, Special_MAP_Chunks },
	{ 'WMAP', Save_WMAP,      Load_WMAP, nullptr, nullptr,    CH_RIFF, Special_WMAP },
};

extern const ChunkHandlerTable _map_chunk_handlers(map_chunk_handlers);
