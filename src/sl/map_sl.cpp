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

static uint32_t _map_dim_x;
static uint32_t _map_dim_y;

extern bool _sl_maybe_chillpp;

static const NamedSaveLoad _map_dimensions[] = {
	NSL("dim_x", SLEG_CONDVAR(_map_dim_x, SLE_UINT32, SLV_6, SL_MAX_VERSION)),
	NSL("dim_y", SLEG_CONDVAR(_map_dim_y, SLE_UINT32, SLV_6, SL_MAX_VERSION)),
};

static void Save_MAPS()
{
	_map_dim_x = MapSizeX();
	_map_dim_y = MapSizeY();
	SlSaveTableObjectChunk(_map_dimensions);
}

static void Load_MAPS()
{
	SlLoadTableOrRiffFiltered(_map_dimensions);
	if (!ValidateMapSize(_map_dim_x, _map_dim_y)) {
		SlErrorCorruptFmt("Invalid map size: %u x %u", _map_dim_x, _map_dim_y);
	}
	AllocateMap(_map_dim_x, _map_dim_y);
}

static void Check_MAPS()
{
	SlLoadTableOrRiffFiltered(_map_dimensions);
	_load_check_data.map_size_x = _map_dim_x;
	_load_check_data.map_size_y = _map_dim_y;
}

static void Load_MAPT()
{
	Tile *m = _m;
	ReadBuffer::GetCurrent()->ReadBytesToHandler(MapSize(), [&](uint8_t val) {
		m->type = val;
		m++;
	});
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

			Tile *m = _m;
			ReadBuffer::GetCurrent()->ReadUint16sToHandler(MapSize(), [&](uint16_t val) {
				m->height = val;
				m++;
			});
		}
		return;
	}

	Tile *m = _m;
	ReadBuffer::GetCurrent()->ReadBytesToHandler(MapSize(), [&](uint8_t val) {
		m->height = val;
		m++;
	});
}

static void Load_MAP1()
{
	Tile *m = _m;
	ReadBuffer::GetCurrent()->ReadBytesToHandler(MapSize(), [&](uint8_t val) {
		m->m1 = val;
		m++;
	});
}

static void Load_MAP2()
{
	Tile *m = _m;
	if (IsSavegameVersionBefore(SLV_5)) {
		/* In those versions the m2 was 8 bits */
		ReadBuffer::GetCurrent()->ReadBytesToHandler(MapSize(), [&](uint8_t val) {
			m->m2 = val;
			m++;
		});
	} else {
		ReadBuffer::GetCurrent()->ReadUint16sToHandler(MapSize(), [&](uint16_t val) {
			m->m2 = val;
			m++;
		});
	}
}

static void Load_MAP3()
{
	Tile *m = _m;
	ReadBuffer::GetCurrent()->ReadBytesToHandler(MapSize(), [&](uint8_t val) {
		m->m3 = val;
		m++;
	});
}

static void Load_MAP4()
{
	Tile *m = _m;
	ReadBuffer::GetCurrent()->ReadBytesToHandler(MapSize(), [&](uint8_t val) {
		m->m4 = val;
		m++;
	});
}

static void Load_MAP5()
{
	Tile *m = _m;
	ReadBuffer::GetCurrent()->ReadBytesToHandler(MapSize(), [&](uint8_t val) {
		m->m5 = val;
		m++;
	});
}

static void Load_MAP6()
{
	TileIndex size = MapSize();

	TileExtended *me = _me;
	if (IsSavegameVersionBefore(SLV_42)) {
		ReadBuffer::GetCurrent()->ReadBytesToHandler(size / 4, [&](uint8_t val) {
			me[0].m6 = GB(val, 0, 2);
			me[1].m6 = GB(val, 2, 2);
			me[2].m6 = GB(val, 4, 2);
			me[3].m6 = GB(val, 6, 2);
			me += 4;
		});
	} else {
		ReadBuffer::GetCurrent()->ReadBytesToHandler(size, [&](uint8_t val) {
			me->m6 = val;
			me++;
		});
	}
}

static void Load_MAP7()
{
	TileExtended *me = _me;
	ReadBuffer::GetCurrent()->ReadBytesToHandler(MapSize(), [&](uint8_t val) {
		me->m7 = val;
		me++;
	});
}

static void Load_MAP8()
{
	TileExtended *me = _me;
	ReadBuffer::GetCurrent()->ReadUint16sToHandler(MapSize(), [&](uint16_t val) {
		me->m8 = val;
		me++;
	});
}

static void Load_WMAP()
{
	static_assert(sizeof(Tile) == 8);
	static_assert(sizeof(TileExtended) == 4);
	assert(_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 1 || _sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 2);

	ReadBuffer *reader = ReadBuffer::GetCurrent();
	const TileIndex size = MapSize();

#if TTD_ENDIAN == TTD_LITTLE_ENDIAN
	reader->CopyBytes((uint8_t *) _m, size * 8);
#else
	Tile *m_start = _m;
	Tile *m_end = _m + size;
	for (Tile *m = m_start; m != m_end; m++) {
		RawReadBuffer buf = reader->ReadRawBytes(8);
		m->type = buf.RawReadByte();
		m->height = buf.RawReadByte();
		uint16_t m2 = buf.RawReadByte();
		m2 |= ((uint16_t) buf.RawReadByte()) << 8;
		m->m2 = m2;
		m->m1 = buf.RawReadByte();
		m->m3 = buf.RawReadByte();
		m->m4 = buf.RawReadByte();
		m->m5 = buf.RawReadByte();
	}
#endif

	TileExtended *me_start = _me;
	TileExtended *me_end = _me + size;
	if (_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 1) {
		for (TileExtended *me = me_start; me != me_end; me++) {
			RawReadBuffer buf = reader->ReadRawBytes(2);
			me->m6 = buf.RawReadByte();
			me->m7 = buf.RawReadByte();
		}
	} else if (_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 2) {
#if TTD_ENDIAN == TTD_LITTLE_ENDIAN
		reader->CopyBytes((uint8_t *) _me, size * 4);
#else
		for (TileExtended *me = me_start; me != me_end; me++) {
			RawReadBuffer buf = reader->ReadRawBytes(4);
			me->m6 = buf.RawReadByte();
			me->m7 = buf.RawReadByte();
			uint16_t m8 = buf.RawReadByte();
			m8 |= ((uint16_t) buf.RawReadByte()) << 8;
			me->m8 = m8;
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
	dumper->CopyBytes((uint8_t *) _m, size * 8);
	dumper->CopyBytes((uint8_t *) _me, size * 4);
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

struct MapTileReader {
	Tile *m;

	MapTileReader() { this->m = _m; }
	Tile *Next() { return this->m++; }
};

struct MapTileExtendedReader {
	TileExtended *me;

	MapTileExtendedReader() { this->me = _me; }
	TileExtended *Next() { return this->me++; }
};

struct MAPT : MapTileReader {
	typedef uint8_t FieldT;
	FieldT GetNextField() { return this->Next()->type; }
};

struct MAPH : MapTileReader {
	typedef uint8_t FieldT;
	FieldT GetNextField() { return this->Next()->height; }
};

struct MAP1 : MapTileReader {
	typedef uint8_t FieldT;
	FieldT GetNextField() { return this->Next()->m1; }
};

struct MAP2 : MapTileReader {
	typedef uint16_t FieldT;
	FieldT GetNextField() { return this->Next()->m2; }
};

struct MAP3 : MapTileReader {
	typedef uint8_t FieldT;
	FieldT GetNextField() { return this->Next()->m3; }
};

struct MAP4 : MapTileReader {
	typedef uint8_t FieldT;
	FieldT GetNextField() { return this->Next()->m4; }
};

struct MAP5 : MapTileReader {
	typedef uint8_t FieldT;
	FieldT GetNextField() { return this->Next()->m5; }
};

struct MAP6 : MapTileExtendedReader {
	typedef uint8_t FieldT;
	FieldT GetNextField() { return this->Next()->m6; }
};

struct MAP7 : MapTileExtendedReader {
	typedef uint8_t FieldT;
	FieldT GetNextField() { return this->Next()->m7; }
};

struct MAP8 : MapTileExtendedReader {
	typedef uint16_t FieldT;
	FieldT GetNextField() { return this->Next()->m8; }
};

template <typename T>
static void Save_MAP()
{
	assert(_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] == 0);

	static_assert(std::is_same_v<typename T::FieldT, uint8_t> || std::is_same_v<typename T::FieldT, uint16_t>);

	TileIndex size = MapSize();
	SlSetLength(size * sizeof(typename T::FieldT));

	T map_reader{};
	if constexpr (std::is_same_v<typename T::FieldT, uint8_t>) {
		MemoryDumper::GetCurrent()->WriteBytesFromHandler(size, [&]() -> uint8_t {
			return map_reader.GetNextField();
		});
	} else {
		MemoryDumper::GetCurrent()->WriteUint16sFromHandler(size, [&]() -> uint16_t {
			return map_reader.GetNextField();
		});
	}
}

static ChunkSaveLoadSpecialOpResult Special_WMAP(uint32_t chunk_id, ChunkSaveLoadSpecialOp op)
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

static ChunkSaveLoadSpecialOpResult Special_MAP_Chunks(uint32_t chunk_id, ChunkSaveLoadSpecialOp op)
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
	{ 'MAPS', Save_MAPS,      Load_MAPS, nullptr, Check_MAPS, CH_TABLE },
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
