/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file plans_base.h Base class for plans. */

#ifndef PLANS_BASE_H
#define PLANS_BASE_H

#include "plans_func.h"
#include "core/pool_type.hpp"
#include "company_type.h"
#include "company_func.h"
#include "map_func.h"
#include "date_func.h"
#include "viewport_func.h"
#include <string>
#include <vector>

typedef Pool<Plan, PlanID, 16, 64000> PlanPool;
extern PlanPool _plan_pool;

static constexpr size_t MAX_PLAN_PAYLOAD_SIZE = 32000;

struct BasePlanLine {
	std::vector<TileIndex> tiles;
	Rect viewport_extents;

	BasePlanLine()
	{
		_plan_update_counter++;
	}

	~BasePlanLine()
	{
		this->Clear();
	}

	void Clear()
	{
		this->tiles.clear();
		_plan_update_counter++;
	}

	bool AppendTile(TileIndex tile)
	{
		const uint cnt = (uint) this->tiles.size();

		if (cnt > 0) {
			const TileIndex last_tile = this->tiles[cnt - 1];
			if (last_tile == tile) return false;
			MarkTileLineDirty(last_tile, tile, VMDF_NOT_LANDSCAPE);

			if (cnt > 1) {
				const TileIndex t0 = this->tiles[cnt - 2];
				const int x0 = (int) TileX(t0);
				const int y0 = (int) TileY(t0);
				const TileIndex t1 = this->tiles[cnt - 1];
				const int x1 = (int) TileX(t1);
				const int y1 = (int) TileY(t1);
				const int x2 = (int) TileX(tile);
				const int y2 = (int) TileY(tile);

				if ((y1 - y0) * (x2 - x1) == (y2 - y1) * (x1 - x0)) { // Same direction.
					if (abs(x2 - x1) <= abs(x2 - x0) && abs(y2 - y1) <= abs(y2 - y0)) { // Tile i+1 is between i and i+2.
						/* The new tile is in the continuity, just update the last tile. */
						this->tiles[cnt - 1] = tile;
						MarkTileLineDirty(t1, tile, VMDF_NOT_LANDSCAPE);
						return true;
					}
				}
			}
		}

		if (this->tiles.size() * sizeof(TileIndex) >= MAX_PLAN_PAYLOAD_SIZE) return false;

		this->tiles.push_back(tile);
		_plan_update_counter++;
		return true;
	}

	void MarkDirty() const
	{
		const uint sz = (uint) this->tiles.size();
		for (uint i = 1; i < sz; i++) {
			MarkTileLineDirty(this->tiles[i-1], this->tiles[i], VMDF_NOT_LANDSCAPE);
		}
	}

	void AddLineToCalculateCentreTile(uint64_t &x, uint64_t &y, uint32_t &count) const
	{
		for (size_t i = 0; i < this->tiles.size(); i++) {
			TileIndex t = this->tiles[i];
			x += TileX(t);
			y += TileY(t);
			count++;
		}
	}

	TileIndex CalculateCentreTile() const
	{
		uint64_t x = 0;
		uint64_t y = 0;
		uint32_t count = 0;
		this->AddLineToCalculateCentreTile(x, y, count);
		if (count == 0) return INVALID_TILE;
		return TileXY(x / count, y / count);
	}

	void UpdateVisualExtents();
};

struct PlanLine : public BasePlanLine {
	bool visible = true;
	bool focused = false;

	void SetFocus(bool focused)
	{
		if (this->focused != focused) {
			this->MarkDirty();
			_plan_update_counter++;
		}
		this->focused = focused;
	}

	bool ToggleVisibility()
	{
		this->SetVisibility(!this->visible);
		return this->visible;
	}

	void SetVisibility(bool visible)
	{
		if (this->visible != visible) {
			this->MarkDirty();
			_plan_update_counter++;
		}
		this->visible = visible;
	}
};

struct Plan : PlanPool::PoolItem<&_plan_pool> {
	Owner owner;
	Colours colour;
	CalTime::Date creation_date;
	std::vector<PlanLine> lines;
	BasePlanLine temp_line{};
	std::string name;
	TileIndex last_tile;
	bool visible;
	bool visible_by_all;
	bool show_lines;

	Plan(Owner owner = INVALID_OWNER)
	{
		this->owner = owner;
		this->creation_date = CalTime::CurDate();
		this->visible = false;
		this->visible_by_all = false;
		this->show_lines = false;
		this->colour = COLOUR_WHITE;
		this->last_tile = INVALID_TILE;
	}

	void SetFocus(bool focused)
	{
		for (PlanLine &it : lines) {
			it.SetFocus(focused);
		}
	}

	void SetVisibility(bool visible, bool do_lines = true)
	{
		this->visible = visible;
		_plan_update_counter++;

		if (!do_lines) return;
		for (PlanLine &it : lines) {
			it.SetVisibility(visible);
		}
	}

	bool ToggleVisibility()
	{
		this->SetVisibility(!this->visible);
		return this->visible;
	}

	PlanLine &NewLine()
	{
		return this->lines.emplace_back();
	}

	bool StoreTempTile(TileIndex tile)
	{
		return this->temp_line.AppendTile(tile);
	}

	bool ValidateNewLine();

	bool IsListable() const
	{
		return (this->owner == _local_company || this->visible_by_all);
	}

	bool IsVisible() const
	{
		if (!this->IsListable()) return false;
		return this->visible;
	}

	bool HasName() const
	{
		return !this->name.empty();
	}

	const std::string &GetName() const
	{
		return this->name;
	}

	TileIndex CalculateCentreTile() const
	{
		uint64_t x = 0;
		uint64_t y = 0;
		uint32_t count = 0;
		for (const PlanLine &it : lines) {
			it.AddLineToCalculateCentreTile(x, y, count);
		}
		if (count == 0) return INVALID_TILE;
		return TileXY(x / count, y / count);
	}
};

#endif /* PLANS_BASE_H */
