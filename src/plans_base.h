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
#include "command_func.h"
#include "map_func.h"
#include "date_func.h"
#include "viewport_func.h"
#include "core/endian_func.hpp"
#include <string>
#include <vector>

typedef Pool<Plan, PlanID, 16, 64000> PlanPool;
typedef std::vector<TileIndex> TileVector;
typedef std::vector<PlanLine*> PlanLineVector;
extern PlanPool _plan_pool;

struct PlanLine {
	bool visible;
	bool focused;
	TileVector tiles;
	Rect viewport_extents;

	PlanLine()
	{
		this->visible = true;
		this->focused = false;
	}

	~PlanLine()
	{
		this->Clear();
	}

	void Clear()
	{
		this->tiles.clear();
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

		if (this->tiles.size() * sizeof(TileIndex) >= MAX_CMD_TEXT_LENGTH) return false;

		this->tiles.push_back(tile);
		return true;
	}

	void SetFocus(bool focused)
	{
		if (this->focused != focused) this->MarkDirty();
		this->focused = focused;
	}

	bool ToggleVisibility()
	{
		this->SetVisibility(!this->visible);
		return this->visible;
	}

	void SetVisibility(bool visible)
	{
		if (this->visible != visible) this->MarkDirty();
		this->visible = visible;
	}

	void MarkDirty() const
	{
		const uint sz = (uint) this->tiles.size();
		for (uint i = 1; i < sz; i++) {
			MarkTileLineDirty(this->tiles[i-1], this->tiles[i], VMDF_NOT_LANDSCAPE);
		}
	}

	TileIndex *Export(uint *buffer_length)
	{
		const uint cnt = (uint) this->tiles.size();
		const uint datalen = sizeof(TileIndex) * cnt;
		TileIndex *buffer = (TileIndex *) malloc(datalen);
		if (buffer) {
			for (uint i = 0; i < cnt; i++) {
				buffer[i] = TO_LE32(this->tiles[i]);
			}
			if (buffer_length) *buffer_length = datalen;
		}
		return buffer;
	}

	bool Import(const TileIndex* data, const uint data_length)
	{
		for (uint i = data_length; i != 0; i--, data++) {
			TileIndex t = FROM_LE32(*data);
			if (t >= MapSize()) return false;
			this->tiles.push_back(t);
		}
		return true;
	}

	void AddLineToCalculateCentreTile(uint64 &x, uint64 &y, uint32 &count) const
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
		uint64 x = 0;
		uint64 y = 0;
		uint32 count = 0;
		this->AddLineToCalculateCentreTile(x, y, count);
		if (count == 0) return INVALID_TILE;
		return TileXY(x / count, y / count);
	}

	void UpdateVisualExtents();
};

struct Plan : PlanPool::PoolItem<&_plan_pool> {
	Owner owner;
	PlanLineVector lines;
	PlanLine *temp_line;
	bool visible;
	bool visible_by_all;
	bool show_lines;
	Date creation_date;
	std::string name;
	Colours colour;

	Plan(Owner owner = INVALID_OWNER)
	{
		this->owner = owner;
		this->creation_date = _date;
		this->visible = false;
		this->visible_by_all = false;
		this->show_lines = false;
		this->colour = COLOUR_WHITE;
		this->temp_line = new PlanLine();
	}

	~Plan()
	{
		for (PlanLineVector::iterator it = lines.begin(); it != lines.end(); it++) {
			delete (*it);
		}
		this->lines.clear();
		delete temp_line;
	}

	void SetFocus(bool focused)
	{
		for (PlanLineVector::iterator it = lines.begin(); it != lines.end(); it++) {
			(*it)->SetFocus(focused);
		}
	}

	void SetVisibility(bool visible, bool do_lines = true)
	{
		this->visible = visible;

		if (!do_lines) return;
		for (PlanLineVector::iterator it = lines.begin(); it != lines.end(); it++) {
			(*it)->SetVisibility(visible);
		}
	}

	bool ToggleVisibility()
	{
		this->SetVisibility(!this->visible);
		return this->visible;
	}

	PlanLine *NewLine()
	{
		PlanLine *pl = new PlanLine();
		if (pl) this->lines.push_back(pl);
		return pl;
	}

	bool StoreTempTile(TileIndex tile)
	{
		return this->temp_line->AppendTile(tile);
	}

	bool ValidateNewLine()
	{
		bool ret = false;
		if (this->temp_line->tiles.size() > 1) {
			uint buffer_length = 0;
			const TileIndex *buffer = this->temp_line->Export(&buffer_length);
			if (buffer) {
				_current_plan->SetVisibility(true, false);
				ret = DoCommandPEx(0, _current_plan->index, (uint32) this->temp_line->tiles.size(), 0, CMD_ADD_PLAN_LINE, nullptr, (const char *) buffer, buffer_length);
				free(buffer);
			}
			_current_plan->temp_line->MarkDirty();
			this->temp_line->Clear();
		}
		return ret;
	}

	bool IsListable()
	{
		return (this->owner == _local_company || this->visible_by_all);
	}

	bool IsVisible()
	{
		if (!this->IsListable()) return false;
		return this->visible;
	}

	bool HasName() const
	{
		return !this->name.empty();
	}

	bool ToggleVisibilityByAll()
	{
		if (_current_plan->owner == _local_company) DoCommandP(0, _current_plan->index, !this->visible_by_all, CMD_CHANGE_PLAN_VISIBILITY);
		return this->visible_by_all;
	}

	void SetPlanColour(Colours colour)
	{
		if (_current_plan->owner == _local_company) DoCommandP(0, _current_plan->index, colour, CMD_CHANGE_PLAN_COLOUR);
	}

	const std::string &GetName() const
	{
		return this->name;
	}

	TileIndex CalculateCentreTile() const
	{
		uint64 x = 0;
		uint64 y = 0;
		uint32 count = 0;
		for (PlanLineVector::const_iterator it = lines.begin(); it != lines.end(); it++) {
			(*it)->AddLineToCalculateCentreTile(x, y, count);
		}
		if (count == 0) return INVALID_TILE;
		return TileXY(x / count, y / count);
	}
};

#endif /* PLANS_BASE_H */
