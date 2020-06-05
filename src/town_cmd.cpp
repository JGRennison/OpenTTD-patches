/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file town_cmd.cpp Handling of town tiles. */

#include "stdafx.h"

#include <bitset>

#include "road.h"
#include "road_internal.h" /* Cleaning up road bits */
#include "road_cmd.h"
#include "landscape.h"
#include "viewport_func.h"
#include "viewport_kdtree.h"
#include "cmd_helper.h"
#include "command_func.h"
#include "industry.h"
#include "station_base.h"
#include "station_kdtree.h"
#include "company_base.h"
#include "news_func.h"
#include "error.h"
#include "object.h"
#include "genworld.h"
#include "newgrf_debug.h"
#include "newgrf_house.h"
#include "autoslope.h"
#include "tunnelbridge_map.h"
#include "strings_func.h"
#include "window_func.h"
#include "string_func.h"
#include "newgrf_cargo.h"
#include "cheat_type.h"
#include "animated_tile_func.h"
#include "date_func.h"
#include "subsidy_func.h"
#include "core/pool_func.hpp"
#include "town.h"
#include "town_kdtree.h"
#include "townname_func.h"
#include "core/random_func.hpp"
#include "core/backup_type.hpp"
#include "depot_base.h"
#include "object_map.h"
#include "object_base.h"
#include "ai/ai.hpp"
#include "game/game.hpp"
#include "zoom_func.h"
#include "zoning.h"
#include "scope.h"

#include "table/strings.h"
#include "table/town_land.h"

#include "safeguards.h"

TownID _new_town_id;
CargoTypes _town_cargoes_accepted; ///< Bitmap of all cargoes accepted by houses.

/* Initialize the town-pool */
TownPool _town_pool("Town");
INSTANTIATE_POOL_METHODS(Town)

static bool CanFollowRoad(TileIndex tile, DiagDirection dir);

TownKdtree _town_kdtree(&Kdtree_TownXYFunc);

void RebuildTownKdtree()
{
	std::vector<TownID> townids;
	for (const Town *town : Town::Iterate()) {
		townids.push_back(town->index);
	}
	_town_kdtree.Build(townids.begin(), townids.end());
}


/**
 * Check if a town 'owns' a bridge.
 * Bridges to not directly have an owner, so we check the tiles adjacent to the bridge ends.
 * If either adjacent tile belongs to the town then it will be assumed that the town built
 * the bridge.
 * @param tile Bridge tile to test
 * @param t Town we are interested in
 * @return true if town 'owns' a bridge.
 */
static bool TestTownOwnsBridge(TileIndex tile, const Town *t)
{
	if (!IsTileOwner(tile, OWNER_TOWN)) return false;

	TileIndex adjacent = tile + TileOffsByDiagDir(ReverseDiagDir(GetTunnelBridgeDirection(tile)));
	bool town_owned = IsTileType(adjacent, MP_ROAD) && IsTileOwner(adjacent, OWNER_TOWN) && GetTownIndex(adjacent) == t->index;

	if (!town_owned) {
		/* Or other adjacent road */
		TileIndex adjacent = tile + TileOffsByDiagDir(ReverseDiagDir(GetTunnelBridgeDirection(GetOtherTunnelBridgeEnd(tile))));
		town_owned = IsTileType(adjacent, MP_ROAD) && IsTileOwner(adjacent, OWNER_TOWN) && GetTownIndex(adjacent) == t->index;
	}

	return town_owned;
}

Town::~Town()
{
	if (CleaningPool()) return;

	/* Delete town authority window
	 * and remove from list of sorted towns */
	DeleteWindowById(WC_TOWN_VIEW, this->index);

	/* Check no industry is related to us. */
	for (const Industry *i : Industry::Iterate()) assert(i->town != this);

	/* ... and no object is related to us. */
	for (const Object *o : Object::Iterate()) assert(o->town != this);

	/* Check no tile is related to us. */
	for (TileIndex tile = 0; tile < MapSize(); ++tile) {
		switch (GetTileType(tile)) {
			case MP_HOUSE:
				assert_tile(GetTownIndex(tile) != this->index, tile);
				break;

			case MP_ROAD:
				assert_tile(!HasTownOwnedRoad(tile) || GetTownIndex(tile) != this->index, tile);
				break;

			case MP_TUNNELBRIDGE:
				assert_tile(!TestTownOwnsBridge(tile, this), tile);
				break;

			default:
				break;
		}
	}

	/* Clear the persistent storage list. */
	this->psa_list.clear();

	DeleteSubsidyWith(ST_TOWN, this->index);
	DeleteNewGRFInspectWindow(GSF_FAKE_TOWNS, this->index);
	CargoPacket::InvalidateAllFrom(ST_TOWN, this->index);
	MarkWholeScreenDirty();
}


/**
 * Invalidating of the "nearest town cache" has to be done
 * after removing item from the pool.
 * @param index index of deleted item
 */
void Town::PostDestructor(size_t index)
{
	InvalidateWindowData(WC_TOWN_DIRECTORY, 0, TDIWD_FORCE_REBUILD);
	UpdateNearestTownForRoadTiles(false);

	/* Give objects a new home! */
	for (Object *o : Object::Iterate()) {
		if (o->town == nullptr) o->town = CalcClosestTownFromTile(o->location.tile, UINT_MAX);
	}
}

/**
 * Assigns town layout. If Random, generates one based on TileHash.
 */
void Town::InitializeLayout(TownLayout layout)
{
	if (layout != TL_RANDOM) {
		this->layout = layout;
		return;
	}

	this->layout = static_cast<TownLayout>(TileHash(TileX(this->xy), TileY(this->xy)) % (NUM_TLS - 1));
}

/**
 * Return a random valid town.
 * @return random town, nullptr if there are no towns
 */
/* static */ Town *Town::GetRandom()
{
	if (Town::GetNumItems() == 0) return nullptr;
	int num = RandomRange((uint16)Town::GetNumItems());
	size_t index = MAX_UVALUE(size_t);

	while (num >= 0) {
		num--;
		index++;

		/* Make sure we have a valid town */
		while (!Town::IsValidID(index)) {
			index++;
			assert(index < Town::GetPoolSize());
		}
	}

	return Town::Get(index);
}

/**
 * Updates the town label of the town after changes in rating. The colour scheme is:
 * Red: Appalling and Very poor ratings.
 * Orange: Poor and mediocre ratings.
 * Yellow: Good rating.
 * White: Very good rating (standard).
 * Green: Excellent and outstanding ratings.
 */
void Town::UpdateLabel()
{
	if (!(_game_mode == GM_EDITOR) && (_local_company < MAX_COMPANIES)) {
		int r = this->ratings[_local_company];
		(this->town_label = 0, r <= RATING_VERYPOOR)  || // Appalling and Very Poor
		(this->town_label++,   r <= RATING_MEDIOCRE)  || // Poor and Mediocre
		(this->town_label++,   r <= RATING_GOOD)      || // Good
		(this->town_label++,   r <= RATING_VERYGOOD)  || // Very Good
		(this->town_label++,   true);                    // Excellent and Outstanding
	}
}

void Town::FillCachedName() const
{
	char buf[MAX_LENGTH_TOWN_NAME_CHARS * MAX_CHAR_LENGTH];
	char *end = GetTownName(buf, this, lastof(buf));
	this->cached_name.assign(buf, end);
}

/**
 * Get the cost for removing this house
 * @return the cost (inflation corrected etc)
 */
Money HouseSpec::GetRemovalCost() const
{
	return (_price[PR_CLEAR_HOUSE] * this->removal_cost) >> 8;
}

/* Local */
static int _grow_town_result;

/* Describe the possible states */
enum TownGrowthResult {
	GROWTH_SUCCEED         = -1,
	GROWTH_SEARCH_STOPPED  =  0
//	GROWTH_SEARCH_RUNNING >=  1
};

static bool BuildTownHouse(Town *t, TileIndex tile);
static Town *CreateRandomTown(uint attempts, uint32 townnameparts, TownSize size, bool city, TownLayout layout);

static void TownDrawHouseLift(const TileInfo *ti)
{
	AddChildSpriteScreen(SPR_LIFT, PAL_NONE, 14, 60 - GetLiftPosition(ti->tile));
}

static void DrawHouseLiftInGUI(int x, int y)
{
	DrawSprite(SPR_LIFT, PAL_NONE, x - 18, y + 7);
}

typedef void TownDrawTileProc(const TileInfo *ti);
static TownDrawTileProc * const _town_draw_tile_procs[1] = {
	TownDrawHouseLift
};

/**
 * Return a random direction
 *
 * @return a random direction
 */
static inline DiagDirection RandomDiagDir()
{
	return (DiagDirection)(3 & Random());
}

/**
 * House Tile drawing handler.
 * Part of the tile loop process
 * @param ti TileInfo of the tile to draw
 */
static void DrawTile_Town(TileInfo *ti, DrawTileProcParams params)
{
	HouseID house_id = GetHouseType(ti->tile);

	if (house_id >= NEW_HOUSE_OFFSET) {
		/* Houses don't necessarily need new graphics. If they don't have a
		 * spritegroup associated with them, then the sprite for the substitute
		 * house id is drawn instead. */
		if (HouseSpec::Get(house_id)->grf_prop.spritegroup[0] != nullptr) {
			DrawNewHouseTile(ti, house_id);
			return;
		} else {
			house_id = HouseSpec::Get(house_id)->grf_prop.subst_id;
		}
	}

	/* Retrieve pointer to the draw town tile struct */
	const DrawBuildingsTileStruct *dcts = &_town_draw_tile_data[house_id << 4 | TileHash2Bit(ti->x, ti->y) << 2 | GetHouseBuildingStage(ti->tile)];

	if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

	DrawGroundSprite(dcts->ground.sprite, dcts->ground.pal);

	/* If houses are invisible, do not draw the upper part */
	if (IsInvisibilitySet(TO_HOUSES)) return;

	/* Add a house on top of the ground? */
	SpriteID image = dcts->building.sprite;
	if (image != 0) {
		AddSortableSpriteToDraw(image, dcts->building.pal,
			ti->x + dcts->subtile_x,
			ti->y + dcts->subtile_y,
			dcts->width,
			dcts->height,
			dcts->dz,
			ti->z,
			IsTransparencySet(TO_HOUSES)
		);

		if (IsTransparencySet(TO_HOUSES)) return;
	}

	{
		int proc = dcts->draw_proc - 1;

		if (proc >= 0) _town_draw_tile_procs[proc](ti);
	}
}

static void DrawOldHouseTileInGUI(int x, int y, HouseID house_id, bool ground)
{
	/* Retrieve pointer to the draw town tile struct */
	const DrawBuildingsTileStruct *dcts = &_town_draw_tile_data[house_id << 4 | TOWN_HOUSE_COMPLETED];
	if (ground) {
		/* Draw the ground sprite */
		DrawSprite(dcts->ground.sprite, dcts->ground.pal, x, y);
	} else {
		/* Add a house on top of the ground? */
		if (dcts->building.sprite != 0) {
			DrawSprite(dcts->building.sprite, dcts->building.pal, x + dcts->subtile_x, y + dcts->subtile_y);
		}
		/* Draw the lift */
		if (dcts->draw_proc == 1) DrawHouseLiftInGUI(x, y);
	}
}

/**
 * Draw image of a house. Image will be centered between the \c left and the \c right and verticaly aligned to the \c bottom.
 *
 * @param house_id house type
 * @param left left bound of the drawing area
 * @param top top bound of the drawing area
 * @param right right bound of the drawing area
 * @param bottom bottom bound of the drawing area
 */
void DrawHouseImage(HouseID house_id, int left, int top, int right, int bottom)
{
	DrawPixelInfo tmp_dpi;
	if (!FillDrawPixelInfo(&tmp_dpi, left, top, right - left + 1, bottom - top + 1)) return;
	DrawPixelInfo *old_dpi = _cur_dpi;
	_cur_dpi = &tmp_dpi;

	const HouseSpec *hs = HouseSpec::Get(house_id);

	/* sprites are relative to the topmost pixel of the ground tile */
	uint x = (right - left + 1) / 2;
	uint y = bottom - top + 1 - TILE_PIXELS;
	if (hs->building_flags & TILE_SIZE_1x2) x -= TILE_PIXELS / 2;
	if (hs->building_flags & TILE_SIZE_2x1) x += TILE_PIXELS / 2;
	if (hs->building_flags & BUILDING_HAS_2_TILES) y -= TILE_PIXELS / 2;
	if (hs->building_flags & BUILDING_HAS_4_TILES) y -= TILE_PIXELS / 2;

	bool new_house = false;
	if (house_id >= NEW_HOUSE_OFFSET) {
		/* Houses don't necessarily need new graphics. If they don't have a
		 * spritegroup associated with them, then the sprite for the substitute
		 * house id is drawn instead. */
		if (hs->grf_prop.spritegroup[0] != nullptr) {
			new_house = true;
		} else {
			house_id = hs->grf_prop.subst_id;
		}
	}

	uint num_row = (hs->building_flags & BUILDING_2_TILES_X) ? 2 : 1;
	uint num_col = (hs->building_flags & BUILDING_2_TILES_Y) ? 2 : 1;

	for (bool ground = true; ; ground = !ground) {
		HouseID hid = house_id;
		for (uint row = 0; row < num_row; row++) {
			for (uint col = 0; col < num_col; col++) {
				Point offset = RemapCoords(row * TILE_SIZE, col * TILE_SIZE, 0); // offset for current tile
				offset.x = UnScaleByZoom(offset.x, ZOOM_LVL_GUI);
				offset.y = UnScaleByZoom(offset.y, ZOOM_LVL_GUI);
				if (new_house) {
					DrawNewHouseTileInGUI(x + offset.x, y + offset.y, hid, ground);
				} else {
					DrawOldHouseTileInGUI(x + offset.x, y + offset.y, hid, ground);
				}
				hid++;
			}
		}
		if (!ground) break;
	}

	_cur_dpi = old_dpi;
}

static int GetSlopePixelZ_Town(TileIndex tile, uint x, uint y)
{
	return GetTileMaxPixelZ(tile);
}

/** Tile callback routine */
static Foundation GetFoundation_Town(TileIndex tile, Slope tileh)
{
	HouseID hid = GetHouseType(tile);

	/* For NewGRF house tiles we might not be drawing a foundation. We need to
	 * account for this, as other structures should
	 * draw the wall of the foundation in this case.
	 */
	if (hid >= NEW_HOUSE_OFFSET) {
		const HouseSpec *hs = HouseSpec::Get(hid);
		if (hs->grf_prop.spritegroup[0] != nullptr && HasBit(hs->callback_mask, CBM_HOUSE_DRAW_FOUNDATIONS)) {
			uint32 callback_res = GetHouseCallback(CBID_HOUSE_DRAW_FOUNDATIONS, 0, 0, hid, Town::GetByTile(tile), tile);
			if (callback_res != CALLBACK_FAILED && !ConvertBooleanCallback(hs->grf_prop.grffile, CBID_HOUSE_DRAW_FOUNDATIONS, callback_res)) return FOUNDATION_NONE;
		}
	}
	return FlatteningFoundation(tileh);
}

/**
 * Animate a tile for a town
 * Only certain houses can be animated
 * The newhouses animation supersedes regular ones
 * @param tile TileIndex of the house to animate
 */
void AnimateTile_Town(TileIndex tile)
{
	if (GetHouseType(tile) >= NEW_HOUSE_OFFSET) {
		AnimateNewHouseTile(tile);
		return;
	}

	if (_scaled_tick_counter & 3) return;

	/* If the house is not one with a lift anymore, then stop this animating.
	 * Not exactly sure when this happens, but probably when a house changes.
	 * Before this was just a return...so it'd leak animated tiles..
	 * That bug seems to have been here since day 1?? */
	if (!(HouseSpec::Get(GetHouseType(tile))->building_flags & BUILDING_IS_ANIMATED)) {
		DeleteAnimatedTile(tile);
		return;
	}

	if (!LiftHasDestination(tile)) {
		uint i;

		/* Building has 6 floors, number 0 .. 6, where 1 is illegal.
		 * This is due to the fact that the first floor is, in the graphics,
		 *  the height of 2 'normal' floors.
		 * Furthermore, there are 6 lift positions from floor N (incl) to floor N + 1 (excl) */
		do {
			i = RandomRange(7);
		} while (i == 1 || i * 6 == GetLiftPosition(tile));

		SetLiftDestination(tile, i);
	}

	int pos = GetLiftPosition(tile);
	int dest = GetLiftDestination(tile) * 6;
	pos += (pos < dest) ? 1 : -1;
	SetLiftPosition(tile, pos);

	if (pos == dest) {
		HaltLift(tile);
		DeleteAnimatedTile(tile);
	}

	MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
}

/**
 * Determines if a town is close to a tile
 * @param tile TileIndex of the tile to query
 * @param dist maximum distance to be accepted
 * @returns true if the tile correspond to the distance criteria
 */
static bool IsCloseToTown(TileIndex tile, uint dist)
{
	if (_town_kdtree.Count() == 0) return false;
	Town *t = Town::Get(_town_kdtree.FindNearest(TileX(tile), TileY(tile)));
	return DistanceManhattan(tile, t->xy) < dist;
}

/**
 * Resize the sign(label) of the town after changes in
 * population (creation or growth or else)
 */
void Town::UpdateVirtCoord()
{
	this->UpdateLabel();
	Point pt = RemapCoords2(TileX(this->xy) * TILE_SIZE, TileY(this->xy) * TILE_SIZE);

	if (_viewport_sign_kdtree_valid && this->cache.sign.kdtree_valid) _viewport_sign_kdtree.Remove(ViewportSignKdtreeItem::MakeTown(this->index));

	SetDParam(0, this->index);
	SetDParam(1, this->cache.population);
	this->cache.sign.UpdatePosition(HasBit(_display_opt, DO_SHOW_TOWN_NAMES) ? ZOOM_LVL_OUT_128X : ZOOM_LVL_END, pt.x, pt.y - 24 * ZOOM_LVL_BASE, this->Label(), STR_VIEWPORT_TOWN);

	if (_viewport_sign_kdtree_valid) _viewport_sign_kdtree.Insert(ViewportSignKdtreeItem::MakeTown(this->index));

	SetWindowDirty(WC_TOWN_VIEW, this->index);
}

/** Update the virtual coords needed to draw the town sign for all towns. */
void UpdateAllTownVirtCoords()
{
	for (Town *t : Town::Iterate()) {
		t->UpdateVirtCoord();
	}
}

void ClearAllTownCachedNames()
{
	for (Town *t : Town::Iterate()) {
		t->cached_name.clear();
	}
}

/**
 * Change the towns population
 * @param t Town which population has changed
 * @param mod population change (can be positive or negative)
 */
static void ChangePopulation(Town *t, int mod)
{
	t->cache.population += mod;
	InvalidateWindowData(WC_TOWN_VIEW, t->index); // Cargo requirements may appear/vanish for small populations
	if (_settings_client.gui.population_in_label) t->UpdateVirtCoord();

	InvalidateWindowData(WC_TOWN_DIRECTORY, 0, TDIWD_POPULATION_CHANGE);
}

/**
 * Determines the world population
 * Basically, count population of all towns, one by one
 * @return uint32 the calculated population of the world
 */
uint32 GetWorldPopulation()
{
	uint32 pop = 0;
	for (const Town *t : Town::Iterate()) pop += t->cache.population;
	return pop;
}

/**
 * Remove stations from nearby station list if a town is no longer in the catchment area of each.
 * To improve performance only checks stations that cover the provided house area (doesn't need to contain an actual house).
 * @param t Town to work on
 * @param tile Location of house area (north part)
 * @param flags BuildingFlags containing the size of house area
 */
static void RemoveNearbyStations(Town *t, TileIndex tile, BuildingFlags flags)
{
	for (StationList::iterator it = t->stations_near.begin(); it != t->stations_near.end(); /* incremented inside loop */) {
		const Station *st = *it;

		bool covers_area = st->TileIsInCatchment(tile);
		if (flags & BUILDING_2_TILES_Y)   covers_area |= st->TileIsInCatchment(tile + TileDiffXY(0, 1));
		if (flags & BUILDING_2_TILES_X)   covers_area |= st->TileIsInCatchment(tile + TileDiffXY(1, 0));
		if (flags & BUILDING_HAS_4_TILES) covers_area |= st->TileIsInCatchment(tile + TileDiffXY(1, 1));

		if (covers_area && !st->CatchmentCoversTown(t->index)) {
			it = t->stations_near.erase(it);
		} else {
			++it;
		}
	}
}

/**
 * Helper function for house completion stages progression
 * @param tile TileIndex of the house (or parts of it) to "grow"
 */
static void MakeSingleHouseBigger(TileIndex tile)
{
	assert_tile(IsTileType(tile, MP_HOUSE), tile);

	/* progress in construction stages */
	IncHouseConstructionTick(tile);
	if (GetHouseConstructionTick(tile) != 0) return;

	AnimateNewHouseConstruction(tile);

	if (IsHouseCompleted(tile)) {
		/* Now that construction is complete, we can add the population of the
		 * building to the town. */
		ChangePopulation(Town::GetByTile(tile), HouseSpec::Get(GetHouseType(tile))->population);
		ResetHouseAge(tile);
	}
	MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
}

/**
 * Make the house advance in its construction stages until completion
 * @param tile TileIndex of house
 */
static void MakeTownHouseBigger(TileIndex tile)
{
	uint flags = HouseSpec::Get(GetHouseType(tile))->building_flags;
	if (flags & BUILDING_HAS_1_TILE)  MakeSingleHouseBigger(TILE_ADDXY(tile, 0, 0));
	if (flags & BUILDING_2_TILES_Y)   MakeSingleHouseBigger(TILE_ADDXY(tile, 0, 1));
	if (flags & BUILDING_2_TILES_X)   MakeSingleHouseBigger(TILE_ADDXY(tile, 1, 0));
	if (flags & BUILDING_HAS_4_TILES) MakeSingleHouseBigger(TILE_ADDXY(tile, 1, 1));
}

/**
 * Generate cargo for a town (house).
 *
 * The amount of cargo should be and will be greater than zero.
 *
 * @param t current town
 * @param ct type of cargo to generate, usually CT_PASSENGERS or CT_MAIL
 * @param amount how many units of cargo
 * @param stations available stations for this house
 * @param economy_adjust true if amount should be reduced during recession
 */
static void TownGenerateCargo (Town *t, CargoID ct, uint amount, StationFinder &stations, bool economy_adjust)
{
	// custom cargo generation factor
	int factor = _settings_game.economy.town_cargo_scale_factor;

	// when the economy flunctuates, everyone wants to stay at home
	if (economy_adjust && EconomyIsInRecession()) {
		amount = (amount + 1) >> 1;
	}

	factor += 200; // ensure factor is positive
	assert(factor >= 0);
	int cf = (factor / 10) - 20;
	int fine = factor % 10;
	if (fine != 0) {
		// 2^0.1 << 16 to 2^0.9 << 16
		const uint32 adj[9] = {70239, 75281, 80684, 86475, 92681, 99334, 106463, 114104, 122294};
		uint64 scaled_amount = ((uint64) amount) * ((uint64) adj[fine - 1]);
		amount = scaled_amount >> 16;
	}

	// apply custom factor?
	if (cf < 0) {
		// approx (amount / 2^cf)
		// adjust with a constant offset of {(2 ^ cf) - 1} (i.e. add cf * 1-bits) before dividing to ensure that it doesn't become zero
		// this skews the curve a little so that isn't entirely exponential, but will still decrease
		amount = (amount + ((1 << -cf) - 1)) >> -cf;
	}

	else if (cf > 0) {
		// approx (amount * 2^cf)
		// XXX: overflow?
		amount = amount << cf;
	}

	// calculate for town stats

	switch (ct) {
		case CT_PASSENGERS:
		case CT_MAIL:
			t->supplied[ct].new_max += amount;
			t->supplied[ct].new_act += MoveGoodsToStation(ct, amount, ST_TOWN, t->index, stations.GetStations());
			break;

		default: {
			const CargoSpec *cs = CargoSpec::Get(ct);
			t->supplied[cs->Index()].new_max += amount;
			t->supplied[cs->Index()].new_act += MoveGoodsToStation(ct, amount, ST_TOWN, t->index, stations.GetStations());
			break;
		}
	}
}

/**
 * Tile callback function.
 *
 * Periodic tic handler for houses and town
 * @param tile been asked to do its stuff
 */
static void TileLoop_Town(TileIndex tile)
{
	HouseID house_id = GetHouseType(tile);

	/* NewHouseTileLoop returns false if Callback 21 succeeded, i.e. the house
	 * doesn't exist any more, so don't continue here. */
	if (house_id >= NEW_HOUSE_OFFSET && !NewHouseTileLoop(tile)) return;

	if (!IsHouseCompleted(tile)) {
		/* Construction is not completed. See if we can go further in construction*/
		MakeTownHouseBigger(tile);
		return;
	}

	const HouseSpec *hs = HouseSpec::Get(house_id);

	/* If the lift has a destination, it is already an animated tile. */
	if ((hs->building_flags & BUILDING_IS_ANIMATED) &&
			house_id < NEW_HOUSE_OFFSET &&
			!LiftHasDestination(tile) &&
			Chance16(1, 2)) {
		AddAnimatedTile(tile);
	}

	Town *t = Town::GetByTile(tile);
	uint32 r = Random();

	StationFinder stations(TileArea(tile, 1, 1));

	if (HasBit(hs->callback_mask, CBM_HOUSE_PRODUCE_CARGO)) {
		for (uint i = 0; i < 256; i++) {
			uint16 callback = GetHouseCallback(CBID_HOUSE_PRODUCE_CARGO, i, r, house_id, t, tile);

			if (callback == CALLBACK_FAILED || callback == CALLBACK_HOUSEPRODCARGO_END) break;

			CargoID cargo = GetCargoTranslation(GB(callback, 8, 7), hs->grf_prop.grffile);
			if (cargo == CT_INVALID) continue;

			uint amt = GB(callback, 0, 8);
			if (amt == 0) continue;

			// XXX: no economy flunctuation for GRF cargos?
			TownGenerateCargo(t, cargo, amt, stations, false);
		}
	} else {
		switch (_settings_game.economy.town_cargogen_mode) {
			case TCGM_ORIGINAL:
				/* Original (quadratic) cargo generation algorithm */
				if (GB(r, 0, 8) < hs->population) {
					uint amt = GB(r, 0, 8) / 8 + 1;
					TownGenerateCargo(t, CT_PASSENGERS, amt, stations, true);
				}

				if (GB(r, 8, 8) < hs->mail_generation) {
					uint amt = GB(r, 8, 8) / 8 + 1;
					TownGenerateCargo(t, CT_MAIL, amt, stations, true);
				}
				break;

			case TCGM_BITCOUNT:
				/* Binomial distribution per tick, by a series of coin flips */
				/* Reduce generation rate to a 1/4, using tile bits to spread out distribution.
				 * As tick counter is incremented by 256 between each call, we ignore the lower 8 bits. */
				if (GB(_tick_counter, 8, 2) == GB(tile, 0, 2)) {
					/* Make a bitmask with up to 32 bits set, one for each potential pax */
					int genmax = (hs->population + 7) / 8;
					uint32 genmask = (genmax >= 32) ? 0xFFFFFFFF : ((1 << genmax) - 1);
					/* Mask random value by potential pax and count number of actual pax */
					uint amt = CountBits(r & genmask);
					/* Adjust and apply */
					TownGenerateCargo(t, CT_PASSENGERS, amt, stations, true);

					/* Do the same for mail, with a fresh random */
					r = Random();
					genmax = (hs->mail_generation + 7) / 8;
					genmask = (genmax >= 32) ? 0xFFFFFFFF : ((1 << genmax) - 1);
					amt = CountBits(r & genmask);
					TownGenerateCargo(t, CT_MAIL, amt, stations, true);
				}
				break;

			default:
				NOT_REACHED();
		}
	}

	Backup<CompanyID> cur_company(_current_company, OWNER_TOWN, FILE_LINE);

	if ((hs->building_flags & BUILDING_HAS_1_TILE) &&
			HasBit(t->flags, TOWN_IS_GROWING) &&
			CanDeleteHouse(tile) &&
			GetHouseAge(tile) >= hs->minimum_life &&
			--t->time_until_rebuild == 0) {
		t->time_until_rebuild = GB(r, 16, 8) + 192;

		ClearTownHouse(t, tile);

		/* Rebuild with another house? */
		if (GB(r, 24, 8) >= 12) BuildTownHouse(t, tile);
	}

	cur_company.Restore();
}

static CommandCost ClearTile_Town(TileIndex tile, DoCommandFlag flags)
{
	if (flags & DC_AUTO) return_cmd_error(STR_ERROR_BUILDING_MUST_BE_DEMOLISHED);
	if (!CanDeleteHouse(tile)) return CMD_ERROR;

	const HouseSpec *hs = HouseSpec::Get(GetHouseType(tile));

	CommandCost cost(EXPENSES_CONSTRUCTION);
	cost.AddCost(hs->GetRemovalCost());

	int rating = hs->remove_rating_decrease;
	Town *t = Town::GetByTile(tile);

	if (Company::IsValidID(_current_company)) {
		if (rating > t->ratings[_current_company] && !(flags & DC_NO_TEST_TOWN_RATING) && !_cheats.magic_bulldozer.value) {
			SetDParam(0, t->index);
			return_cmd_error(STR_ERROR_LOCAL_AUTHORITY_REFUSES_TO_ALLOW_THIS);
		}
	}

	ChangeTownRating(t, -rating, RATING_HOUSE_MINIMUM, flags);
	if (flags & DC_EXEC) {
		ClearTownHouse(t, tile);
	}

	return cost;
}

void AddProducedHouseCargo(HouseID house_id, TileIndex tile, CargoArray &produced)
{
	const HouseSpec *hs = HouseSpec::Get(house_id);

	if (HasBit(hs->callback_mask, CBM_HOUSE_PRODUCE_CARGO)) {
		Town *t = (tile == INVALID_TILE) ? nullptr : Town::GetByTile(tile);
		for (uint i = 0; i < 256; i++) {
			uint16 callback = GetHouseCallback(CBID_HOUSE_PRODUCE_CARGO, i, 0, house_id, t, tile);

			if (callback == CALLBACK_FAILED || callback == CALLBACK_HOUSEPRODCARGO_END) break;

			CargoID cargo = GetCargoTranslation(GB(callback, 8, 7), hs->grf_prop.grffile);

			if (cargo == CT_INVALID) continue;
			produced[cargo]++;
		}
	} else {
		if (hs->population > 0) {
			produced[CT_PASSENGERS]++;
		}
		if (hs->mail_generation > 0) {
			produced[CT_MAIL]++;
		}
	}
}

static void AddProducedCargo_Town(TileIndex tile, CargoArray &produced)
{
	AddProducedHouseCargo(GetHouseType(tile), tile, produced);
}

static inline void AddAcceptedCargoSetMask(CargoID cargo, uint amount, CargoArray &acceptance, CargoTypes *always_accepted)
{
	if (cargo == CT_INVALID || amount == 0) return;
	acceptance[cargo] += amount;
	SetBit(*always_accepted, cargo);
}

void AddAcceptedHouseCargo(HouseID house_id, TileIndex tile, CargoArray &acceptance, CargoTypes *always_accepted)
{
	const HouseSpec *hs = HouseSpec::Get(house_id);
	Town *t = (tile == INVALID_TILE) ? nullptr : Town::GetByTile(tile);
	CargoID accepts[lengthof(hs->accepts_cargo)];

	/* Set the initial accepted cargo types */
	for (uint8 i = 0; i < lengthof(accepts); i++) {
		accepts[i] = hs->accepts_cargo[i];
	}

	/* Check for custom accepted cargo types */
	if (HasBit(hs->callback_mask, CBM_HOUSE_ACCEPT_CARGO)) {
		uint16 callback = GetHouseCallback(CBID_HOUSE_ACCEPT_CARGO, 0, 0, house_id, t, tile);
		if (callback != CALLBACK_FAILED) {
			/* Replace accepted cargo types with translated values from callback */
			accepts[0] = GetCargoTranslation(GB(callback,  0, 5), hs->grf_prop.grffile);
			accepts[1] = GetCargoTranslation(GB(callback,  5, 5), hs->grf_prop.grffile);
			accepts[2] = GetCargoTranslation(GB(callback, 10, 5), hs->grf_prop.grffile);
		}
	}

	/* Check for custom cargo acceptance */
	if (HasBit(hs->callback_mask, CBM_HOUSE_CARGO_ACCEPTANCE)) {
		uint16 callback = GetHouseCallback(CBID_HOUSE_CARGO_ACCEPTANCE, 0, 0, house_id, t, tile);
		if (callback != CALLBACK_FAILED) {
			AddAcceptedCargoSetMask(accepts[0], GB(callback, 0, 4), acceptance, always_accepted);
			AddAcceptedCargoSetMask(accepts[1], GB(callback, 4, 4), acceptance, always_accepted);
			if (_settings_game.game_creation.landscape != LT_TEMPERATE && HasBit(callback, 12)) {
				/* The 'S' bit indicates food instead of goods */
				AddAcceptedCargoSetMask(CT_FOOD, GB(callback, 8, 4), acceptance, always_accepted);
			} else {
				AddAcceptedCargoSetMask(accepts[2], GB(callback, 8, 4), acceptance, always_accepted);
			}
			return;
		}
	}

	/* No custom acceptance, so fill in with the default values */
	for (uint8 i = 0; i < lengthof(accepts); i++) {
		AddAcceptedCargoSetMask(accepts[i], hs->cargo_acceptance[i], acceptance, always_accepted);
	}
}

static void AddAcceptedCargo_Town(TileIndex tile, CargoArray &acceptance, CargoTypes *always_accepted)
{
	AddAcceptedHouseCargo(GetHouseType(tile), tile, acceptance, always_accepted);
}

static void GetTileDesc_Town(TileIndex tile, TileDesc *td)
{
	const HouseID house = GetHouseType(tile);

	td->str = GetHouseName(house, tile);

	if (!IsHouseCompleted(tile)) {
		SetDParamX(td->dparam, 0, td->str);
		td->str = STR_LAI_TOWN_INDUSTRY_DESCRIPTION_UNDER_CONSTRUCTION;
	}

	const HouseSpec *hs = HouseSpec::Get(house);
	if (hs->grf_prop.grffile != nullptr) {
		const GRFConfig *gc = GetGRFConfig(hs->grf_prop.grffile->grfid);
		td->grf = gc->GetName();
	}

	td->owner[0] = OWNER_TOWN;
}

static TrackStatus GetTileTrackStatus_Town(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	/* not used */
	return 0;
}

static void ChangeTileOwner_Town(TileIndex tile, Owner old_owner, Owner new_owner)
{
	/* not used */
}

/** Update the total cargo acceptance of the whole town.
 * @param t The town to update.
 */
void UpdateTownCargoTotal(Town *t)
{
	t->cargo_accepted_total = 0;

	const TileArea &area = t->cargo_accepted.GetArea();
	TILE_AREA_LOOP_STEP(tile, area, AcceptanceMatrix::GRID) {
		t->cargo_accepted_total |= t->cargo_accepted[tile];
	}
}

/**
 * Update accepted town cargoes around a specific tile.
 * @param t The town to update.
 * @param start Update the values around this tile.
 * @param update_total Set to true if the total cargo acceptance should be updated.
 */
static void UpdateTownCargoesSingleGridArea(Town *t, TileIndex start, bool update_total = true)
{
	CargoArray accepted, produced;
	CargoTypes dummy = 0;

	/* Gather acceptance for all houses in an area around the start tile. */
	TileArea area = AcceptanceMatrix::GetAreaForTile(start, 1);
	TILE_AREA_LOOP(tile, area) {
		if (!IsTileType(tile, MP_HOUSE) || GetTownIndex(tile) != t->index) continue;

		AddAcceptedCargo_Town(tile, accepted, &dummy);
		AddProducedCargo_Town(tile, produced);
	}

	/* Create bitmap of produced and accepted cargoes. */
	CargoTypes acc = 0;
	for (uint cid = 0; cid < NUM_CARGO; cid++) {
		if (accepted[cid] >= 8) SetBit(acc, cid);
		if (produced[cid] > 0) SetBit(t->cargo_produced, cid);
	}
	t->cargo_accepted[start] = acc;

	if (update_total) UpdateTownCargoTotal(t);
}

static void UpdateTownCargoesHouse(Town *t, TileIndex start, bool x_two_tiles, bool y_two_tiles, bool update_total = true)
{
	TileIndex lower = TileAddSaturating(start, -AcceptanceMatrix::GRID, -AcceptanceMatrix::GRID);
	TileIndex upper = TileAddSaturating(start, AcceptanceMatrix::GRID + (x_two_tiles ? 1 : 0), AcceptanceMatrix::GRID + (y_two_tiles ? 1 : 0));
	for (uint x = TileX(lower) & ~(AcceptanceMatrix::GRID - 1); x <= TileX(upper); x += AcceptanceMatrix::GRID) {
		for (uint y = TileY(lower) & ~(AcceptanceMatrix::GRID - 1); y <= TileY(upper); y += AcceptanceMatrix::GRID) {
			UpdateTownCargoesSingleGridArea(t, TileXY(x, y), false);
		}
	}
	if (update_total) UpdateTownCargoTotal(t);
}

/** Update cargo acceptance for the complete town.
 * @param t The town to update.
 */
void UpdateTownCargoes(Town *t)
{
	t->cargo_produced = 0;

	const TileArea &area = t->cargo_accepted.GetArea();
	if (area.tile != INVALID_TILE) {
		/* Update acceptance for each grid square. */
		TILE_AREA_LOOP_STEP(tile, area, AcceptanceMatrix::GRID) {
			UpdateTownCargoesSingleGridArea(t, tile, false);
		}
	}

	/* Update the total acceptance. */
	UpdateTownCargoTotal(t);
}

/** Updates the bitmap of all cargoes accepted by houses. */
void UpdateTownCargoBitmap()
{
	_town_cargoes_accepted = 0;

	for (const Town *town : Town::Iterate()) {
		_town_cargoes_accepted |= town->cargo_accepted_total;
	}
}

static bool GrowTown(Town *t);

static void TownTickHandler(Town *t)
{
	if (HasBit(t->flags, TOWN_IS_GROWING)) {
		int i = (int)t->grow_counter - 1;
		if (i < 0) {
			if (GrowTown(t)) {
				i = t->growth_rate;
			} else {
				/* If growth failed wait a bit before retrying */
				i = min(t->growth_rate, TOWN_GROWTH_TICKS - 1);
			}
		}
		t->grow_counter = i;
	}
}

void OnTick_Town()
{
	if (_game_mode == GM_EDITOR) return;

	for (Town *t : Town::Iterate()) {
		TownTickHandler(t);
	}
}

/**
 * Return the RoadBits of a tile
 *
 * @note There are many other functions doing things like that.
 * @note Needs to be checked for needlessness.
 * @param tile The tile we want to analyse
 * @return The roadbits of the given tile
 */
static RoadBits GetTownRoadBits(TileIndex tile)
{
	if (IsRoadDepotTile(tile) || IsStandardRoadStopTile(tile)) return ROAD_NONE;

	return GetAnyRoadBits(tile, RTT_ROAD, true);
}

RoadType GetTownRoadType(const Town *t)
{
	RoadType best_rt = ROADTYPE_ROAD;
	const RoadTypeInfo *best = nullptr;
	const uint16 assume_max_speed = 50;

	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		if (RoadTypeIsTram(rt)) continue;

		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);

		/* Unused road type. */
		if (rti->label == 0) continue;

		/* Can town build this road. */
		if (!HasBit(rti->flags, ROTF_TOWN_BUILD)) continue;

		/* Not yet introduced at this date. */
		if (IsInsideMM(rti->introduction_date, 0, MAX_DAY) && rti->introduction_date > _date) continue;

		if (best != nullptr) {
			if ((rti->max_speed == 0 ? assume_max_speed : rti->max_speed) < (best->max_speed == 0 ? assume_max_speed : best->max_speed)) continue;
		}

		best_rt = rt;
		best = rti;
	}

	return best_rt;
}

/**
 * Check for parallel road inside a given distance.
 *   Assuming a road from (tile - TileOffsByDiagDir(dir)) to tile,
 *   is there a parallel road left or right of it within distance dist_multi?
 *
 * @param tile current tile
 * @param dir target direction
 * @param dist_multi distance multiplayer
 * @return true if there is a parallel road
 */
static bool IsNeighborRoadTile(TileIndex tile, const DiagDirection dir, uint dist_multi)
{
	if (!IsValidTile(tile)) return false;

	/* Lookup table for the used diff values */
	const TileIndexDiff tid_lt[3] = {
		TileOffsByDiagDir(ChangeDiagDir(dir, DIAGDIRDIFF_90RIGHT)),
		TileOffsByDiagDir(ChangeDiagDir(dir, DIAGDIRDIFF_90LEFT)),
		TileOffsByDiagDir(ReverseDiagDir(dir)),
	};

	dist_multi = (dist_multi + 1) * 4;
	for (uint pos = 4; pos < dist_multi; pos++) {
		/* Go (pos / 4) tiles to the left or the right */
		TileIndexDiff cur = tid_lt[(pos & 1) ? 0 : 1] * (pos / 4);

		/* Use the current tile as origin, or go one tile backwards */
		if (pos & 2) cur += tid_lt[2];

		/* Test for roadbit parallel to dir and facing towards the middle axis */
		if (IsValidTile(tile + cur) &&
				GetTownRoadBits(TILE_ADD(tile, cur)) & DiagDirToRoadBits((pos & 2) ? dir : ReverseDiagDir(dir))) return true;
	}
	return false;
}

/**
 * Check if a Road is allowed on a given tile
 *
 * @param t The current town
 * @param tile The target tile
 * @param dir The direction in which we want to extend the town
 * @return true if it is allowed else false
 */
static bool IsRoadAllowedHere(Town *t, TileIndex tile, DiagDirection dir)
{
	if (DistanceFromEdge(tile) == 0) return false;

	/* Prevent towns from building roads under bridges along the bridge. Looks silly. */
	if (IsBridgeAbove(tile) && GetBridgeAxis(tile) == DiagDirToAxis(dir)) return false;

	/* Check if there already is a road at this point? */
	if (GetTownRoadBits(tile) == ROAD_NONE) {
		/* No, try if we are able to build a road piece there.
		 * If that fails clear the land, and if that fails exit.
		 * This is to make sure that we can build a road here later. */
		RoadType rt = GetTownRoadType(t);
		if (DoCommand(tile, ((dir == DIAGDIR_NW || dir == DIAGDIR_SE) ? ROAD_Y : ROAD_X) | (rt << 4), 0, DC_AUTO, CMD_BUILD_ROAD).Failed() &&
				DoCommand(tile, 0, 0, DC_AUTO, CMD_LANDSCAPE_CLEAR).Failed()) {
			return false;
		}
	}

	Slope cur_slope = _settings_game.construction.build_on_slopes ? GetFoundationSlope(tile) : GetTileSlope(tile);
	bool ret = !IsNeighborRoadTile(tile, dir, t->layout == TL_ORIGINAL ? 1 : 2);
	if (cur_slope == SLOPE_FLAT) return ret;

	/* If the tile is not a slope in the right direction, then
	 * maybe terraform some. */
	Slope desired_slope = (dir == DIAGDIR_NW || dir == DIAGDIR_SE) ? SLOPE_NW : SLOPE_NE;
	if (desired_slope != cur_slope && ComplementSlope(desired_slope) != cur_slope) {
		if (Chance16(1, 8)) {
			CommandCost res = CMD_ERROR;
			if (!_generating_world && Chance16(1, 10)) {
				/* Note: Do not replace "^ SLOPE_ELEVATED" with ComplementSlope(). The slope might be steep. */
				res = DoCommand(tile, Chance16(1, 16) ? cur_slope : cur_slope ^ SLOPE_ELEVATED, 0,
						DC_EXEC | DC_AUTO | DC_NO_WATER, CMD_TERRAFORM_LAND);
			}
			if (res.Failed() && Chance16(1, 3)) {
				/* We can consider building on the slope, though. */
				return ret;
			}
		}
		return false;
	}
	return ret;
}

static bool TerraformTownTile(TileIndex tile, int edges, int dir)
{
	assert(tile < MapSize());

	CommandCost r = DoCommand(tile, edges, dir, DC_AUTO | DC_NO_WATER, CMD_TERRAFORM_LAND);
	if (r.Failed() || r.GetCost() >= (_price[PR_TERRAFORM] + 2) * 8) return false;
	DoCommand(tile, edges, dir, DC_AUTO | DC_NO_WATER | DC_EXEC, CMD_TERRAFORM_LAND);
	return true;
}

static void LevelTownLand(TileIndex tile)
{
	assert(tile < MapSize());

	/* Don't terraform if land is plain or if there's a house there. */
	if (IsTileType(tile, MP_HOUSE)) return;
	Slope tileh = GetTileSlope(tile);
	if (tileh == SLOPE_FLAT) return;

	/* First try up, then down */
	if (!TerraformTownTile(tile, ~tileh & SLOPE_ELEVATED, 1)) {
		TerraformTownTile(tile, tileh & SLOPE_ELEVATED, 0);
	}
}

/**
 * Generate the RoadBits of a grid tile
 *
 * @param t current town
 * @param tile tile in reference to the town
 * @param dir The direction to which we are growing ATM
 * @return the RoadBit of the current tile regarding
 *  the selected town layout
 */
static RoadBits GetTownRoadGridElement(Town *t, TileIndex tile, DiagDirection dir)
{
	/* align the grid to the downtown */
	TileIndexDiffC grid_pos = TileIndexToTileIndexDiffC(t->xy, tile); // Vector from downtown to the tile
	RoadBits rcmd = ROAD_NONE;

	switch (t->layout) {
		default: NOT_REACHED();

		case TL_2X2_GRID:
			if ((grid_pos.x % 3) == 0) rcmd |= ROAD_Y;
			if ((grid_pos.y % 3) == 0) rcmd |= ROAD_X;
			break;

		case TL_3X3_GRID:
			if ((grid_pos.x % 4) == 0) rcmd |= ROAD_Y;
			if ((grid_pos.y % 4) == 0) rcmd |= ROAD_X;
			break;
	}

	/* Optimise only X-junctions */
	if (rcmd != ROAD_ALL) return rcmd;

	RoadBits rb_template;

	switch (GetTileSlope(tile)) {
		default:       rb_template = ROAD_ALL; break;
		case SLOPE_W:  rb_template = ROAD_NW | ROAD_SW; break;
		case SLOPE_SW: rb_template = ROAD_Y  | ROAD_SW; break;
		case SLOPE_S:  rb_template = ROAD_SW | ROAD_SE; break;
		case SLOPE_SE: rb_template = ROAD_X  | ROAD_SE; break;
		case SLOPE_E:  rb_template = ROAD_SE | ROAD_NE; break;
		case SLOPE_NE: rb_template = ROAD_Y  | ROAD_NE; break;
		case SLOPE_N:  rb_template = ROAD_NE | ROAD_NW; break;
		case SLOPE_NW: rb_template = ROAD_X  | ROAD_NW; break;
		case SLOPE_STEEP_W:
		case SLOPE_STEEP_S:
		case SLOPE_STEEP_E:
		case SLOPE_STEEP_N:
			rb_template = ROAD_NONE;
			break;
	}

	/* Stop if the template is compatible to the growth dir */
	if (DiagDirToRoadBits(ReverseDiagDir(dir)) & rb_template) return rb_template;
	/* If not generate a straight road in the direction of the growth */
	return DiagDirToRoadBits(dir) | DiagDirToRoadBits(ReverseDiagDir(dir));
}

/**
 * Grows the town with an extra house.
 *  Check if there are enough neighbor house tiles
 *  next to the current tile. If there are enough
 *  add another house.
 *
 * @param t The current town
 * @param tile The target tile for the extra house
 * @return true if an extra house has been added
 */
static bool GrowTownWithExtraHouse(Town *t, TileIndex tile)
{
	/* We can't look further than that. */
	if (DistanceFromEdge(tile) == 0) return false;

	uint counter = 0; // counts the house neighbor tiles

	/* Check the tiles E,N,W and S of the current tile for houses */
	for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
		/* Count both void and house tiles for checking whether there
		 * are enough houses in the area. This to make it likely that
		 * houses get build up to the edge of the map. */
		switch (GetTileType(TileAddByDiagDir(tile, dir))) {
			case MP_HOUSE:
			case MP_VOID:
				counter++;
				break;

			default:
				break;
		}

		/* If there are enough neighbors stop here */
		if (counter >= 3) {
			if (BuildTownHouse(t, tile)) {
				_grow_town_result = GROWTH_SUCCEED;
				return true;
			}
			return false;
		}
	}
	return false;
}

/**
 * Grows the town with a road piece.
 *
 * @param t The current town
 * @param tile The current tile
 * @param rcmd The RoadBits we want to build on the tile
 * @return true if the RoadBits have been added else false
 */
static bool GrowTownWithRoad(const Town *t, TileIndex tile, RoadBits rcmd)
{
	RoadType rt = GetTownRoadType(t);
	if (DoCommand(tile, rcmd | (rt << 4), t->index, DC_EXEC | DC_AUTO | DC_NO_WATER, CMD_BUILD_ROAD).Succeeded()) {
		_grow_town_result = GROWTH_SUCCEED;
		return true;
	}
	return false;
}

/**
 * Grows the town with a bridge.
 *  At first we check if a bridge is reasonable.
 *  If so we check if we are able to build it.
 *
 * @param t The current town
 * @param tile The current tile
 * @param bridge_dir The valid direction in which to grow a bridge
 * @return true if a bridge has been build else false
 */
static bool GrowTownWithBridge(const Town *t, const TileIndex tile, const DiagDirection bridge_dir)
{
	assert(bridge_dir < DIAGDIR_END);

	const Slope slope = GetTileSlope(tile);

	/* Make sure the direction is compatible with the slope.
	 * Well we check if the slope has an up bit set in the
	 * reverse direction. */
	if (slope != SLOPE_FLAT && slope & InclinedSlope(bridge_dir)) return false;

	/* Assure that the bridge is connectable to the start side */
	if (!(GetTownRoadBits(TileAddByDiagDir(tile, ReverseDiagDir(bridge_dir))) & DiagDirToRoadBits(bridge_dir))) return false;

	/* We are in the right direction */
	uint8 bridge_length = 0;      // This value stores the length of the possible bridge
	TileIndex bridge_tile = tile; // Used to store the other waterside

	const int delta = TileOffsByDiagDir(bridge_dir);

	if (slope == SLOPE_FLAT) {
		/* Bridges starting on flat tiles are only allowed when crossing rivers, rails or one-way roads. */
		do {
			if (bridge_length++ >= 4) {
				/* Allow to cross rivers, not big lakes, nor large amounts of rails or one-way roads. */
				return false;
			}
			bridge_tile += delta;
		} while (IsValidTile(bridge_tile) && ((IsWaterTile(bridge_tile) && !IsSea(bridge_tile)) || IsPlainRailTile(bridge_tile) || (IsNormalRoadTile(bridge_tile) && GetDisallowedRoadDirections(bridge_tile) != DRD_NONE)));
	} else {
		do {
			if (bridge_length++ >= 11) {
				/* Max 11 tile long bridges */
				return false;
			}
			bridge_tile += delta;
		} while (IsValidTile(bridge_tile) && (IsWaterTile(bridge_tile) || IsPlainRailTile(bridge_tile) || (IsNormalRoadTile(bridge_tile) && GetDisallowedRoadDirections(bridge_tile) != DRD_NONE)));
	}

	/* no water tiles in between? */
	if (bridge_length == 1) return false;

	std::bitset <MAX_BRIDGES> tried;
	uint n = MAX_BRIDGES;
	byte bridge_type = RandomRange (n);

	for (;;) {
		/* Can we actually build the bridge? */
		RoadType rt = GetTownRoadType(t);
		if (DoCommand(tile, bridge_tile, bridge_type | rt << 8 | TRANSPORT_ROAD << 15, CommandFlagsToDCFlags(GetCommandFlags(CMD_BUILD_BRIDGE)), CMD_BUILD_BRIDGE).Succeeded()) {
			DoCommand(tile, bridge_tile, bridge_type | rt << 8 | TRANSPORT_ROAD << 15, DC_EXEC | CommandFlagsToDCFlags(GetCommandFlags(CMD_BUILD_BRIDGE)), CMD_BUILD_BRIDGE);
			_grow_town_result = GROWTH_SUCCEED;
			return true;
		}

		/* Try a different bridge. */
		tried[bridge_type] = true;
		n--;
		assert (n + tried.count() == MAX_BRIDGES);
		if (n == 0) break;

		bridge_type = 0;
		uint i = RandomRange (n);
		while (tried[bridge_type] || (i-- > 0)) {
			bridge_type++;
			assert (bridge_type < MAX_BRIDGES);
		}
	}

	/* Quit if no bridge can be built. */
	return false;
}


/**
 * Checks whether at least one surrounding roads allows to build a house here
 *
 * @param t the tile where the house will be built
 * @return true if at least one surrounding roadtype allows building houses here
 */
static inline bool RoadTypesAllowHouseHere(TileIndex t)
{
	static const TileIndexDiffC tiles[] = { {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1} };
	bool allow = false;

	for (const TileIndexDiffC *ptr = tiles; ptr != endof(tiles); ++ptr) {
		TileIndex cur_tile = t + ToTileIndexDiff(*ptr);
		if (!IsValidTile(cur_tile)) continue;

		if (!(IsTileType(cur_tile, MP_ROAD) || IsTileType(cur_tile, MP_STATION))) continue;
		allow = true;

		RoadType road_rt = GetRoadTypeRoad(cur_tile);
		RoadType tram_rt = GetRoadTypeTram(cur_tile);
		if (road_rt != INVALID_ROADTYPE && !HasBit(GetRoadTypeInfo(road_rt)->flags, ROTF_NO_HOUSES)) return true;
		if (tram_rt != INVALID_ROADTYPE && !HasBit(GetRoadTypeInfo(tram_rt)->flags, ROTF_NO_HOUSES)) return true;
	}

	/* If no road was found surrounding the tile we can allow building the house since there is
	 * nothing which forbids it, if a road was found but the execution reached this point, then
	 * all the found roads don't allow houses to be built */
	return !allow;
}

/**
 * Grows the given town.
 * There are at the moment 3 possible way's for
 * the town expansion:
 *  @li Generate a random tile and check if there is a road allowed
 *  @li TL_ORIGINAL
 *  @li TL_BETTER_ROADS
 *  @li Check if the town geometry allows a road and which one
 *  @li TL_2X2_GRID
 *  @li TL_3X3_GRID
 *  @li Forbid roads, only build houses
 *
 * @param tile_ptr The current tile
 * @param cur_rb The current tiles RoadBits
 * @param target_dir The target road dir
 * @param t1 The current town
 */
static void GrowTownInTile(TileIndex *tile_ptr, RoadBits cur_rb, DiagDirection target_dir, Town *t1)
{
	RoadBits rcmd = ROAD_NONE;  // RoadBits for the road construction command
	TileIndex tile = *tile_ptr; // The main tile on which we base our growth

	assert(tile < MapSize());

	if (cur_rb == ROAD_NONE) {
		/* Tile has no road. First reset the status counter
		 * to say that this is the last iteration. */
		_grow_town_result = GROWTH_SEARCH_STOPPED;

		if (!_settings_game.economy.allow_town_roads && !_generating_world) return;
		if (!_settings_game.economy.allow_town_level_crossings && IsTileType(tile, MP_RAILWAY)) return;

		/* Remove hills etc */
		if (!_settings_game.construction.build_on_slopes || Chance16(1, 6)) LevelTownLand(tile);

		/* Is a road allowed here? */
		switch (t1->layout) {
			default: NOT_REACHED();

			case TL_3X3_GRID:
			case TL_2X2_GRID:
				rcmd = GetTownRoadGridElement(t1, tile, target_dir);
				if (rcmd == ROAD_NONE) return;
				break;

			case TL_BETTER_ROADS:
			case TL_ORIGINAL:
				if (!IsRoadAllowedHere(t1, tile, target_dir)) return;

				DiagDirection source_dir = ReverseDiagDir(target_dir);

				if (Chance16(1, 4)) {
					/* Randomize a new target dir */
					do target_dir = RandomDiagDir(); while (target_dir == source_dir);
				}

				if (!IsRoadAllowedHere(t1, TileAddByDiagDir(tile, target_dir), target_dir)) {
					/* A road is not allowed to continue the randomized road,
					 *  return if the road we're trying to build is curved. */
					if (target_dir != ReverseDiagDir(source_dir)) return;

					/* Return if neither side of the new road is a house */
					if (!IsTileType(TileAddByDiagDir(tile, ChangeDiagDir(target_dir, DIAGDIRDIFF_90RIGHT)), MP_HOUSE) &&
							!IsTileType(TileAddByDiagDir(tile, ChangeDiagDir(target_dir, DIAGDIRDIFF_90LEFT)), MP_HOUSE)) {
						return;
					}

					/* That means that the road is only allowed if there is a house
					 *  at any side of the new road. */
				}

				rcmd = DiagDirToRoadBits(target_dir) | DiagDirToRoadBits(source_dir);
				break;
		}

	} else if (target_dir < DIAGDIR_END && !(cur_rb & DiagDirToRoadBits(ReverseDiagDir(target_dir)))) {
		/* Continue building on a partial road.
		 * Should be always OK, so we only generate
		 * the fitting RoadBits */
		_grow_town_result = GROWTH_SEARCH_STOPPED;

		if (!_settings_game.economy.allow_town_roads && !_generating_world) return;

		switch (t1->layout) {
			default: NOT_REACHED();

			case TL_3X3_GRID:
			case TL_2X2_GRID:
				rcmd = GetTownRoadGridElement(t1, tile, target_dir);
				break;

			case TL_BETTER_ROADS:
			case TL_ORIGINAL:
				rcmd = DiagDirToRoadBits(ReverseDiagDir(target_dir));
				break;
		}
	} else {
		bool allow_house = true; // Value which decides if we want to construct a house

		/* Reached a tunnel/bridge? Then continue at the other side of it, unless
		 * it is the starting tile. Half the time, we stay on this side then.
		 * For custom bridge heads decide whether or not to cross depending on the available
		 * head road bits. */
		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			if (IsRoadCustomBridgeHeadTile(tile)) {
				if (target_dir != DIAGDIR_END) {
					/* don't go back to the source direction */
					cur_rb &= ~DiagDirToRoadBits(ReverseDiagDir(target_dir));
				}

				/* randomly pick a usable head road bit */
				do {
					if (cur_rb == ROAD_NONE) return;
					RoadBits target_bits;
					do {
						target_dir = RandomDiagDir();
						target_bits = DiagDirToRoadBits(target_dir);
					} while (!(cur_rb & target_bits));
					cur_rb &= ~target_bits;
				} while (!(target_dir == GetTunnelBridgeDirection(tile) || CanFollowRoad(tile, target_dir)));
				if (target_dir == GetTunnelBridgeDirection(tile)) {
					/* cross the bridge */
					*tile_ptr = GetOtherTunnelBridgeEnd(tile);
				}
			} else if (GetTunnelBridgeTransportType(tile) == TRANSPORT_ROAD && (target_dir != DIAGDIR_END || Chance16(1, 2))) {
				*tile_ptr = GetOtherTunnelBridgeEnd(tile);
			}
			return;
		}

		/* Possibly extend the road in a direction.
		 * Randomize a direction and if it has a road, bail out. */
		target_dir = RandomDiagDir();
		RoadBits target_rb = DiagDirToRoadBits(target_dir);
		TileIndex house_tile; // position of a possible house

		if (cur_rb & target_rb) {
			/* If it's a road turn possibly build a house in a corner.
			 * Use intersection with straight road as an indicator
			 * that we randomed corner house position.
			 * A turn (and we check for that later) always has only
			 * one common bit with a straight road so it has the same
			 * chance to be chosen as the house on the side of a road.
			 */
			if ((cur_rb & ROAD_X) != target_rb) return;

			/* Check whether it is a turn and if so determine
			 * position of the corner tile */
			switch (cur_rb) {
				case ROAD_N:
					house_tile = TileAddByDir(tile, DIR_S);
					break;
				case ROAD_S:
					house_tile = TileAddByDir(tile, DIR_N);
					break;
				case ROAD_E:
					house_tile = TileAddByDir(tile, DIR_W);
					break;
				case ROAD_W:
					house_tile = TileAddByDir(tile, DIR_E);
					break;
				default:
					return;  // not a turn
			}
			target_dir = DIAGDIR_END;
		} else {
			house_tile = TileAddByDiagDir(tile, target_dir);
		}

		/* Don't walk into water. */
		if (HasTileWaterGround(house_tile)) return;

		if (!IsValidTile(house_tile)) return;

		if (target_dir != DIAGDIR_END && (_settings_game.economy.allow_town_roads || _generating_world)) {
			switch (t1->layout) {
				default: NOT_REACHED();

				case TL_3X3_GRID: // Use 2x2 grid afterwards!
					GrowTownWithExtraHouse(t1, TileAddByDiagDir(house_tile, target_dir));
					FALLTHROUGH;

				case TL_2X2_GRID:
					rcmd = GetTownRoadGridElement(t1, tile, target_dir);
					allow_house = (rcmd & target_rb) == ROAD_NONE;
					break;

				case TL_BETTER_ROADS: // Use original afterwards!
					GrowTownWithExtraHouse(t1, TileAddByDiagDir(house_tile, target_dir));
					FALLTHROUGH;

				case TL_ORIGINAL:
					/* Allow a house at the edge. 60% chance or
					 * always ok if no road allowed. */
					rcmd = target_rb;
					allow_house = (!IsRoadAllowedHere(t1, house_tile, target_dir) || Chance16(6, 10));
					break;
			}
		}

		allow_house &= RoadTypesAllowHouseHere(house_tile);

		if (allow_house) {
			/* Build a house, but not if there already is a house there. */
			if (!IsTileType(house_tile, MP_HOUSE)) {
				/* Level the land if possible */
				if (Chance16(1, 6)) LevelTownLand(house_tile);

				/* And build a house.
				 * Set result to -1 if we managed to build it. */
				if (BuildTownHouse(t1, house_tile)) {
					_grow_town_result = GROWTH_SUCCEED;
				}
			}
			return;
		}

		_grow_town_result = GROWTH_SEARCH_STOPPED;
	}

	/* Return if a water tile */
	if (HasTileWaterGround(tile)) return;

	/* Make the roads look nicer */
	rcmd = CleanUpRoadBits(tile, rcmd);
	if (rcmd == ROAD_NONE) return;

	/* Only use the target direction for bridges to ensure they're connected.
	 * The target_dir is as computed previously according to town layout, so
	 * it will match it perfectly. */
	if (GrowTownWithBridge(t1, tile, target_dir)) return;

	GrowTownWithRoad(t1, tile, rcmd);
}

/**
 * Checks whether a road can be followed or is a dead end, that can not be extended to the next tile.
 * This only checks trivial but often cases.
 * @param tile Start tile for road.
 * @param dir Direction for road to follow or build.
 * @return true If road is or can be connected in the specified direction.
 */
static bool CanFollowRoad(TileIndex tile, DiagDirection dir)
{
	TileIndex target_tile = tile + TileOffsByDiagDir(dir);
	if (!IsValidTile(target_tile)) return false;
	if (HasTileWaterGround(target_tile)) return false;

	RoadBits target_rb = GetTownRoadBits(target_tile);
	if (_settings_game.economy.allow_town_roads || _generating_world) {
		/* Check whether a road connection exists or can be build. */
		switch (GetTileType(target_tile)) {
			case MP_ROAD:
				return target_rb != ROAD_NONE;

			case MP_STATION:
				return IsDriveThroughStopTile(target_tile);

			case MP_TUNNELBRIDGE:
				return GetTunnelBridgeTransportType(target_tile) == TRANSPORT_ROAD;

			case MP_HOUSE:
			case MP_INDUSTRY:
			case MP_OBJECT:
				return false;

			default:
				/* Checked for void and water earlier */
				return true;
		}
	} else {
		/* Check whether a road connection already exists,
		 * and it leads somewhere else. */
		RoadBits back_rb = DiagDirToRoadBits(ReverseDiagDir(dir));
		return (target_rb & back_rb) != 0 && (target_rb & ~back_rb) != 0;
	}
}

/**
 * Returns "growth" if a house was built, or no if the build failed.
 * @param t town to inquiry
 * @param tile to inquiry
 * @return true if town expansion was possible
 */
static bool GrowTownAtRoad(Town *t, TileIndex tile)
{
	/* Special case.
	 * @see GrowTownInTile Check the else if
	 */
	DiagDirection target_dir = DIAGDIR_END; // The direction in which we want to extend the town

	assert(tile < MapSize());

	/* Number of times to search.
	 * Better roads, 2X2 and 3X3 grid grow quite fast so we give
	 * them a little handicap. */
	switch (t->layout) {
		case TL_BETTER_ROADS:
			_grow_town_result = 10 + t->cache.num_houses * 2 / 9;
			break;

		case TL_3X3_GRID:
		case TL_2X2_GRID:
			_grow_town_result = 10 + t->cache.num_houses * 1 / 9;
			break;

		default:
			_grow_town_result = 10 + t->cache.num_houses * 4 / 9;
			break;
	}

	do {
		RoadBits cur_rb = GetTownRoadBits(tile); // The RoadBits of the current tile

		TileIndex orig_tile = tile;

		/* Try to grow the town from this point */
		GrowTownInTile(&tile, cur_rb, target_dir, t);
		if (_grow_town_result == GROWTH_SUCCEED) return true;

		if (orig_tile == tile) {
			/* Exclude the source position from the bitmask
			 * and return if no more road blocks available */
			if (IsValidDiagDirection(target_dir)) cur_rb &= ~DiagDirToRoadBits(ReverseDiagDir(target_dir));
		} else {
			/* Crossed bridge/tunnel, no need to mask bits */
			cur_rb = GetTownRoadBits(tile);
		}
		if (cur_rb == ROAD_NONE) return false;

		const bool custom_bridge_head = IsRoadCustomBridgeHeadTile(tile);
		if (IsTileType(tile, MP_TUNNELBRIDGE) && !custom_bridge_head) {
			/* Only build in the direction away from the tunnel or bridge. */
			target_dir = ReverseDiagDir(GetTunnelBridgeDirection(tile));
		} else {
			if (custom_bridge_head) {
				/* Do not build into the bridge */
				cur_rb &= ~DiagDirToRoadBits(GetTunnelBridgeDirection(tile));
			}
			/* Select a random bit from the blockmask, walk a step
			 * and continue the search from there. */
			do {
				if (cur_rb == ROAD_NONE) return false;
				RoadBits target_bits;
				do {
					target_dir = RandomDiagDir();
					target_bits = DiagDirToRoadBits(target_dir);
				} while (!(cur_rb & target_bits));
				cur_rb &= ~target_bits;
			} while (!CanFollowRoad(tile, target_dir));
		}
		tile = TileAddByDiagDir(tile, target_dir);

		if (IsTileType(tile, MP_ROAD) && !IsRoadDepot(tile) && HasTileRoadType(tile, RTT_ROAD)) {
			/* Don't allow building over roads of other cities */
			if (IsRoadOwner(tile, RTT_ROAD, OWNER_TOWN) && Town::GetByTile(tile) != t) {
				return false;
			} else if (IsRoadOwner(tile, RTT_ROAD, OWNER_NONE) && _game_mode == GM_EDITOR) {
				/* If we are in the SE, and this road-piece has no town owner yet, it just found an
				 * owner :) (happy happy happy road now) */
				SetRoadOwner(tile, RTT_ROAD, OWNER_TOWN);
				SetTownIndex(tile, t->index);
			}
		}

		/* Max number of times is checked. */
	} while (--_grow_town_result >= 0);

	return false;
}

/**
 * Generate a random road block.
 * The probability of a straight road
 * is somewhat higher than a curved.
 *
 * @return A RoadBits value with 2 bits set
 */
static RoadBits GenRandomRoadBits()
{
	uint32 r = Random();
	uint a = GB(r, 0, 2);
	uint b = GB(r, 8, 2);
	if (a == b) b ^= 2;
	return (RoadBits)((ROAD_NW << a) + (ROAD_NW << b));
}

/**
 * Grow the town
 * @param t town to grow
 * @return true iff something (house, road, bridge, ...) was built
 */
static bool GrowTown(Town *t)
{
	static const TileIndexDiffC _town_coord_mod[] = {
		{-1,  0},
		{ 1,  1},
		{ 1, -1},
		{-1, -1},
		{-1,  0},
		{ 0,  2},
		{ 2,  0},
		{ 0, -2},
		{-1, -1},
		{-2,  2},
		{ 2,  2},
		{ 2, -2},
		{ 0,  0}
	};

	/* Current "company" is a town */
	Backup<CompanyID> cur_company(_current_company, OWNER_TOWN, FILE_LINE);

	TileIndex tile = t->xy; // The tile we are working with ATM

	/* Find a road that we can base the construction on. */
	const TileIndexDiffC *ptr;
	for (ptr = _town_coord_mod; ptr != endof(_town_coord_mod); ++ptr) {
		if (GetTownRoadBits(tile) != ROAD_NONE) {
			bool success = GrowTownAtRoad(t, tile);
			cur_company.Restore();
			return success;
		}
		tile = TILE_ADD(tile, ToTileIndexDiff(*ptr));
	}

	/* No road available, try to build a random road block by
	 * clearing some land and then building a road there. */
	if (_settings_game.economy.allow_town_roads || _generating_world) {
		tile = t->xy;
		for (ptr = _town_coord_mod; ptr != endof(_town_coord_mod); ++ptr) {
			/* Only work with plain land that not already has a house */
			if (!IsTileType(tile, MP_HOUSE) && IsTileFlat(tile)) {
				if (DoCommand(tile, 0, 0, DC_AUTO | DC_NO_WATER, CMD_LANDSCAPE_CLEAR).Succeeded()) {
					RoadType rt = GetTownRoadType(t);
					DoCommand(tile, GenRandomRoadBits() | (rt << 4), t->index, DC_EXEC | DC_AUTO, CMD_BUILD_ROAD);
					cur_company.Restore();
					return true;
				}
			}
			tile = TILE_ADD(tile, ToTileIndexDiff(*ptr));
		}
	}

	cur_company.Restore();
	return false;
}

void UpdateTownRadius(Town *t)
{
	static const uint32 _town_squared_town_zone_radius_data[23][5] = {
		{  4,  0,  0,  0,  0}, // 0
		{ 16,  0,  0,  0,  0},
		{ 25,  0,  0,  0,  0},
		{ 36,  0,  0,  0,  0},
		{ 49,  0,  4,  0,  0},
		{ 64,  0,  4,  0,  0}, // 20
		{ 64,  0,  9,  0,  1},
		{ 64,  0,  9,  0,  4},
		{ 64,  0, 16,  0,  4},
		{ 81,  0, 16,  0,  4},
		{ 81,  0, 16,  0,  4}, // 40
		{ 81,  0, 25,  0,  9},
		{ 81, 36, 25,  0,  9},
		{ 81, 36, 25, 16,  9},
		{ 81, 49,  0, 25,  9},
		{ 81, 64,  0, 25,  9}, // 60
		{ 81, 64,  0, 36,  9},
		{ 81, 64,  0, 36, 16},
		{100, 81,  0, 49, 16},
		{100, 81,  0, 49, 25},
		{121, 81,  0, 49, 25}, // 80
		{121, 81,  0, 49, 25},
		{121, 81,  0, 49, 36}, // 88
	};

	if (t->cache.num_houses < 92) {
		memcpy(t->cache.squared_town_zone_radius, _town_squared_town_zone_radius_data[t->cache.num_houses / 4], sizeof(t->cache.squared_town_zone_radius));
	} else {
		int mass = t->cache.num_houses / 8;
		/* Actually we are proportional to sqrt() but that's right because we are covering an area.
		 * The offsets are to make sure the radii do not decrease in size when going from the table
		 * to the calculated value.*/
		t->cache.squared_town_zone_radius[0] = mass * 15 - 40;
		t->cache.squared_town_zone_radius[1] = mass * 9 - 15;
		t->cache.squared_town_zone_radius[2] = 0;
		t->cache.squared_town_zone_radius[3] = mass * 5 - 5;
		t->cache.squared_town_zone_radius[4] = mass * 3 + 5;
	}
}

void UpdateTownMaxPass(Town *t)
{
	t->supplied[CT_PASSENGERS].old_max = t->cache.population >> 3;
	t->supplied[CT_MAIL].old_max = t->cache.population >> 4;
}

static void UpdateTownGrowthRate(Town *t);
static void UpdateTownGrowth(Town *t);

/**
 * Does the actual town creation.
 *
 * @param t The town
 * @param tile Where to put it
 * @param townnameparts The town name
 * @param size Parameter for size determination
 * @param city whether to build a city or town
 * @param layout the (road) layout of the town
 * @param manual was the town placed manually?
 */
static void DoCreateTown(Town *t, TileIndex tile, uint32 townnameparts, TownSize size, bool city, TownLayout layout, bool manual)
{
	t->xy = tile;
	t->cache.num_houses = 0;
	t->time_until_rebuild = 10;
	UpdateTownRadius(t);
	t->flags = 0;
	t->cache.population = 0;
	/* Spread growth across ticks so even if there are many
	 * similar towns they're unlikely to grow all in one tick */
	t->grow_counter = t->index % TOWN_GROWTH_TICKS;
	t->growth_rate = TownTicksToGameTicks(250);
	t->show_zone = false;

	_town_kdtree.Insert(t->index);

	/* Set the default cargo requirement for town growth */
	switch (_settings_game.game_creation.landscape) {
		case LT_ARCTIC:
			if (FindFirstCargoWithTownEffect(TE_FOOD) != nullptr) t->goal[TE_FOOD] = TOWN_GROWTH_WINTER;
			break;

		case LT_TROPIC:
			if (FindFirstCargoWithTownEffect(TE_FOOD) != nullptr) t->goal[TE_FOOD] = TOWN_GROWTH_DESERT;
			if (FindFirstCargoWithTownEffect(TE_WATER) != nullptr) t->goal[TE_WATER] = TOWN_GROWTH_DESERT;
			break;
	}

	t->fund_buildings_months = 0;

	for (uint i = 0; i != MAX_COMPANIES; i++) t->ratings[i] = RATING_INITIAL;

	t->have_ratings = 0;
	t->exclusivity = INVALID_COMPANY;
	t->exclusive_counter = 0;
	t->statues = 0;

	extern int _nb_orig_names;
	if (_settings_game.game_creation.town_name < _nb_orig_names) {
		/* Original town name */
		t->townnamegrfid = 0;
		t->townnametype = SPECSTR_TOWNNAME_START + _settings_game.game_creation.town_name;
	} else {
		/* Newgrf town name */
		t->townnamegrfid = GetGRFTownNameId(_settings_game.game_creation.town_name  - _nb_orig_names);
		t->townnametype  = GetGRFTownNameType(_settings_game.game_creation.town_name - _nb_orig_names);
	}
	t->townnameparts = townnameparts;

	t->UpdateVirtCoord();
	InvalidateWindowData(WC_TOWN_DIRECTORY, 0, TDIWD_FORCE_REBUILD);

	t->InitializeLayout(layout);

	t->larger_town = city;

	int x = (int)size * 16 + 3;
	if (size == TSZ_RANDOM) x = (Random() & 0xF) + 8;
	/* Don't create huge cities when founding town in-game */
	if (city && (!manual || _game_mode == GM_EDITOR)) x *= _settings_game.economy.initial_city_size;

	t->cache.num_houses += x;
	UpdateTownRadius(t);

	int i = x * 4;
	do {
		GrowTown(t);
	} while (--i);

	t->cache.num_houses -= x;
	UpdateTownRadius(t);
	UpdateTownGrowthRate(t);
	UpdateTownMaxPass(t);
	UpdateAirportsNoise();
}

/**
 * Checks if it's possible to place a town at given tile
 * @param tile tile to check
 * @return error value or zero cost
 */
static CommandCost TownCanBePlacedHere(TileIndex tile)
{
	/* Check if too close to the edge of map */
	if (DistanceFromEdge(tile) < 12) {
		return_cmd_error(STR_ERROR_TOO_CLOSE_TO_EDGE_OF_MAP_SUB);
	}

	/* Check distance to all other towns. */
	if (IsCloseToTown(tile, _settings_game.economy.town_min_distance)) {
		return_cmd_error(STR_ERROR_TOO_CLOSE_TO_ANOTHER_TOWN);
	}

	/* Can only build on clear flat areas, possibly with trees. */
	if ((!IsTileType(tile, MP_CLEAR) && !IsTileType(tile, MP_TREES)) || !IsTileFlat(tile)) {
		return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
	}

	return CommandCost(EXPENSES_OTHER);
}

/**
 * Verifies this custom name is unique. Only custom names are checked.
 * @param name name to check
 * @return is this name unique?
 */
static bool IsUniqueTownName(const char *name)
{
	for (const Town *t : Town::Iterate()) {
		if (!t->name.empty() && t->name == name) return false;
	}

	return true;
}

/**
 * Create a new town.
 * @param tile coordinates where town is built
 * @param flags type of operation
 * @param p1  0..1 size of the town (@see TownSize)
 *               2 true iff it should be a city
 *            3..5 town road layout (@see TownLayout)
 *               6 use random location (randomize \c tile )
 * @param p2 town name parts
 * @param text Custom name for the town. If empty, the town name parts will be used.
 * @return the cost of this operation or an error
 */
CommandCost CmdFoundTown(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	TownSize size = Extract<TownSize, 0, 2>(p1);
	bool city = HasBit(p1, 2);
	TownLayout layout = Extract<TownLayout, 3, 3>(p1);
	TownNameParams par(_settings_game.game_creation.town_name);
	bool random = HasBit(p1, 6);
	uint32 townnameparts = p2;

	if (size >= TSZ_END) return CMD_ERROR;
	if (layout >= NUM_TLS) return CMD_ERROR;

	/* Some things are allowed only in the scenario editor and for game scripts. */
	if (_game_mode != GM_EDITOR && _current_company != OWNER_DEITY) {
		if (_settings_game.economy.found_town == TF_FORBIDDEN) return CMD_ERROR;
		if (size == TSZ_LARGE) return CMD_ERROR;
		if (random) return CMD_ERROR;
		if (_settings_game.economy.found_town != TF_CUSTOM_LAYOUT && layout != _settings_game.economy.town_layout) {
			return CMD_ERROR;
		}
	} else if (_current_company == OWNER_DEITY && random) {
		/* Random parameter is not allowed for Game Scripts. */
		return CMD_ERROR;
	}

	if (StrEmpty(text)) {
		/* If supplied name is empty, townnameparts has to generate unique automatic name */
		if (!VerifyTownName(townnameparts, &par)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);
	} else {
		/* If name is not empty, it has to be unique custom name */
		if (Utf8StringLength(text) >= MAX_LENGTH_TOWN_NAME_CHARS) return CMD_ERROR;
		if (!IsUniqueTownName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	/* Allocate town struct */
	if (!Town::CanAllocateItem()) return_cmd_error(STR_ERROR_TOO_MANY_TOWNS);

	if (!random) {
		CommandCost ret = TownCanBePlacedHere(tile);
		if (ret.Failed()) return ret;
	}

	static const byte price_mult[][TSZ_RANDOM + 1] = {{ 15, 25, 40, 25 }, { 20, 35, 55, 35 }};
	/* multidimensional arrays have to have defined length of non-first dimension */
	assert_compile(lengthof(price_mult[0]) == 4);

	CommandCost cost(EXPENSES_OTHER, _price[PR_BUILD_TOWN]);
	byte mult = price_mult[city][size];

	cost.MultiplyCost(mult);

	/* Create the town */
	if (flags & DC_EXEC) {
		if (cost.GetCost() > GetAvailableMoneyForCommand()) {
			_additional_cash_required = cost.GetCost();
			return CommandCost(EXPENSES_OTHER);
		}

		Backup<bool> old_generating_world(_generating_world, true, FILE_LINE);
		UpdateNearestTownForRoadTiles(true);
		Town *t;
		if (random) {
			t = CreateRandomTown(20, townnameparts, size, city, layout);
			if (t == nullptr) {
				cost = CommandCost(STR_ERROR_NO_SPACE_FOR_TOWN);
			} else {
				_new_town_id = t->index;
			}
		} else {
			t = new Town(tile);
			DoCreateTown(t, tile, townnameparts, size, city, layout, true);
		}
		UpdateNearestTownForRoadTiles(false);
		old_generating_world.Restore();

		if (t != nullptr && !StrEmpty(text)) {
			t->name = stredup(text);
			t->UpdateVirtCoord();
		}

		if (_game_mode != GM_EDITOR) {
			/* 't' can't be nullptr since 'random' is false outside scenedit */
			assert(!random);

			UpdateTownCargoBitmap();

			if (_current_company == OWNER_DEITY) {
				SetDParam(0, t->index);
				AddTileNewsItem(STR_NEWS_NEW_TOWN_UNSPONSORED, NT_INDUSTRY_OPEN, tile);
			} else {
				char company_name[MAX_LENGTH_COMPANY_NAME_CHARS * MAX_CHAR_LENGTH];
				SetDParam(0, _current_company);
				GetString(company_name, STR_COMPANY_NAME, lastof(company_name));

				char *cn = stredup(company_name);
				SetDParamStr(0, cn);
				SetDParam(1, t->index);

				AddTileNewsItem(STR_NEWS_NEW_TOWN, NT_INDUSTRY_OPEN, tile, cn);
			}
			AI::BroadcastNewEvent(new ScriptEventTownFounded(t->index));
			Game::NewEvent(new ScriptEventTownFounded(t->index));
		}
	}
	return cost;
}

/**
 * Towns must all be placed on the same grid or when they eventually
 * interpenetrate their road networks will not mesh nicely; this
 * function adjusts a tile so that it aligns properly.
 *
 * @param tile the tile to start at
 * @param layout which town layout algo is in effect
 * @return the adjusted tile
 */
static TileIndex AlignTileToGrid(TileIndex tile, TownLayout layout)
{
	switch (layout) {
		case TL_2X2_GRID: return TileXY(TileX(tile) - TileX(tile) % 3, TileY(tile) - TileY(tile) % 3);
		case TL_3X3_GRID: return TileXY(TileX(tile) & ~3, TileY(tile) & ~3);
		default:          return tile;
	}
}

/**
 * Towns must all be placed on the same grid or when they eventually
 * interpenetrate their road networks will not mesh nicely; this
 * function tells you if a tile is properly aligned.
 *
 * @param tile the tile to start at
 * @param layout which town layout algo is in effect
 * @return true if the tile is in the correct location
 */
static bool IsTileAlignedToGrid(TileIndex tile, TownLayout layout)
{
	switch (layout) {
		case TL_2X2_GRID: return TileX(tile) % 3 == 0 && TileY(tile) % 3 == 0;
		case TL_3X3_GRID: return TileX(tile) % 4 == 0 && TileY(tile) % 4 == 0;
		default:          return true;
	}
}

/**
 * Used as the user_data for FindFurthestFromWater
 */
struct SpotData {
	TileIndex tile; ///< holds the tile that was found
	uint max_dist;  ///< holds the distance that tile is from the water
	TownLayout layout; ///< tells us what kind of town we're building
};

/**
 * CircularTileSearch callback; finds the tile furthest from any
 * water. slightly bit tricky, since it has to do a search of its own
 * in order to find the distance to the water from each square in the
 * radius.
 *
 * Also, this never returns true, because it needs to take into
 * account all locations being searched before it knows which is the
 * furthest.
 *
 * @param tile Start looking from this tile
 * @param user_data Storage area for data that must last across calls;
 * must be a pointer to struct SpotData
 *
 * @return always false
 */
static bool FindFurthestFromWater(TileIndex tile, void *user_data)
{
	SpotData *sp = (SpotData*)user_data;
	uint dist = GetClosestWaterDistance(tile, true);

	if (IsTileType(tile, MP_CLEAR) &&
			IsTileFlat(tile) &&
			IsTileAlignedToGrid(tile, sp->layout) &&
			dist > sp->max_dist) {
		sp->tile = tile;
		sp->max_dist = dist;
	}

	return false;
}

/**
 * CircularTileSearch callback; finds the nearest land tile
 *
 * @param tile Start looking from this tile
 * @param user_data not used
 */
static bool FindNearestEmptyLand(TileIndex tile, void *user_data)
{
	return IsTileType(tile, MP_CLEAR);
}

/**
 * Given a spot on the map (presumed to be a water tile), find a good
 * coastal spot to build a city. We don't want to build too close to
 * the edge if we can help it (since that retards city growth) hence
 * the search within a search within a search. O(n*m^2), where n is
 * how far to search for land, and m is how far inland to look for a
 * flat spot.
 *
 * @param tile Start looking from this spot.
 * @param layout the road layout to search for
 * @return tile that was found
 */
static TileIndex FindNearestGoodCoastalTownSpot(TileIndex tile, TownLayout layout)
{
	SpotData sp = { INVALID_TILE, 0, layout };

	TileIndex coast = tile;
	if (CircularTileSearch(&coast, 40, FindNearestEmptyLand, nullptr)) {
		CircularTileSearch(&coast, 10, FindFurthestFromWater, &sp);
		return sp.tile;
	}

	/* if we get here just give up */
	return INVALID_TILE;
}

static Town *CreateRandomTown(uint attempts, uint32 townnameparts, TownSize size, bool city, TownLayout layout)
{
	assert(_game_mode == GM_EDITOR || _generating_world); // These are the preconditions for CMD_DELETE_TOWN

	if (!Town::CanAllocateItem()) return nullptr;

	do {
		/* Generate a tile index not too close from the edge */
		TileIndex tile = AlignTileToGrid(RandomTile(), layout);

		/* if we tried to place the town on water, slide it over onto
		 * the nearest likely-looking spot */
		if (IsTileType(tile, MP_WATER)) {
			tile = FindNearestGoodCoastalTownSpot(tile, layout);
			if (tile == INVALID_TILE) continue;
		}

		/* Make sure town can be placed here */
		if (TownCanBePlacedHere(tile).Failed()) continue;

		/* Allocate a town struct */
		Town *t = new Town(tile);

		DoCreateTown(t, tile, townnameparts, size, city, layout, false);

		/* if the population is still 0 at the point, then the
		 * placement is so bad it couldn't grow at all */
		if (t->cache.population > 0) return t;

		Backup<CompanyID> cur_company(_current_company, OWNER_TOWN, FILE_LINE);
		CommandCost rc = DoCommand(t->xy, t->index, 0, DC_EXEC, CMD_DELETE_TOWN);
		cur_company.Restore();
		assert(rc.Succeeded());

		/* We already know that we can allocate a single town when
		 * entering this function. However, we create and delete
		 * a town which "resets" the allocation checks. As such we
		 * need to check again when assertions are enabled. */
		assert(Town::CanAllocateItem());
	} while (--attempts != 0);

	return nullptr;
}

static const byte _num_initial_towns[4] = {5, 11, 23, 46};  // very low, low, normal, high

/**
 * This function will generate a certain amount of towns, with a certain layout
 * It can be called from the scenario editor (i.e.: generate Random Towns)
 * as well as from world creation.
 * @param layout which towns will be set to, when created
 * @return true if towns have been successfully created
 */
bool GenerateTowns(TownLayout layout)
{
	uint current_number = 0;
	uint difficulty = (_game_mode != GM_EDITOR) ? _settings_game.difficulty.number_towns : 0;
	uint total = (difficulty == (uint)CUSTOM_TOWN_NUMBER_DIFFICULTY) ? _settings_game.game_creation.custom_town_number : ScaleByMapSize(_num_initial_towns[difficulty] + (Random() & 7));
	total = min(TownPool::MAX_SIZE, total);
	uint32 townnameparts;
	TownNames town_names;

	SetGeneratingWorldProgress(GWP_TOWN, total);

	/* First attempt will be made at creating the suggested number of towns.
	 * Note that this is really a suggested value, not a required one.
	 * We would not like the system to lock up just because the user wanted 100 cities on a 64*64 map, would we? */
	do {
		bool city = (_settings_game.economy.larger_towns != 0 && Chance16(1, _settings_game.economy.larger_towns));
		IncreaseGeneratingWorldProgress(GWP_TOWN);
		/* Get a unique name for the town. */
		if (!GenerateTownName(&townnameparts, &town_names)) continue;
		/* try 20 times to create a random-sized town for the first loop. */
		if (CreateRandomTown(20, townnameparts, TSZ_RANDOM, city, layout) != nullptr) current_number++; // If creation was successful, raise a flag.
	} while (--total);

	town_names.clear();

	/* Build the town k-d tree again to make sure it's well balanced */
	RebuildTownKdtree();

	if (current_number != 0) return true;

	/* If current_number is still zero at this point, it means that not a single town has been created.
	 * So give it a last try, but now more aggressive */
	if (GenerateTownName(&townnameparts) &&
			CreateRandomTown(10000, townnameparts, TSZ_RANDOM, _settings_game.economy.larger_towns != 0, layout) != nullptr) {
		return true;
	}

	/* If there are no towns at all and we are generating new game, bail out */
	if (Town::GetNumItems() == 0 && _game_mode != GM_EDITOR) {
		ShowErrorMessage(STR_ERROR_COULD_NOT_CREATE_TOWN, INVALID_STRING_ID, WL_CRITICAL);
	}

	return false;  // we are still without a town? we failed, simply
}


/**
 * Returns the bit corresponding to the town zone of the specified tile
 * or #HZB_END if the tile is ouside of the town.
 *
 * @param t Town on which town zone is to be found
 * @param tile TileIndex where town zone needs to be found
 * @return the bit position of the given zone, as defined in HouseZones
 *
 * @see GetTownRadiusGroup
 */
HouseZonesBits TryGetTownRadiusGroup(const Town *t, TileIndex tile)
{
	uint dist = DistanceSquare(tile, t->xy);

	if (t->fund_buildings_months && dist <= 25) return HZB_TOWN_CENTRE;

	HouseZonesBits smallest = HZB_END;
	for (HouseZonesBits i = HZB_BEGIN; i < HZB_END; i++) {
		if (dist < t->cache.squared_town_zone_radius[i]) smallest = i;
	}

	return smallest;
}

/**
 * Returns the bit corresponding to the town zone of the specified tile.
 * Returns #HZB_TOWN_EDGE if the tile is either in an edge zone or ouside of the town.
 *
 * @param t Town on which town zone is to be found
 * @param tile TileIndex where town zone needs to be found
 * @return the bit position of the given zone, as defined in HouseZones
 *
 * @see TryGetTownRadiusGroup
 */
HouseZonesBits GetTownRadiusGroup(const Town *t, TileIndex tile)
{
	HouseZonesBits ret = TryGetTownRadiusGroup(t, tile);
	return ret != HZB_END ? ret : HZB_TOWN_EDGE;
}

/**
 * Clears tile and builds a house or house part.
 * @param tile tile index
 * @param t The town to clear the house for
 * @param counter of construction step
 * @param stage of construction (used for drawing)
 * @param type of house. Index into house specs array
 * @param random_bits required for newgrf houses
 * @pre house can be built here
 */
static inline void ClearMakeHouseTile(TileIndex tile, Town *t, byte counter, byte stage, HouseID type, byte random_bits)
{
	CommandCost cc = DoCommand(tile, 0, 0, DC_EXEC | DC_AUTO | DC_NO_WATER, CMD_LANDSCAPE_CLEAR);

	assert(cc.Succeeded());

	IncreaseBuildingCount(t, type);
	MakeHouseTile(tile, t->index, counter, stage, type, random_bits);
	if (HouseSpec::Get(type)->building_flags & BUILDING_IS_ANIMATED) AddAnimatedTile(tile);

	MarkTileDirtyByTile(tile);
}


/**
 * Write house information into the map. For houses > 1 tile, all tiles are marked.
 * @param t tile index
 * @param town The town related to this house
 * @param counter of construction step
 * @param stage of construction (used for drawing)
 * @param type of house. Index into house specs array
 * @param random_bits required for newgrf houses
 * @pre house can be built here
 */
static void MakeTownHouse(TileIndex t, Town *town, byte counter, byte stage, HouseID type, byte random_bits)
{
	BuildingFlags size = HouseSpec::Get(type)->building_flags;

	ClearMakeHouseTile(t, town, counter, stage, type, random_bits);
	if (size & BUILDING_2_TILES_Y)   ClearMakeHouseTile(t + TileDiffXY(0, 1), town, counter, stage, ++type, random_bits);
	if (size & BUILDING_2_TILES_X)   ClearMakeHouseTile(t + TileDiffXY(1, 0), town, counter, stage, ++type, random_bits);
	if (size & BUILDING_HAS_4_TILES) ClearMakeHouseTile(t + TileDiffXY(1, 1), town, counter, stage, ++type, random_bits);

	if (!_generating_world) {
		ForAllStationsAroundTiles(TileArea(t, (size & BUILDING_2_TILES_X) ? 2 : 1, (size & BUILDING_2_TILES_Y) ? 2 : 1), [town](Station *st, TileIndex tile) {
			town->stations_near.insert(st);
			return true;
		});
	}
}


/**
 * Checks if a house can be built here. Important is slope, bridge above
 * and ability to clear the land.
 * @param tile tile to check
 * @param town town that is checking
 * @param noslope are slopes (foundations) allowed?
 * @return success if house can be built here, error message otherwise
 */
static inline CommandCost CanBuildHouseHere(TileIndex tile, TownID town, bool noslope)
{
	/* cannot build on these slopes... */
	if (noslope) {
		if (!IsTileFlat(tile)) return_cmd_error(STR_ERROR_FLAT_LAND_REQUIRED);
	} else {
		if (IsSteepSlope(GetTileSlope(tile))) return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
	}

	/* at least one RoadTypes allow building the house here? */
	if (!RoadTypesAllowHouseHere(tile)) return_cmd_error(STR_ERROR_NO_SUITABLE_ROAD);

	/* building under a bridge? */
	if (IsBridgeAbove(tile)) return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

	/* can we clear the land? */
	CommandCost ret = DoCommand(tile, 0, 0, DC_AUTO | DC_NO_WATER, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return ret;

	/* do not try to build over house owned by another town */
	if (IsTileType(tile, MP_HOUSE) && GetTownIndex(tile) != town) return CMD_ERROR;

	return CommandCost();
}


/**
 * Checks if a house can be built here. Important is slope, bridge above
 * and ability to clear the land.
 *
 * @param ta tile area to check
 * @param town town that is checking
 * @param maxz z level of the house, check if all tiles have this max z level
 * @param noslope are slopes (foundations) allowed?
 * @return success if house can be built here, error message otherwise
 *
 * @see TownLayoutAllowsHouseHere
 */
static inline CommandCost CanBuildHouseHere(const TileArea &ta, TownID town, int maxz, bool noslope)
{
	TILE_AREA_LOOP(tile, ta) {
		CommandCost ret = CanBuildHouseHere(tile, town, noslope);
		/* if building on slopes is allowed, there will be flattening foundation (to tile max z) */
		if (ret.Succeeded() && GetTileMaxZ(tile) != maxz) ret = CommandCost(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
		if (ret.Failed()) return ret;
	}

	return CommandCost();
}


/**
 * Test whether houses of given type are avaliable in current game.
 *
 * The function will check whether the house is available at all e.g. is not overriden.
 * Also availability for current climate and given house zone will be tested.
 *
 * @param house house type
 * @param above_snowline true to test availability above the snow line, false for below (arctic climate only)
 * @param zone return error if houses are forbidden in this house zone
 * @return success if house is avaliable, error message otherwise
 */
static inline CommandCost IsHouseTypeAllowed(HouseID house, bool above_snowline, HouseZonesBits zone)
 {
	const HouseSpec *hs = HouseSpec::Get(house);
	/* Disallow disabled and replaced houses. */
	if (!hs->enabled || hs->grf_prop.override != INVALID_HOUSE_ID) return CMD_ERROR;

	/* Check if we can build this house in current climate. */
	if (_settings_game.game_creation.landscape != LT_ARCTIC) {
		if (!(hs->building_availability & (HZ_TEMP << _settings_game.game_creation.landscape))) return CMD_ERROR;
	} else if (above_snowline) {
		if (!(hs->building_availability & HZ_SUBARTC_ABOVE)) return_cmd_error(STR_ERROR_BUILDING_NOT_ALLOWED_ABOVE_SNOW_LINE);
	} else {
		if (!(hs->building_availability & HZ_SUBARTC_BELOW)) return_cmd_error(STR_ERROR_BUILDING_NOT_ALLOWED_BELOW_SNOW_LINE);
	}

	/* Check if the house zone is allowed for this type of houses. */
	if (!HasBit(hs->building_availability & HZ_ZONALL, zone)) {
		return_cmd_error(STR_ERROR_BUILDING_NOT_ALLOWED_IN_THIS_TOWN_ZONE);
	}

	return CommandCost();
}


/**
 * Check whether a town can hold more house types.
 * @param t the town we wan't to check
 * @param house type of the house we wan't to add
 * @return success if houses of this type are allowed, error message otherwise
 */
static inline CommandCost IsAnotherHouseTypeAllowedInTown(Town *t, HouseID house)
{
	const HouseSpec *hs = HouseSpec::Get(house);

	/* Don't let these counters overflow. Global counters are 32bit, there will never be that many houses. */
	if (hs->class_id != HOUSE_NO_CLASS) {
		/* id_count is always <= class_count, so it doesn't need to be checked */
		if (t->cache.building_counts.class_count[hs->class_id] == UINT16_MAX) return_cmd_error(STR_ERROR_TOO_MANY_HOUSE_SETS);
	} else {
		/* If the house has no class, check id_count instead */
		if (t->cache.building_counts.id_count[house] == UINT16_MAX) return_cmd_error(STR_ERROR_TOO_MANY_HOUSE_TYPES);
	}

	return CommandCost();
}

/**
 * Checks if current town layout allows building here
 * @param t town
 * @param ta tile area to check
 * @return true iff town layout allows building here
 * @note see layouts
 */
static inline bool TownLayoutAllowsHouseHere(Town *t, const TileArea &ta)
{
	/* Allow towns everywhere when we don't build roads */
	if (!_settings_game.economy.allow_town_roads && !_generating_world) return true;

	TileIndexDiffC grid_pos = TileIndexToTileIndexDiffC(t->xy, ta.tile);

	const uint overflow = 3 * 4 * UINT16_MAX; // perform "floor division"
	switch (t->layout) {
		case TL_2X2_GRID: return (uint)(grid_pos.x + overflow) % 3 >= ta.w && (uint)(grid_pos.y + overflow) % 3 >= ta.h;
		case TL_3X3_GRID: return (uint)(grid_pos.x + overflow) % 4 >= ta.w && (uint)(grid_pos.y + overflow) % 4 >= ta.h;
		default: return true;
	}
}


/**
 * Find a suitable place (free of any obstacles) for a new town house. Search around a given location
 * taking into account the layout of the town.
 *
 * @param tile tile that must be included by the building
 * @param t the town we are building in
 * @param house house type
 * @return where the building can be placed, INVALID_TILE if no lacation was found
 *
 * @pre CanBuildHouseHere(tile, t->index, false)
 *
 * @see CanBuildHouseHere
 */
static TileIndex FindPlaceForTownHouseAroundTile(TileIndex tile, Town *t, HouseID house)
{
	const HouseSpec *hs = HouseSpec::Get(house);
	bool noslope = (hs->building_flags & TILE_NOT_SLOPED) != 0;

	TileArea ta(tile, 1, 1);
	DiagDirection dir;
	uint count;
	if (hs->building_flags & TILE_SIZE_2x2) {
		ta.w = ta.h = 2;
		dir = DIAGDIR_NW; // 'd' goes through DIAGDIR_NW, DIAGDIR_NE, DIAGDIR_SE
		count = 4;
	} else if (hs->building_flags & TILE_SIZE_2x1) {
		ta.w = 2;
		dir = DIAGDIR_NE;
		count = 2;
	} else if (hs->building_flags & TILE_SIZE_1x2) {
		ta.h = 2;
		dir = DIAGDIR_NW;
		count = 2;
	} else { // TILE_SIZE_1x1
		/* CanBuildHouseHere(tile, t->index, false) already checked */
		if (noslope && !IsTileFlat(tile)) return INVALID_TILE;
		return tile;
	}

	int maxz = GetTileMaxZ(tile);
	/* Drift around the tile and find a place for the house. For 1x2 and 2x1 houses just two
	 * positions will be checked (at the exact tile and the other). In case of 2x2 houses
	 * 4 positions have to be checked (clockwise). */
	while (count-- > 0) {
		if (!TownLayoutAllowsHouseHere(t, ta)) continue;
		if (CanBuildHouseHere(ta, t->index, maxz, noslope).Succeeded()) return ta.tile;
		ta.tile += TileOffsByDiagDir(dir);
		dir = ChangeDiagDir(dir, DIAGDIRDIFF_90RIGHT);
	}

	return INVALID_TILE;
}


/**
 * Check if a given house can be built in a given town.
 * @param house house type
 * @param t the town
 * @return success if house can be built, error message otherwise
 */
static CommandCost CheckCanBuildHouse(HouseID house, const Town *t)
{
	const HouseSpec *hs = HouseSpec::Get(house);

	if (!_generating_world && _game_mode != GM_EDITOR && (hs->extra_flags & BUILDING_IS_HISTORICAL) != 0) {
		return CMD_ERROR;
	}

	if (_cur_year > hs->max_year) return_cmd_error(STR_ERROR_BUILDING_IS_TOO_OLD);
	if (_cur_year < hs->min_year) return_cmd_error(STR_ERROR_BUILDING_IS_TOO_MODERN);

	/* Special houses that there can be only one of. */
	if (hs->building_flags & BUILDING_IS_CHURCH) {
		if (HasBit(t->flags, TOWN_HAS_CHURCH)) return_cmd_error(STR_ERROR_ONLY_ONE_BUILDING_ALLOWED_PER_TOWN);
	} else if (hs->building_flags & BUILDING_IS_STADIUM) {
		if (HasBit(t->flags, TOWN_HAS_STADIUM)) return_cmd_error(STR_ERROR_ONLY_ONE_BUILDING_ALLOWED_PER_TOWN);
	}

	return CommandCost();
}


/**
 * Really build a house.
 * @param t town to build house in
 * @param tile house location
 * @param house house type
 * @param random_bits random bits for the house
 */
static void DoBuildHouse(Town *t, TileIndex tile, HouseID house, byte random_bits)
{
	t->cache.num_houses++;

	const HouseSpec *hs = HouseSpec::Get(house);

	/* Special houses that there can be only one of. */
	if (hs->building_flags & BUILDING_IS_CHURCH) {
		SetBit(t->flags, TOWN_HAS_CHURCH);
	} else if (hs->building_flags & BUILDING_IS_STADIUM) {
		SetBit(t->flags, TOWN_HAS_STADIUM);
	}

	byte construction_counter = 0;
	byte construction_stage = 0;

	if (_generating_world || _game_mode == GM_EDITOR) {
		uint32 r = Random();

		construction_stage = TOWN_HOUSE_COMPLETED;
		if (Chance16(1, 7)) construction_stage = GB(r, 0, 2);

		if (construction_stage == TOWN_HOUSE_COMPLETED) {
			ChangePopulation(t, hs->population);
		} else {
			construction_counter = GB(r, 2, 2);
		}
	}

	MakeTownHouse(tile, t, construction_counter, construction_stage, house, random_bits);
	UpdateTownRadius(t);
	UpdateTownGrowthRate(t);
	UpdateTownCargoesHouse(t, tile, hs->building_flags & BUILDING_2_TILES_X, hs->building_flags & BUILDING_2_TILES_Y);
}

/**
 * Place a custom house
 * @param tile tile where the house will be located
 * @param flags flags for the command
 * @param p1 \n
 *    bits  0..15 - the HouseID of the house \n
 *    bits 16..31 - the TownID of the town \n
 * @param p2 \n
 *    bits  0..7  - random bits \n
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildHouse(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (_game_mode != GM_EDITOR && // in scenario editor anyone can build a house
			_current_company != OWNER_TOWN && // towns naturally can build houses
			_current_company != OWNER_DEITY) { // GameScript can place a house too
		return CMD_ERROR;
	}

	HouseID house = GB(p1, 0, 16);
	Town *t = Town::Get(GB(p1, 16, 16));
	if (t == nullptr) return CMD_ERROR;
	byte random_bits = GB(p2, 0, 8);

	int max_z = GetTileMaxZ(tile);
	bool above_snowline = (_settings_game.game_creation.landscape == LT_ARCTIC) && (max_z > HighestSnowLine());

	CommandCost          ret = IsHouseTypeAllowed(house, above_snowline, TryGetTownRadiusGroup(t, tile));
	if (ret.Succeeded()) ret = IsAnotherHouseTypeAllowedInTown(t, house);
	if (ret.Succeeded()) ret = CheckCanBuildHouse(house, t);
	if (ret.Succeeded()) {
		/* While placing a house manually, try only at exact position and ignore the layout */
		const HouseSpec *hs = HouseSpec::Get(house);
		uint w = hs->building_flags & BUILDING_2_TILES_X ? 2 : 1;
		uint h = hs->building_flags & BUILDING_2_TILES_Y ? 2 : 1;
		bool noslope = (hs->building_flags & TILE_NOT_SLOPED) != 0;
		ret = CanBuildHouseHere(TileArea(tile, w, h), t->index, max_z, noslope);
	}
	if (ret.Failed()) return ret;

	/* Check if GRF allows this house */
	if (!HouseAllowsConstruction(house, tile, t, random_bits)) return_cmd_error(STR_ERROR_BUILDING_NOT_ALLOWED);

	if (flags & DC_EXEC) DoBuildHouse(t, tile, house, random_bits);
	return CommandCost();
}

/**
 * Tries to build a house at this tile
 * @param t town the house will belong to
 * @param tile where the house will be built
 * @return false iff no house can be built at this tile
 */
static bool BuildTownHouse(Town *t, TileIndex tile)
{
	/* forbidden building here by town layout */
	if (!TownLayoutAllowsHouseHere(t, TileArea(tile, 1, 1))) return false;

	/* no house allowed at all, bail out */
	if (CanBuildHouseHere(tile, t->index, false).Failed()) return false;

	bool above_snowline = _settings_game.game_creation.landscape == LT_ARCTIC && GetTileMaxZ(tile) > HighestSnowLine();
	HouseZonesBits zone = GetTownRadiusGroup(t, tile);

	/* bits 0-4 are used
	 * bits 11-15 are used
	 * bits 5-10 are not used. */
	HouseID houses[NUM_HOUSES];
	uint num = 0;
	uint probs[NUM_HOUSES];
	uint probability_max = 0;

	/* Generate a list of all possible houses that can be built. */
	for (uint i = 0; i < NUM_HOUSES; i++) {
		if (IsHouseTypeAllowed((HouseID)i, above_snowline, zone).Failed()) continue;
		if (IsAnotherHouseTypeAllowedInTown(t, (HouseID)i).Failed()) continue;

		uint cur_prob = HouseSpec::Get(i)->probability;
		probability_max += cur_prob;
		probs[num] = cur_prob;
		houses[num++] = (HouseID)i;
	}

	TileIndex baseTile = tile;

	while (probability_max > 0) {
		/* Building a multitile building can change the location of tile.
		 * The building would still be built partially on that tile, but
		 * its northern tile would be elsewhere. However, if the callback
		 * fails we would be basing further work from the changed tile.
		 * So a next 1x1 tile building could be built on the wrong tile. */
		tile = baseTile;

		uint r = RandomRange(probability_max);
		uint i;
		for (i = 0; i < num; i++) {
			if (probs[i] > r) break;
			r -= probs[i];
		}

		HouseID house = houses[i];
		probability_max -= probs[i];

		/* remove tested house from the set */
		num--;
		houses[i] = houses[num];
		probs[i] = probs[num];

		CommandCost ret = CheckCanBuildHouse(house, t);
		if (ret.Failed()) continue;

		tile = FindPlaceForTownHouseAroundTile(tile, t, house);
		if (tile == INVALID_TILE) continue;

		byte random_bits = Random();

		/* Check if GRF allows this house */
		if (!HouseAllowsConstruction(house, tile, t, random_bits)) continue;

		DoBuildHouse(t, tile, house, random_bits);
		return true;
	}

	return false;
}

/**
 * Update data structures when a house is removed
 * @param tile  Tile of the house
 * @param t     Town owning the house
 * @param house House type
 */
static void DoClearTownHouseHelper(TileIndex tile, Town *t, HouseID house)
{
	assert_tile(IsTileType(tile, MP_HOUSE), tile);
	DecreaseBuildingCount(t, house);
	DoClearSquare(tile);
	DeleteAnimatedTile(tile);

	DeleteNewGRFInspectWindow(GSF_HOUSES, tile);
}

/**
 * Determines if a given HouseID is part of a multitile house.
 * The given ID is set to the ID of the north tile and the TileDiff to the north tile is returned.
 *
 * @param house Is changed to the HouseID of the north tile of the same house
 * @return TileDiff from the tile of the given HouseID to the north tile
 */
TileIndexDiff GetHouseNorthPart(HouseID &house)
{
	if (house >= 3) { // house id 0,1,2 MUST be single tile houses, or this code breaks.
		if (HouseSpec::Get(house - 1)->building_flags & TILE_SIZE_2x1) {
			house--;
			return TileDiffXY(-1, 0);
		} else if (HouseSpec::Get(house - 1)->building_flags & BUILDING_2_TILES_Y) {
			house--;
			return TileDiffXY(0, -1);
		} else if (HouseSpec::Get(house - 2)->building_flags & BUILDING_HAS_4_TILES) {
			house -= 2;
			return TileDiffXY(-1, 0);
		} else if (HouseSpec::Get(house - 3)->building_flags & BUILDING_HAS_4_TILES) {
			house -= 3;
			return TileDiffXY(-1, -1);
		}
	}
	return 0;
}

void ClearTownHouse(Town *t, TileIndex tile)
{
	assert_tile(IsTileType(tile, MP_HOUSE), tile);

	HouseID house = GetHouseType(tile);

	/* need to align the tile to point to the upper left corner of the house */
	tile += GetHouseNorthPart(house); // modifies house to the ID of the north tile

	const HouseSpec *hs = HouseSpec::Get(house);

	/* Remove population from the town if the house is finished. */
	if (IsHouseCompleted(tile)) {
		ChangePopulation(t, -hs->population);
	}

	t->cache.num_houses--;

	/* Clear flags for houses that only may exist once/town. */
	if (hs->building_flags & BUILDING_IS_CHURCH) {
		ClrBit(t->flags, TOWN_HAS_CHURCH);
	} else if (hs->building_flags & BUILDING_IS_STADIUM) {
		ClrBit(t->flags, TOWN_HAS_STADIUM);
	}

	/* Do the actual clearing of tiles */
	DoClearTownHouseHelper(tile, t, house);
	if (hs->building_flags & BUILDING_2_TILES_Y)   DoClearTownHouseHelper(tile + TileDiffXY(0, 1), t, ++house);
	if (hs->building_flags & BUILDING_2_TILES_X)   DoClearTownHouseHelper(tile + TileDiffXY(1, 0), t, ++house);
	if (hs->building_flags & BUILDING_HAS_4_TILES) DoClearTownHouseHelper(tile + TileDiffXY(1, 1), t, ++house);

	RemoveNearbyStations(t, tile, hs->building_flags);

	UpdateTownRadius(t);

	/* Update cargo acceptance. */
	UpdateTownCargoesHouse(t, tile, hs->building_flags & BUILDING_2_TILES_X, hs->building_flags & BUILDING_2_TILES_Y);
}

/**
 * Rename a town (server-only).
 * @param tile unused
 * @param flags type of operation
 * @param p1 town ID to rename
 * @param p2 unused
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameTown(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Town *t = Town::GetIfValid(p1);
	if (t == nullptr) return CMD_ERROR;

	bool reset = StrEmpty(text);

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_TOWN_NAME_CHARS) return CMD_ERROR;
		if (!IsUniqueTownName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		t->cached_name.clear();
		if (reset) {
			t->name.clear();
		} else {
			t->name = text;
		}

		t->UpdateVirtCoord();
		InvalidateWindowData(WC_TOWN_DIRECTORY, 0, TDIWD_FORCE_RESORT);
		ClearAllStationCachedNames();
		ClearAllIndustryCachedNames();
		UpdateAllStationVirtCoords();
	}
	return CommandCost();
}

/**
 * Determines the first cargo with a certain town effect
 * @param effect Town effect of interest
 * @return first active cargo slot with that effect
 */
const CargoSpec *FindFirstCargoWithTownEffect(TownEffect effect)
{
	const CargoSpec *cs;
	FOR_ALL_CARGOSPECS(cs) {
		if (cs->town_effect == effect) return cs;
	}
	return nullptr;
}

/**
 * Change the cargo goal of a town.
 * @param tile Unused.
 * @param flags Type of operation.
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 15) - Town ID to cargo game of.
 * - p1 = (bit 16 - 23) - TownEffect to change the game of.
 * @param p2 The new goal value.
 * @param text Unused.
 * @return Empty cost or an error.
 */
CommandCost CmdTownCargoGoal(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;

	TownEffect te = (TownEffect)GB(p1, 16, 8);
	if (te < TE_BEGIN || te >= TE_END) return CMD_ERROR;

	uint16 index = GB(p1, 0, 16);
	Town *t = Town::GetIfValid(index);
	if (t == nullptr) return CMD_ERROR;

	/* Validate if there is a cargo which is the requested TownEffect */
	const CargoSpec *cargo = FindFirstCargoWithTownEffect(te);
	if (cargo == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		t->goal[te] = p2;
		UpdateTownGrowth(t);
		InvalidateWindowData(WC_TOWN_VIEW, index);
	}

	return CommandCost();
}

/**
 * Set a custom text in the Town window.
 * @param tile Unused.
 * @param flags Type of operation.
 * @param p1 Town ID to change the text of.
 * @param p2 Unused.
 * @param text The new text (empty to remove the text).
 * @return Empty cost or an error.
 */
CommandCost CmdTownSetText(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	Town *t = Town::GetIfValid(p1);
	if (t == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		t->text.clear();
		if (!StrEmpty(text)) t->text = text;
		InvalidateWindowData(WC_TOWN_VIEW, p1);
	}

	return CommandCost();
}

/**
 * Change the growth rate of the town.
 * @param tile Unused.
 * @param flags Type of operation.
 * @param p1 Town ID to cargo game of.
 * @param p2 Amount of days between growth, or TOWN_GROWTH_RATE_NONE, or 0 to reset custom growth rate.
 * @param text Unused.
 * @return Empty cost or an error.
 */
CommandCost CmdTownGrowthRate(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	if (GB(p2, 16, 16) != 0) return CMD_ERROR;

	Town *t = Town::GetIfValid(p1);
	if (t == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		if (p2 == 0) {
			/* Just clear the flag, UpdateTownGrowth will determine a proper growth rate */
			ClrBit(t->flags, TOWN_CUSTOM_GROWTH);
		} else {
			uint old_rate = t->growth_rate;
			if (t->grow_counter >= old_rate) {
				/* This also catches old_rate == 0 */
				t->grow_counter = p2;
			} else {
				/* Scale grow_counter, so half finished houses stay half finished */
				t->grow_counter = t->grow_counter * p2 / old_rate;
			}
			t->growth_rate = p2;
			SetBit(t->flags, TOWN_CUSTOM_GROWTH);
		}
		UpdateTownGrowth(t);
		InvalidateWindowData(WC_TOWN_VIEW, p1);
	}

	return CommandCost();
}

/**
 * Change the rating of a company in a town
 * @param tile Unused.
 * @param flags Type of operation.
 * @param p1 Bit 0..15 = Town ID to change, bit 16..23 = Company ID to change.
 * @param p2 Bit 0..15 = New rating of company (signed int16).
 * @param text Unused.
 * @return Empty cost or an error.
 */
CommandCost CmdTownRating(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;

	TownID town_id = (TownID)GB(p1, 0, 16);
	Town *t = Town::GetIfValid(town_id);
	if (t == nullptr) return CMD_ERROR;

	CompanyID company_id = (CompanyID)GB(p1, 16, 8);
	if (!Company::IsValidID(company_id)) return CMD_ERROR;

	int16 new_rating = Clamp((int16)GB(p2, 0, 16), RATING_MINIMUM, RATING_MAXIMUM);
	if (flags & DC_EXEC) {
		t->ratings[company_id] = new_rating;
		InvalidateWindowData(WC_TOWN_AUTHORITY, town_id);
	}

	return CommandCost();
}

/**
 * Expand a town (scenario editor only).
 * @param tile Unused.
 * @param flags Type of operation.
 * @param p1 Town ID to expand.
 * @param p2 Amount to grow, or 0 to grow a random size up to the current amount of houses.
 * @param text Unused.
 * @return Empty cost or an error.
 */
CommandCost CmdExpandTown(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (_game_mode != GM_EDITOR && _current_company != OWNER_DEITY) return CMD_ERROR;
	Town *t = Town::GetIfValid(p1);
	if (t == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		/* The more houses, the faster we grow */
		if (p2 == 0) {
			uint amount = RandomRange(ClampToU16(t->cache.num_houses / 10)) + 3;
			t->cache.num_houses += amount;
			UpdateTownRadius(t);

			uint n = amount * 10;
			do GrowTown(t); while (--n);

			t->cache.num_houses -= amount;
		} else {
			for (; p2 > 0; p2--) {
				/* Try several times to grow, as we are really suppose to grow */
				for (uint i = 0; i < 25; i++) if (GrowTown(t)) break;
			}
		}
		UpdateTownRadius(t);

		UpdateTownMaxPass(t);
	}

	return CommandCost();
}

/**
 * Delete a town (scenario editor or worldgen only).
 * @param tile Unused.
 * @param flags Type of operation.
 * @param p1 Town ID to delete.
 * @param p2 Unused.
 * @param text Unused.
 * @return Empty cost or an error.
 */
CommandCost CmdDeleteTown(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (_game_mode != GM_EDITOR && !_generating_world) return CMD_ERROR;
	Town *t = Town::GetIfValid(p1);
	if (t == nullptr) return CMD_ERROR;

	/* Stations refer to towns. */
	for (const Station *st : Station::Iterate()) {
		if (st->town == t) {
			/* Non-oil rig stations are always a problem. */
			if (!(st->facilities & FACIL_AIRPORT) || st->airport.type != AT_OILRIG) return CMD_ERROR;
			/* We can only automatically delete oil rigs *if* there's no vehicle on them. */
			CommandCost ret = DoCommand(st->airport.tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			if (ret.Failed()) return ret;
		}
	}

	/* Depots refer to towns. */
	for (const Depot *d : Depot::Iterate()) {
		if (d->town == t) return CMD_ERROR;
	}

	/* Check all tiles for town ownership. First check for bridge tiles, as
	 * these do not directly have an owner so we need to check adjacent
	 * tiles. This won't work correctly in the same loop if the adjacent
	 * tile was already deleted earlier in the loop. */
	for (TileIndex tile = 0; tile < MapSize(); ++tile) {
		if (IsTileType(tile, MP_TUNNELBRIDGE) && TestTownOwnsBridge(tile, t)) {
			CommandCost ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			if (ret.Failed()) return ret;
		}
	}

	/* Check all remaining tiles for town ownership. */
	for (TileIndex tile = 0; tile < MapSize(); ++tile) {
		bool try_clear = false;
		switch (GetTileType(tile)) {
			case MP_ROAD:
				try_clear = HasTownOwnedRoad(tile) && GetTownIndex(tile) == t->index;
				break;

			case MP_HOUSE:
				try_clear = GetTownIndex(tile) == t->index;
				break;

			case MP_INDUSTRY:
				try_clear = Industry::GetByTile(tile)->town == t;
				break;

			case MP_OBJECT:
				if (Town::GetNumItems() == 1) {
					/* No towns will be left, remove it! */
					try_clear = true;
				} else {
					Object *o = Object::GetByTile(tile);
					if (o->town == t) {
						if (o->type == OBJECT_STATUE) {
							/* Statue... always remove. */
							try_clear = true;
						} else {
							/* Tell to find a new town. */
							if (flags & DC_EXEC) o->town = nullptr;
						}
					}
				}
				break;

			default:
				break;
		}
		if (try_clear) {
			CommandCost ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			if (ret.Failed()) return ret;
		}
	}

	/* The town destructor will delete the other things related to the town. */
	if (flags & DC_EXEC) {
		_town_kdtree.Remove(t->index);
		if (_viewport_sign_kdtree_valid && t->cache.sign.kdtree_valid) _viewport_sign_kdtree.Remove(ViewportSignKdtreeItem::MakeTown(t->index));
		delete t;
	}

	return CommandCost();
}

/**
 * Factor in the cost of each town action.
 * @see TownActions
 */
const byte _town_action_costs[TACT_COUNT] = {
	2, 4, 9, 35, 48, 53, 117, 175
};

static CommandCost TownActionAdvertiseSmall(Town *t, DoCommandFlag flags)
{
	if (flags & DC_EXEC) {
		ModifyStationRatingAround(t->xy, _current_company, 0x40, 10);
	}
	return CommandCost();
}

static CommandCost TownActionAdvertiseMedium(Town *t, DoCommandFlag flags)
{
	if (flags & DC_EXEC) {
		ModifyStationRatingAround(t->xy, _current_company, 0x70, 15);
	}
	return CommandCost();
}

static CommandCost TownActionAdvertiseLarge(Town *t, DoCommandFlag flags)
{
	if (flags & DC_EXEC) {
		ModifyStationRatingAround(t->xy, _current_company, 0xA0, 20);
	}
	return CommandCost();
}

static CommandCost TownActionRoadRebuild(Town *t, DoCommandFlag flags)
{
	/* Check if the company is allowed to fund new roads. */
	if (!_settings_game.economy.fund_roads) return CMD_ERROR;

	if (flags & DC_EXEC) {
		t->road_build_months = 6;

		char company_name[MAX_LENGTH_COMPANY_NAME_CHARS * MAX_CHAR_LENGTH];
		SetDParam(0, _current_company);
		GetString(company_name, STR_COMPANY_NAME, lastof(company_name));

		char *cn = stredup(company_name);
		SetDParam(0, t->index);
		SetDParamStr(1, cn);

		AddNewsItem(STR_NEWS_ROAD_REBUILDING, NT_GENERAL, NF_NORMAL, NR_TOWN, t->index, NR_NONE, UINT32_MAX, cn);
		AI::BroadcastNewEvent(new ScriptEventRoadReconstruction((ScriptCompany::CompanyID)(Owner)_current_company, t->index));
		Game::NewEvent(new ScriptEventRoadReconstruction((ScriptCompany::CompanyID)(Owner)_current_company, t->index));
	}
	return CommandCost();
}

/**
 * Check whether the land can be cleared.
 * @param tile Tile to check.
 * @return The tile can be cleared.
 */
static bool TryClearTile(TileIndex tile)
{
	Backup<CompanyID> cur_company(_current_company, OWNER_NONE, FILE_LINE);
	CommandCost r = DoCommand(tile, 0, 0, DC_NONE, CMD_LANDSCAPE_CLEAR);
	cur_company.Restore();
	return r.Succeeded();
}

/** Structure for storing data while searching the best place to build a statue. */
struct StatueBuildSearchData {
	TileIndex best_position; ///< Best position found so far.
	int tile_count;          ///< Number of tiles tried.

	StatueBuildSearchData(TileIndex best_pos, int count) : best_position(best_pos), tile_count(count) { }
};

/**
 * Search callback function for #TownActionBuildStatue.
 * @param tile Tile on which to perform the search.
 * @param user_data Reference to the statue search data.
 * @return Result of the test.
 */
static bool SearchTileForStatue(TileIndex tile, void *user_data)
{
	static const int STATUE_NUMBER_INNER_TILES = 25; // Number of tiles int the center of the city, where we try to protect houses.

	StatueBuildSearchData *statue_data = (StatueBuildSearchData *)user_data;
	statue_data->tile_count++;

	/* Statues can be build on slopes, just like houses. Only the steep slopes is a no go. */
	if (IsSteepSlope(GetTileSlope(tile))) return false;
	/* Don't build statues under bridges. */
	if (IsBridgeAbove(tile)) return false;

	/* A clear-able open space is always preferred. */
	if ((IsTileType(tile, MP_CLEAR) || IsTileType(tile, MP_TREES)) && TryClearTile(tile)) {
		statue_data->best_position = tile;
		return true;
	}

	bool house = IsTileType(tile, MP_HOUSE);

	/* Searching inside the inner circle. */
	if (statue_data->tile_count <= STATUE_NUMBER_INNER_TILES) {
		/* Save first house in inner circle. */
		if (house && statue_data->best_position == INVALID_TILE && TryClearTile(tile)) {
			statue_data->best_position = tile;
		}

		/* If we have reached the end of the inner circle, and have a saved house, terminate the search. */
		return statue_data->tile_count == STATUE_NUMBER_INNER_TILES && statue_data->best_position != INVALID_TILE;
	}

	/* Searching outside the circle, just pick the first possible spot. */
	statue_data->best_position = tile; // Is optimistic, the condition below must also hold.
	return house && TryClearTile(tile);
}

/**
 * Perform a 9x9 tiles circular search from the center of the town
 * in order to find a free tile to place a statue
 * @param t town to search in
 * @param flags Used to check if the statue must be built or not.
 * @return Empty cost or an error.
 */
static CommandCost TownActionBuildStatue(Town *t, DoCommandFlag flags)
{
	if (!Object::CanAllocateItem()) return_cmd_error(STR_ERROR_TOO_MANY_OBJECTS);

	TileIndex tile = t->xy;
	StatueBuildSearchData statue_data(INVALID_TILE, 0);
	if (!CircularTileSearch(&tile, 9, SearchTileForStatue, &statue_data)) return_cmd_error(STR_ERROR_STATUE_NO_SUITABLE_PLACE);

	if (flags & DC_EXEC) {
		Backup<CompanyID> cur_company(_current_company, OWNER_NONE, FILE_LINE);
		DoCommand(statue_data.best_position, 0, 0, DC_EXEC, CMD_LANDSCAPE_CLEAR);
		cur_company.Restore();
		BuildObject(OBJECT_STATUE, statue_data.best_position, _current_company, t);
		SetBit(t->statues, _current_company); // Once found and built, "inform" the Town.
		MarkTileDirtyByTile(statue_data.best_position);
	}
	return CommandCost();
}

static CommandCost TownActionFundBuildings(Town *t, DoCommandFlag flags)
{
	/* Check if it's allowed to buy the rights */
	if (!_settings_game.economy.fund_buildings) return CMD_ERROR;

	if (flags & DC_EXEC) {
		/* And grow for 3 months */
		t->fund_buildings_months = 3;

		/* Enable growth (also checking GameScript's opinion) */
		UpdateTownGrowth(t);

		/* Build a new house, but add a small delay to make sure
		 * that spamming funding doesn't let town grow any faster
		 * than 1 house per 2 * TOWN_GROWTH_TICKS ticks.
		 * Also emulate original behaviour when town was only growing in
		 * TOWN_GROWTH_TICKS intervals, to make sure that it's not too
		 * tick-perfect and gives player some time window where he can
		 * spam funding with the exact same efficiency.
		 */
		t->grow_counter = min(t->grow_counter, 2 * TOWN_GROWTH_TICKS - (t->growth_rate - t->grow_counter) % TOWN_GROWTH_TICKS);

		SetWindowDirty(WC_TOWN_VIEW, t->index);
	}
	return CommandCost();
}

static CommandCost TownActionBuyRights(Town *t, DoCommandFlag flags)
{
	/* Check if it's allowed to buy the rights */
	if (!_settings_game.economy.exclusive_rights) return CMD_ERROR;

	if (flags & DC_EXEC) {
		t->exclusive_counter = 12;
		t->exclusivity = _current_company;

		ModifyStationRatingAround(t->xy, _current_company, 130, 17);

		SetWindowClassesDirty(WC_STATION_VIEW);

		/* Spawn news message */
		CompanyNewsInformation *cni = MallocT<CompanyNewsInformation>(1);
		cni->FillData(Company::Get(_current_company));
		SetDParam(0, STR_NEWS_EXCLUSIVE_RIGHTS_TITLE);
		SetDParam(1, STR_NEWS_EXCLUSIVE_RIGHTS_DESCRIPTION);
		SetDParam(2, t->index);
		SetDParamStr(3, cni->company_name);
		AddNewsItem(STR_MESSAGE_NEWS_FORMAT, NT_GENERAL, NF_COMPANY, NR_TOWN, t->index, NR_NONE, UINT32_MAX, cni);
		AI::BroadcastNewEvent(new ScriptEventExclusiveTransportRights((ScriptCompany::CompanyID)(Owner)_current_company, t->index));
		Game::NewEvent(new ScriptEventExclusiveTransportRights((ScriptCompany::CompanyID)(Owner)_current_company, t->index));
	}
	return CommandCost();
}

static CommandCost TownActionBribe(Town *t, DoCommandFlag flags)
{
	if (flags & DC_EXEC) {
		if (Chance16(1, 14)) {
			/* set as unwanted for 6 months */
			t->unwanted[_current_company] = 6;

			/* set all close by station ratings to 0 */
			for (Station *st : Station::Iterate()) {
				if (st->town == t && st->owner == _current_company) {
					for (CargoID i = 0; i < NUM_CARGO; i++) st->goods[i].rating = 0;
				}
			}

			/* only show error message to the executing player. All errors are handled command.c
			 * but this is special, because it can only 'fail' on a DC_EXEC */
			if (IsLocalCompany()) ShowErrorMessage(STR_ERROR_BRIBE_FAILED, INVALID_STRING_ID, WL_INFO);

			/* decrease by a lot!
			 * ChangeTownRating is only for stuff in demolishing. Bribe failure should
			 * be independent of any cheat settings
			 */
			if (t->ratings[_current_company] > RATING_BRIBE_DOWN_TO) {
				t->ratings[_current_company] = RATING_BRIBE_DOWN_TO;
				t->UpdateVirtCoord();
				SetWindowDirty(WC_TOWN_AUTHORITY, t->index);
			}
		} else {
			ChangeTownRating(t, RATING_BRIBE_UP_STEP, RATING_BRIBE_MAXIMUM, DC_EXEC);
		}
	}
	return CommandCost();
}

typedef CommandCost TownActionProc(Town *t, DoCommandFlag flags);
static TownActionProc * const _town_action_proc[] = {
	TownActionAdvertiseSmall,
	TownActionAdvertiseMedium,
	TownActionAdvertiseLarge,
	TownActionRoadRebuild,
	TownActionBuildStatue,
	TownActionFundBuildings,
	TownActionBuyRights,
	TownActionBribe
};

/**
 * Get a list of available actions to do at a town.
 * @param nump if not nullptr add put the number of available actions in it
 * @param cid the company that is querying the town
 * @param t the town that is queried
 * @return bitmasked value of enabled actions
 */
uint GetMaskOfTownActions(int *nump, CompanyID cid, const Town *t)
{
	int num = 0;
	TownActions buttons = TACT_NONE;

	/* Spectators and unwanted have no options */
	if (cid != COMPANY_SPECTATOR && !(_settings_game.economy.bribe && t->unwanted[cid])) {

		/* Things worth more than this are not shown */
		Money avail = Company::Get(cid)->money + _price[PR_STATION_VALUE] * 200;

		/* Check the action bits for validity and
		 * if they are valid add them */
		for (uint i = 0; i != lengthof(_town_action_costs); i++) {
			const TownActions cur = (TownActions)(1 << i);

			/* Is the company not able to bribe ? */
			if (cur == TACT_BRIBE && (!_settings_game.economy.bribe || t->ratings[cid] >= RATING_BRIBE_MAXIMUM)) continue;

			/* Is the company not able to buy exclusive rights ? */
			if (cur == TACT_BUY_RIGHTS && !_settings_game.economy.exclusive_rights) continue;

			/* Is the company not able to fund buildings ? */
			if (cur == TACT_FUND_BUILDINGS && !_settings_game.economy.fund_buildings) continue;

			/* Is the company not able to fund local road reconstruction? */
			if (cur == TACT_ROAD_REBUILD && !_settings_game.economy.fund_roads) continue;

			/* Is the company not able to build a statue ? */
			if (cur == TACT_BUILD_STATUE && HasBit(t->statues, cid)) continue;

			if (avail >= _town_action_costs[i] * _price[PR_TOWN_ACTION] >> 8) {
				buttons |= cur;
				num++;
			}
		}
	}

	if (nump != nullptr) *nump = num;
	return buttons;
}

/**
 * Do a town action.
 * This performs an action such as advertising, building a statue, funding buildings,
 * but also bribing the town-council
 * @param tile unused
 * @param flags type of operation
 * @param p1 town to do the action at
 * @param p2 action to perform, @see _town_action_proc for the list of available actions
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDoTownAction(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Town *t = Town::GetIfValid(p1);
	if (t == nullptr || p2 >= lengthof(_town_action_proc)) return CMD_ERROR;

	if (!HasBit(GetMaskOfTownActions(nullptr, _current_company, t), p2)) return CMD_ERROR;

	CommandCost cost(EXPENSES_OTHER, _price[PR_TOWN_ACTION] * _town_action_costs[p2] >> 8);

	CommandCost ret = _town_action_proc[p2](t, flags);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		SetWindowDirty(WC_TOWN_AUTHORITY, p1);
	}

	return cost;
}

template <typename Func>
static void ForAllStationsNearTown(Town *t, Func func)
{
	/* Ideally the search radius should be close to the actual town zone 0 radius.
	 * The true radius is not stored or calculated anywhere, only the squared radius. */
	/* The efficiency of this search might be improved for large towns and many stations on the map,
	 * by using an integer square root approximation giving a value not less than the true square root. */
	uint search_radius = t->cache.squared_town_zone_radius[0] / 2;
	ForAllStationsRadius(t->xy, search_radius, [&](const Station * st) {
		if (DistanceSquare(st->xy, t->xy) <= t->cache.squared_town_zone_radius[0]) {
			func(st);
		}
	});
}

static void UpdateTownRating(Town *t)
{
	/* Increase company ratings if they're low */
	for (const Company *c : Company::Iterate()) {
		if (t->ratings[c->index] < RATING_GROWTH_MAXIMUM) {
			t->ratings[c->index] = min((int)RATING_GROWTH_MAXIMUM, t->ratings[c->index] + RATING_GROWTH_UP_STEP);
		}
	}

	ForAllStationsNearTown(t, [&](const Station *st) {
		if (st->time_since_load <= 20 || st->time_since_unload <= 20) {
			if (Company::IsValidID(st->owner)) {
				int new_rating = t->ratings[st->owner] + RATING_STATION_UP_STEP;
				t->ratings[st->owner] = min(new_rating, INT16_MAX); // do not let it overflow
			}
		} else {
			if (Company::IsValidID(st->owner)) {
				int new_rating = t->ratings[st->owner] + RATING_STATION_DOWN_STEP;
				t->ratings[st->owner] = max(new_rating, INT16_MIN);
			}
		}
	});

	/* clamp all ratings to valid values */
	for (uint i = 0; i < MAX_COMPANIES; i++) {
		t->ratings[i] = Clamp(t->ratings[i], RATING_MINIMUM, RATING_MAXIMUM);
	}

	t->UpdateVirtCoord();
	SetWindowDirty(WC_TOWN_AUTHORITY, t->index);
}


/**
 * Updates town grow counter after growth rate change.
 * Preserves relative house builting progress whenever it can.
 * @param t The town to calculate grow counter for
 * @param prev_growth_rate Town growth rate before it changed (one that was used with grow counter to be updated)
 */
static void UpdateTownGrowCounter(Town *t, uint16 prev_growth_rate)
{
	if (t->growth_rate == TOWN_GROWTH_RATE_NONE) return;
	if (prev_growth_rate == TOWN_GROWTH_RATE_NONE) {
		t->grow_counter = min(t->growth_rate, t->grow_counter);
		return;
	}
	t->grow_counter = RoundDivSU((uint32)t->grow_counter * (t->growth_rate + 1), prev_growth_rate + 1);
}

/**
 * Calculates amount of active stations in the range of town (HZB_TOWN_EDGE).
 * @param t The town to calculate stations for
 * @returns Amount of active stations
 */
static int CountActiveStations(Town *t)
{
	int n = 0;
	ForAllStationsNearTown(t, [&](const Station * st) {
		if (st->time_since_load <= 20 || st->time_since_unload <= 20) {
			n++;
		}
	});
	return n;
}

/**
 * Calculates town growth rate in normal conditions (custom growth rate not set).
 * If town growth speed is set to None(0) returns the same rate as if it was Normal(2).
 * @param t The town to calculate growth rate for
 * @returns Calculated growth rate
 */
static uint GetNormalGrowthRate(Town *t)
{
	/**
	 * Note:
	 * Unserviced+unfunded towns get an additional malus in UpdateTownGrowth(),
	 * so the "320" is actually not better than the "420".
	 */
	static const uint16 _grow_count_values[2][6] = {
		{ 120, 120, 120, 100,  80,  60 }, // Fund new buildings has been activated
		{ 320, 420, 300, 220, 160, 100 }  // Normal values
	};

	int n = CountActiveStations(t);
	uint16 m = _grow_count_values[t->fund_buildings_months != 0 ? 0 : 1][min(n, 5)];

	int growth_multiplier;
	if (_settings_game.economy.town_growth_rate == 0) {
		growth_multiplier = 1;
	} else if (_settings_game.economy.town_growth_rate > 0) {
		growth_multiplier = _settings_game.economy.town_growth_rate - 1;
	} else {
		growth_multiplier = _settings_game.economy.town_growth_rate;
	}

	if (growth_multiplier < 0) {
		m <<= (-growth_multiplier);
	} else {
		m >>= growth_multiplier;
	}
	if (t->larger_town) m /= 2;

	if (_settings_game.economy.town_growth_cargo_transported > 0) {
		uint32 inverse_m = UINT32_MAX / m;
		auto calculate_cargo_ratio_fix15 = [](const TransportedCargoStat<uint32> &stat) -> uint32 {
			return stat.old_max ? ((uint64) (stat.old_act << 15)) / stat.old_max : 1 << 15;
		};
		uint32 cargo_ratio_fix16 = calculate_cargo_ratio_fix15(t->supplied[CT_PASSENGERS]) + calculate_cargo_ratio_fix15(t->supplied[CT_MAIL]);
		uint32 cargo_dependant_part = (((uint64) cargo_ratio_fix16) * ((uint64) inverse_m) * _settings_game.economy.town_growth_cargo_transported) >> 16;
		uint32 non_cargo_dependant_part = ((uint64) inverse_m) * (100 - _settings_game.economy.town_growth_cargo_transported);
		uint32 total = (cargo_dependant_part + non_cargo_dependant_part);
		if (total == 0) {
			ClrBit(t->flags, TOWN_IS_GROWING);
			return UINT16_MAX;
		}
		m = ((uint64) UINT32_MAX * 100) / total;
	}

	return TownTicksToGameTicks(m / (t->cache.num_houses / 50 + 1));
}

/**
 * Updates town growth rate.
 * @param t The town to update growth rate for
 */
static void UpdateTownGrowthRate(Town *t)
{
	if (HasBit(t->flags, TOWN_CUSTOM_GROWTH)) return;
	uint old_rate = t->growth_rate;
	t->growth_rate = GetNormalGrowthRate(t);
	UpdateTownGrowCounter(t, old_rate);
	SetWindowDirty(WC_TOWN_VIEW, t->index);
}

/**
 * Updates town growth state (whether it is growing or not).
 * @param t The town to update growth for
 */
static void UpdateTownGrowth(Town *t)
{
	auto guard = scope_guard([t]() {
		SetWindowDirty(WC_TOWN_VIEW, t->index);
	});

	SetBit(t->flags, TOWN_IS_GROWING);
	UpdateTownGrowthRate(t);
	if (!HasBit(t->flags, TOWN_IS_GROWING)) return;

	ClrBit(t->flags, TOWN_IS_GROWING);

	if (_settings_game.economy.town_growth_rate == 0 && t->fund_buildings_months == 0) return;

	if (t->fund_buildings_months == 0) {
		/* Check if all goals are reached for this town to grow (given we are not funding it) */
		for (int i = TE_BEGIN; i < TE_END; i++) {
			switch (t->goal[i]) {
				case TOWN_GROWTH_WINTER:
					if (TileHeight(t->xy) >= GetSnowLine() && t->received[i].old_act == 0 && t->cache.population > 90) return;
					break;
				case TOWN_GROWTH_DESERT:
					if (GetTropicZone(t->xy) == TROPICZONE_DESERT && t->received[i].old_act == 0 && t->cache.population > 60) return;
					break;
				default:
					if (t->goal[i] > t->received[i].old_act) return;
					break;
			}
		}
	}

	if (HasBit(t->flags, TOWN_CUSTOM_GROWTH)) {
		if (t->growth_rate != TOWN_GROWTH_RATE_NONE) SetBit(t->flags, TOWN_IS_GROWING);
		SetWindowDirty(WC_TOWN_VIEW, t->index);
		return;
	}

	if (t->fund_buildings_months == 0 && CountActiveStations(t) == 0 && !Chance16(1, 12)) return;

	SetBit(t->flags, TOWN_IS_GROWING);
}

static void UpdateTownAmounts(Town *t)
{
	for (CargoID i = 0; i < NUM_CARGO; i++) t->supplied[i].NewMonth();
	for (int i = TE_BEGIN; i < TE_END; i++) t->received[i].NewMonth();
	if (t->fund_buildings_months != 0) t->fund_buildings_months--;

	SetWindowDirty(WC_TOWN_VIEW, t->index);
}

static void UpdateTownUnwanted(Town *t)
{
	for (const Company *c : Company::Iterate()) {
		if (t->unwanted[c->index] > 0) t->unwanted[c->index]--;
	}
}

/**
 * Checks whether the local authority allows construction of a new station (rail, road, airport, dock) on the given tile
 * @param tile The tile where the station shall be constructed.
 * @param flags Command flags. DC_NO_TEST_TOWN_RATING is tested.
 * @return Succeeded or failed command.
 */
CommandCost CheckIfAuthorityAllowsNewStation(TileIndex tile, DoCommandFlag flags)
{
	if (!Company::IsValidID(_current_company) || (flags & DC_NO_TEST_TOWN_RATING)) return CommandCost();

	Town *t = ClosestTownFromTile(tile, _settings_game.economy.dist_local_authority);
	if (t == nullptr) return CommandCost();

	if (t->ratings[_current_company] > RATING_VERYPOOR) return CommandCost();

	SetDParam(0, t->index);
	return_cmd_error(STR_ERROR_LOCAL_AUTHORITY_REFUSES_TO_ALLOW_THIS);
}

/**
 * Return the town closest to the given tile within \a threshold.
 * @param tile      Starting point of the search.
 * @param threshold Biggest allowed distance to the town.
 * @return Closest town to \a tile within \a threshold, or \c nullptr if there is no such town.
 *
 * @note This function only uses distance, the #ClosestTownFromTile function also takes town ownership into account.
 */
Town *CalcClosestTownFromTile(TileIndex tile, uint threshold)
{
	if (Town::GetNumItems() == 0) return nullptr;

	TownID tid = _town_kdtree.FindNearest(TileX(tile), TileY(tile));
	Town *town = Town::Get(tid);
	if (DistanceManhattan(tile, town->xy) < threshold) return town;
	return nullptr;
}

/**
 * Return the town closest (in distance or ownership) to a given tile, within a given threshold.
 * @param tile      Starting point of the search.
 * @param threshold Biggest allowed distance to the town.
 * @return Closest town to \a tile within \a threshold, or \c nullptr if there is no such town.
 *
 * @note If you only care about distance, you can use the #CalcClosestTownFromTile function.
 */
Town *ClosestTownFromTile(TileIndex tile, uint threshold)
{
	switch (GetTileType(tile)) {
		case MP_ROAD:
			if (IsRoadDepot(tile)) return CalcClosestTownFromTile(tile, threshold);

			if (!HasTownOwnedRoad(tile)) {
				TownID tid = GetTownIndex(tile);

				if (tid == INVALID_TOWN) {
					/* in the case we are generating "many random towns", this value may be INVALID_TOWN */
					if (_generating_world) return CalcClosestTownFromTile(tile, threshold);
					assert(Town::GetNumItems() == 0);
					return nullptr;
				}

				assert(Town::IsValidID(tid));
				Town *town = Town::Get(tid);

				if (DistanceManhattan(tile, town->xy) >= threshold) town = nullptr;

				return town;
			}
			FALLTHROUGH;

		case MP_HOUSE:
			return Town::GetByTile(tile);

		default:
			return CalcClosestTownFromTile(tile, threshold);
	}
}

static bool _town_rating_test = false; ///< If \c true, town rating is in test-mode.
static SmallMap<const Town *, int> _town_test_ratings; ///< Map of towns to modified ratings, while in town rating test-mode.

/**
 * Switch the town rating to test-mode, to allow commands to be tested without affecting current ratings.
 * The function is safe to use in nested calls.
 * @param mode Test mode switch (\c true means go to test-mode, \c false means leave test-mode).
 */
void SetTownRatingTestMode(bool mode)
{
	static int ref_count = 0; // Number of times test-mode is switched on.
	if (mode) {
		if (ref_count == 0) {
			_town_test_ratings.clear();
		}
		ref_count++;
	} else {
		assert(ref_count > 0);
		ref_count--;
	}
	_town_rating_test = !(ref_count == 0);
}

/**
 * Get the rating of a town for the #_current_company.
 * @param t Town to get the rating from.
 * @return Rating of the current company in the given town.
 */
static int GetRating(const Town *t)
{
	if (_town_rating_test) {
		SmallMap<const Town *, int>::iterator it = _town_test_ratings.Find(t);
		if (it != _town_test_ratings.End()) {
			return it->second;
		}
	}
	return t->ratings[_current_company];
}

/**
 * Changes town rating of the current company
 * @param t Town to affect
 * @param add Value to add
 * @param max Minimum (add < 0) resp. maximum (add > 0) rating that should be achievable with this change.
 * @param flags Command flags, especially DC_NO_MODIFY_TOWN_RATING is tested
 */
void ChangeTownRating(Town *t, int add, int max, DoCommandFlag flags)
{
	/* if magic_bulldozer cheat is active, town doesn't penalize for removing stuff */
	if (t == nullptr || (flags & DC_NO_MODIFY_TOWN_RATING) ||
			!Company::IsValidID(_current_company) ||
			(_cheats.magic_bulldozer.value && add < 0)) {
		return;
	}

	const int prev_rating = GetRating(t);
	int rating = prev_rating;
	if (add < 0) {
		if (rating > max) {
			rating += add;
			if (rating < max) rating = max;
		}
	} else {
		if (rating < max) {
			rating += add;
			if (rating > max) rating = max;
		}
	}
	if (_town_rating_test) {
		_town_test_ratings[t] = rating;
	} else {
		if (_local_company == _current_company && (!HasBit(t->have_ratings, _current_company) || ((prev_rating > 0) != (rating > 0)))) {
			ZoningTownAuthorityRatingChange();
		}
		SetBit(t->have_ratings, _current_company);
		t->ratings[_current_company] = rating;
		t->UpdateVirtCoord();
		SetWindowDirty(WC_TOWN_AUTHORITY, t->index);
	}
}

/**
 * Does the town authority allow the (destructive) action of the current company?
 * @param flags Checking flags of the command.
 * @param t     Town that must allow the company action.
 * @param type  Type of action that is wanted.
 * @return A succeeded command if the action is allowed, a failed command if it is not allowed.
 */
CommandCost CheckforTownRating(DoCommandFlag flags, Town *t, TownRatingCheckType type)
{
	/* if magic_bulldozer cheat is active, town doesn't restrict your destructive actions */
	if (t == nullptr || !Company::IsValidID(_current_company) ||
			_cheats.magic_bulldozer.value || (flags & DC_NO_TEST_TOWN_RATING)) {
		return CommandCost();
	}

	/* minimum rating needed to be allowed to remove stuff */
	static const int needed_rating[][TOWN_RATING_CHECK_TYPE_COUNT] = {
		/*                  ROAD_REMOVE,                    TUNNELBRIDGE_REMOVE */
		{ RATING_ROAD_NEEDED_PERMISSIVE, RATING_TUNNEL_BRIDGE_NEEDED_PERMISSIVE}, // Permissive
		{    RATING_ROAD_NEEDED_NEUTRAL,    RATING_TUNNEL_BRIDGE_NEEDED_NEUTRAL}, // Neutral
		{    RATING_ROAD_NEEDED_HOSTILE,    RATING_TUNNEL_BRIDGE_NEEDED_HOSTILE}, // Hostile
	};

	/* check if you're allowed to remove the road/bridge/tunnel
	 * owned by a town no removal if rating is lower than ... depends now on
	 * difficulty setting. Minimum town rating selected by difficulty level
	 */
	int needed = needed_rating[_settings_game.difficulty.town_council_tolerance][type];

	if (GetRating(t) < needed) {
		SetDParam(0, t->index);
		return_cmd_error(STR_ERROR_LOCAL_AUTHORITY_REFUSES_TO_ALLOW_THIS);
	}

	return CommandCost();
}

void TownsMonthlyLoop()
{
	for (Town *t : Town::Iterate()) {
		if (t->road_build_months != 0) t->road_build_months--;

		if (t->exclusive_counter != 0) {
			if (--t->exclusive_counter == 0) t->exclusivity = INVALID_COMPANY;
		}

		UpdateTownAmounts(t);
		UpdateTownGrowth(t);
		UpdateTownRating(t);
		UpdateTownUnwanted(t);
		UpdateTownCargoes(t);
	}

	UpdateTownCargoBitmap();
}

void TownsYearlyLoop()
{
	/* Increment house ages */
	for (TileIndex t = 0; t < MapSize(); t++) {
		if (!IsTileType(t, MP_HOUSE)) continue;
		IncrementHouseAge(t);
	}
}

static CommandCost TerraformTile_Town(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	if (AutoslopeEnabled()) {
		HouseID house = GetHouseType(tile);
		GetHouseNorthPart(house); // modifies house to the ID of the north tile
		const HouseSpec *hs = HouseSpec::Get(house);

		/* Here we differ from TTDP by checking TILE_NOT_SLOPED */
		if (((hs->building_flags & TILE_NOT_SLOPED) == 0) && !IsSteepSlope(tileh_new) &&
				(GetTileMaxZ(tile) == z_new + GetSlopeMaxZ(tileh_new))) {
			bool allow_terraform = true;

			/* Call the autosloping callback per tile, not for the whole building at once. */
			house = GetHouseType(tile);
			hs = HouseSpec::Get(house);
			if (HasBit(hs->callback_mask, CBM_HOUSE_AUTOSLOPE)) {
				/* If the callback fails, allow autoslope. */
				uint16 res = GetHouseCallback(CBID_HOUSE_AUTOSLOPE, 0, 0, house, Town::GetByTile(tile), tile);
				if (res != CALLBACK_FAILED && ConvertBooleanCallback(hs->grf_prop.grffile, CBID_HOUSE_AUTOSLOPE, res)) allow_terraform = false;
			}

			if (allow_terraform) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
		}
	}

	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}

/** Tile callback functions for a town */
extern const TileTypeProcs _tile_type_town_procs = {
	DrawTile_Town,           // draw_tile_proc
	GetSlopePixelZ_Town,     // get_slope_z_proc
	ClearTile_Town,          // clear_tile_proc
	AddAcceptedCargo_Town,   // add_accepted_cargo_proc
	GetTileDesc_Town,        // get_tile_desc_proc
	GetTileTrackStatus_Town, // get_tile_track_status_proc
	nullptr,                    // click_tile_proc
	AnimateTile_Town,        // animate_tile_proc
	TileLoop_Town,           // tile_loop_proc
	ChangeTileOwner_Town,    // change_tile_owner_proc
	AddProducedCargo_Town,   // add_produced_cargo_proc
	nullptr,                    // vehicle_enter_tile_proc
	GetFoundation_Town,      // get_foundation_proc
	TerraformTile_Town,      // terraform_tile_proc
};


HouseSpec _house_specs[NUM_HOUSES];

void ResetHouses()
{
	memset(&_house_specs, 0, sizeof(_house_specs));
	memcpy(&_house_specs, &_original_house_specs, sizeof(_original_house_specs));

	/* Reset any overrides that have been set. */
	_house_mngr.ResetOverride();
}
