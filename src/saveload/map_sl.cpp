/* $Id$ */

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

#include "saveload.h"
#include "saveload_buffer.h"

#include "../safeguards.h"

static uint32 _map_dim_x;
static uint32 _map_dim_y;

static const SaveLoadGlobVarList _map_dimensions[] = {
	SLEG_CONDVAR(_map_dim_x, SLE_UINT32, 6, SL_MAX_VERSION),
	SLEG_CONDVAR(_map_dim_y, SLE_UINT32, 6, SL_MAX_VERSION),
	    SLEG_END()
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
	SmallStackSafeStackAlloc<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf, MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].type = buf[j];
	}
}

static void Load_MAPH()
{
	SmallStackSafeStackAlloc<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf, MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].height = buf[j];
	}
}

static void Load_MAP1()
{
	SmallStackSafeStackAlloc<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf, MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m1 = buf[j];
	}
}

static void Load_MAP2()
{
	SmallStackSafeStackAlloc<uint16, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf, MAP_SL_BUF_SIZE,
			/* In those versions the m2 was 8 bits */
			IsSavegameVersionBefore(5) ? SLE_FILE_U8 | SLE_VAR_U16 : SLE_UINT16
		);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m2 = buf[j];
	}
}

static void Load_MAP3()
{
	SmallStackSafeStackAlloc<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf, MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m3 = buf[j];
	}
}

static void Load_MAP4()
{
	SmallStackSafeStackAlloc<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf, MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m4 = buf[j];
	}
}

static void Load_MAP5()
{
	SmallStackSafeStackAlloc<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf, MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _m[i++].m5 = buf[j];
	}
}

static void Load_MAP6()
{
	SmallStackSafeStackAlloc<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	if (IsSavegameVersionBefore(42)) {
		for (TileIndex i = 0; i != size;) {
			/* 1024, otherwise we overflow on 64x64 maps! */
			SlArray(buf, 1024, SLE_UINT8);
			for (uint j = 0; j != 1024; j++) {
				_me[i++].m6 = GB(buf[j], 0, 2);
				_me[i++].m6 = GB(buf[j], 2, 2);
				_me[i++].m6 = GB(buf[j], 4, 2);
				_me[i++].m6 = GB(buf[j], 6, 2);
			}
		}
	} else {
		for (TileIndex i = 0; i != size;) {
			SlArray(buf, MAP_SL_BUF_SIZE, SLE_UINT8);
			for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _me[i++].m6 = buf[j];
		}
	}
}

static void Load_MAP7()
{
	SmallStackSafeStackAlloc<byte, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf, MAP_SL_BUF_SIZE, SLE_UINT8);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _me[i++].m7 = buf[j];
	}
}

static void Load_MAP8()
{
	SmallStackSafeStackAlloc<uint16, MAP_SL_BUF_SIZE> buf;
	TileIndex size = MapSize();

	for (TileIndex i = 0; i != size;) {
		SlArray(buf, MAP_SL_BUF_SIZE, SLE_UINT16);
		for (uint j = 0; j != MAP_SL_BUF_SIZE; j++) _me[i++].m8 = buf[j];
	}
}

static void Load_WMAP()
{
	assert_compile(sizeof(Tile) == 8);
	assert_compile(sizeof(TileExtended) == 4);
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
	assert_compile(sizeof(Tile) == 8);
	assert_compile(sizeof(TileExtended) == 4);
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

extern const ChunkHandler _map_chunk_handlers[] = {
	{ 'MAPS', Save_MAPS, Load_MAPS, NULL, Check_MAPS, CH_RIFF },
	{ 'MAPT', NULL,      Load_MAPT, NULL, NULL,       CH_RIFF },
	{ 'MAPH', NULL,      Load_MAPH, NULL, NULL,       CH_RIFF },
	{ 'MAPO', NULL,      Load_MAP1, NULL, NULL,       CH_RIFF },
	{ 'MAP2', NULL,      Load_MAP2, NULL, NULL,       CH_RIFF },
	{ 'M3LO', NULL,      Load_MAP3, NULL, NULL,       CH_RIFF },
	{ 'M3HI', NULL,      Load_MAP4, NULL, NULL,       CH_RIFF },
	{ 'MAP5', NULL,      Load_MAP5, NULL, NULL,       CH_RIFF },
	{ 'MAPE', NULL,      Load_MAP6, NULL, NULL,       CH_RIFF },
	{ 'MAP7', NULL,      Load_MAP7, NULL, NULL,       CH_RIFF },
	{ 'MAP8', NULL,      Load_MAP8, NULL, NULL,       CH_RIFF },
	{ 'WMAP', Save_WMAP, Load_WMAP, NULL, NULL,       CH_RIFF | CH_LAST },
};
