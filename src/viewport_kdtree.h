/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file town_kdtree.h Declarations for accessing the k-d tree of towns */

#ifndef VIEWPORT_KDTREE_H
#define VIEWPORT_KDTREE_H

#include "core/kdtree.hpp"
#include "viewport_type.h"
#include "station_base.h"
#include "town_type.h"
#include "signs_base.h"

struct ViewportSignKdtreeItem {
	using IDType = uint16_t;

	template <typename T>
	static constexpr void IDTypeCheck()
	{
		static_assert(std::is_same_v<T, StationID> || std::is_same_v<T, TownID> || std::is_same_v<T, SignID>);
		static_assert(sizeof(IDType) >= sizeof(T));
	}

	enum ItemType : uint16_t {
		VKI_STATION,
		VKI_WAYPOINT,
		VKI_TOWN,
		VKI_SIGN,
	};
	ItemType type;
	IDType id;
	int32_t center;
	int32_t top;

	template <typename T>
	T GetIdAs() const
	{
		IDTypeCheck<T>();
		if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
			return T(this->id);
		} else {
			return T(static_cast<typename T::BaseType>(this->id));
		}

	}

	template <typename T>
	void SetID(T id)
	{
		IDTypeCheck<T>();
		if constexpr (std::is_integral_v<T>) {
			this->id = id;
		} else if constexpr (std::is_enum_v<T>) {
			this->id = to_underlying(id);
		} else {
			this->id = id.base();
		}
	}

	bool operator== (const ViewportSignKdtreeItem &other) const
	{
		if (this->type != other.type) return false;
		return this->id == other.id;
	}

	bool operator< (const ViewportSignKdtreeItem &other) const
	{
		if (this->type != other.type) return this->type < other.type;
		return this->id < other.id;
	}

	static ViewportSignKdtreeItem MakeStation(StationID id);
	static ViewportSignKdtreeItem MakeWaypoint(StationID id);
	static ViewportSignKdtreeItem MakeTown(TownID id);
	static ViewportSignKdtreeItem MakeSign(SignID id);
};

struct Kdtree_ViewportSignXYFunc {
	inline int32_t operator()(const ViewportSignKdtreeItem &item, int dim)
	{
		return (dim == 0) ? item.center : item.top;
	}
};

using ViewportSignKdtree = Kdtree<ViewportSignKdtreeItem, Kdtree_ViewportSignXYFunc, int32_t, int32_t>;
extern ViewportSignKdtree _viewport_sign_kdtree;
extern bool _viewport_sign_kdtree_valid;

void RebuildViewportKdtree();

#endif
