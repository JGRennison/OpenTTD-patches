/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tile_type.h Types related to tiles. */

#ifndef TILE_TYPE_H
#define TILE_TYPE_H

#include "core/strong_typedef_type.hpp"

static const uint TILE_SIZE           = 16;                    ///< Tile size in world coordinates.
static const uint TILE_UNIT_MASK      = TILE_SIZE - 1;         ///< For masking in/out the inner-tile world coordinate units.
static const uint TILE_PIXELS         = 32;                    ///< Pixel distance between tile columns/rows in #ZOOM_BASE.
static const uint TILE_HEIGHT         =  8;                    ///< Height of a height level in world coordinate AND in pixels in #ZOOM_BASE.

static const uint MAX_BUILDING_PIXELS = 200;                   ///< Maximum height of a building in pixels in #ZOOM_BASE. (Also applies to "bridge buildings" on the bridge floor.)
static const int MAX_VEHICLE_PIXEL_X  = 192;                   ///< Maximum width of a vehicle in pixels in #ZOOM_BASE.
static const int MAX_VEHICLE_PIXEL_Y  = 96;                    ///< Maximum height of a vehicle in pixels in #ZOOM_BASE.

static const uint MAX_TILE_HEIGHT     = 255;                   ///< Maximum allowed tile height

static const uint MIN_HEIGHTMAP_HEIGHT = 1;                    ///< Lowest possible peak value for heightmap creation
static const uint MIN_CUSTOM_TERRAIN_TYPE = 1;                 ///< Lowest possible peak value for world generation

static const uint MIN_MAP_HEIGHT_LIMIT = 15;                   ///< Lower bound of maximum allowed heightlevel (in the construction settings)
static const uint MAX_MAP_HEIGHT_LIMIT = MAX_TILE_HEIGHT;      ///< Upper bound of maximum allowed heightlevel (in the construction settings)

static const uint MIN_SNOWLINE_HEIGHT = 2;                     ///< Minimum snowline height
static const uint DEF_SNOWLINE_HEIGHT = 10;                    ///< Default snowline height
static const uint MAX_SNOWLINE_HEIGHT = (MAX_TILE_HEIGHT - 2); ///< Maximum allowed snowline height

static const uint MIN_RAINFOREST_HEIGHT = 1;                   ///< Minimum rainforest height
static const uint DEF_RAINFOREST_HEIGHT = 8;                   ///< Default rainforest height
static const uint MAX_RAINFOREST_HEIGHT = 255;                 ///< Maximum rainforest height

static const uint DEF_SNOW_COVERAGE = 40;                      ///< Default snow coverage.
static const uint DEF_DESERT_COVERAGE = 50;                    ///< Default desert coverage.


/**
 * The different types of tiles.
 *
 * Each tile belongs to one type, according whatever is build on it.
 *
 * @note A railway with a crossing street is marked as MP_ROAD.
 */
enum TileType {
	MP_CLEAR,               ///< A tile without any structures, i.e. grass, rocks, farm fields etc.
	MP_RAILWAY,             ///< A railway
	MP_ROAD,                ///< A tile with road (or tram tracks)
	MP_HOUSE,               ///< A house by a town
	MP_TREES,               ///< Tile got trees
	MP_STATION,             ///< A tile of a station
	MP_WATER,               ///< Water tile
	MP_VOID,                ///< Invisible tiles at the SW and SE border
	MP_INDUSTRY,            ///< Part of an industry
	MP_TUNNELBRIDGE,        ///< Tunnel entry/exit and bridge heads
	MP_OBJECT,              ///< Contains objects such as transmitters and owned land
};

/**
 * Additional infos of a tile on a tropic game.
 *
 * The tropiczone is not modified during gameplay. It mainly affects tree growth. (desert tiles are visible though)
 *
 * In randomly generated maps:
 *  TROPICZONE_DESERT: Generated everywhere, if there is neither water nor mountains (TileHeight >= 4) in a certain distance from the tile.
 *  TROPICZONE_RAINFOREST: Generated everywhere, if there is no desert in a certain distance from the tile.
 *  TROPICZONE_NORMAL: Everywhere else, i.e. between desert and rainforest and on sea (if you clear the water).
 *
 * In scenarios:
 *  TROPICZONE_NORMAL: Default value.
 *  TROPICZONE_DESERT: Placed manually.
 *  TROPICZONE_RAINFOREST: Placed if you plant certain rainforest-trees.
 */
enum TropicZone {
	TROPICZONE_NORMAL     = 0,      ///< Normal tropiczone
	TROPICZONE_DESERT     = 1,      ///< Tile is desert
	TROPICZONE_RAINFOREST = 2,      ///< Rainforest tile
};

/**
 * An offset value between two tiles.
 *
 * This value is used for the difference between
 * two tiles. It can be added to a TileIndex to get
 * the resulting TileIndex of the start tile applied
 * with this saved difference.
 *
 * @see TileDiffXY(int, int)
 */
typedef int32_t TileIndexDiff;

/**
 * Instead of using StrongType::Integer, implement delta behaviour with TileIndexDiff.
 * As TileIndexDiff is not a strong type, StrongType::IntegerDelta can't be used.
 * Instead of using many semi-overlapping mixins, which creates problems with ambiguous overloads, provide a single one which spells out exactly what is and isn't wanted.
 */
struct TileIndexIntegerMixin {
	template <typename TType, typename TBaseType>
	struct mixin {
		friend constexpr TType &operator ++(TType &lhs) { lhs.edit_base()++; return lhs; }
		friend constexpr TType &operator --(TType &lhs) { lhs.edit_base()--; return lhs; }
		friend constexpr TType operator ++(TType &lhs, int) { TType res = lhs; lhs.edit_base()++; return res; }
		friend constexpr TType operator --(TType &lhs, int) { TType res = lhs; lhs.edit_base()--; return res; }

		template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
		friend constexpr TType &operator +=(TType &lhs, T rhs) { lhs.edit_base() += rhs; return lhs; }

		template <class T, typename std::enable_if<std::is_same<T, TType>::value>::type>
		friend constexpr TType &operator +=(TType &lhs, const T &rhs) = delete;

		template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
		friend constexpr TType operator +(const TType &lhs, T rhs) { return TType(lhs.base() + rhs); }

		template <class T, typename std::enable_if<std::is_same<T, TType>::value>::type>
		friend constexpr TType operator +(const TType &lhs, const T &rhs) = delete;


		template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
		friend constexpr TType &operator -=(TType &lhs, T rhs) { lhs.edit_base() -= rhs; return lhs; }

		template <class T, typename std::enable_if<std::is_same<T, TType>::value>::type>
		friend constexpr TType &operator -=(TType &lhs, const T &rhs) = delete;

		template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
		friend constexpr TType operator -(const TType &lhs, T rhs) { return TType(lhs.base() - rhs); }

		template <class T, std::enable_if_t<std::is_same<T, TType>::value, int> = 0>
		friend constexpr TileIndexDiff operator -(const TType &lhs, const T &rhs) { return static_cast<TileIndexDiff>(lhs.base() - rhs.base()); }


		friend constexpr bool operator ==(const TType &lhs, const TType &rhs) { return lhs.base() == rhs.base(); }

		template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
		friend constexpr bool operator ==(const TType &lhs, T rhs) { return lhs.base() == static_cast<TBaseType>(rhs); }

		template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
		friend constexpr auto operator <=>(const TType &lhs, T rhs) { return lhs.base() <=> static_cast<TBaseType>(rhs); }

		friend constexpr auto operator <=>(const TType &lhs, const TType &rhs) { return lhs.base() <=> rhs.base(); }

		/* For most new types, the rest of the operators make no sense. For example,
		 * what does it actually mean to multiply a Year with a value. Or to do a
		 * bitwise OR on a Date. Or to divide a TileIndex by 2. Conceptually, they
		 * don't really mean anything. So force the user to first cast it to the
		 * base type, so the operation no longer returns the new Typedef. */

		constexpr TType &operator *=(const TType &rhs) = delete;
		constexpr TType operator *(const TType &rhs) = delete;
		constexpr TType operator *(const TBaseType &rhs) = delete;

		constexpr TType &operator /=(const TType &rhs) = delete;
		constexpr TType operator /(const TType &rhs) = delete;
		constexpr TType operator /(const TBaseType &rhs) = delete;

		constexpr TType &operator %=(const TType &rhs) = delete;
		constexpr TType operator %(const TType &rhs) = delete;
		constexpr TType operator %(const TBaseType &rhs) = delete;

		constexpr TType &operator &=(const TType &rhs) = delete;
		constexpr TType operator &(const TType &rhs) = delete;
		constexpr TType operator &(const TBaseType &rhs) = delete;

		constexpr TType &operator |=(const TType &rhs) = delete;
		constexpr TType operator |(const TType &rhs) = delete;
		constexpr TType operator |(const TBaseType &rhs) = delete;

		constexpr TType &operator ^=(const TType &rhs) = delete;
		constexpr TType operator ^(const TType &rhs) = delete;
		constexpr TType operator ^(const TBaseType &rhs) = delete;

		constexpr TType &operator <<=(const TType &rhs) = delete;
		constexpr TType operator <<(const TType &rhs) = delete;
		constexpr TType operator <<(const TBaseType &rhs) = delete;

		constexpr TType &operator >>=(const TType &rhs) = delete;
		constexpr TType operator >>(const TType &rhs) = delete;
		constexpr TType operator >>(const TBaseType &rhs) = delete;

		constexpr TType operator ~() = delete;
		constexpr TType operator -() = delete;
	};
};

/**
 * The index/ID of a Tile.
 *
 * This type represents an absolute tile ID, TileIndexDiff is used for relative values.
 *
 * Subtracting a TileIndex from another TileIndex results in a TileIndexDiff.
 * Adding a TileIndex to another TileIndex is not allowd.
 *
 * TileIndex - TileIndex --> TileIndexDiff
 * TileIndex + TileIndex --> delete
 *
 * Integer values can be added/subtracted tofrom TileIndex to produce an offsetted TileIndex.
 *
 * TileIndex + int/int32_t/uint/etc --> TileIndex
 * TileIndex - int/int32_t/uint/etc --> TileIndex
 */
struct TileIndexTag : public StrongType::TypedefTraits<uint32_t, TileIndexIntegerMixin> {};
using TileIndex = StrongType::Typedef<TileIndexTag>;

/* Make sure the size is as expected. */
static_assert(sizeof(TileIndex) == 4);

/**
 * The very nice invalid tile marker
 */
inline constexpr TileIndex INVALID_TILE = TileIndex{ (uint32_t)-1 };

debug_inline uint32_t debug_tile_index_type_erasure(TileIndex tile) { return tile.base(); }
[[noreturn]] void assert_tile_error(int line, const char *file, const char *expr, TileIndex tile);

#endif /* TILE_TYPE_H */
