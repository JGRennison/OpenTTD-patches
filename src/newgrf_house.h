/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_house.h Functions related to NewGRF houses. */

#ifndef NEWGRF_HOUSE_H
#define NEWGRF_HOUSE_H

#include "newgrf_callbacks.h"
#include "tile_cmd.h"
#include "house_type.h"
#include "newgrf_spritegroup.h"
#include "newgrf_town.h"

struct CommonHouseScopeResolver : public ScopeResolver {
	HouseID house_id;              ///< Type of house being queried.

	CommonHouseScopeResolver(ResolverObject &ro, HouseID house_id)
		: ScopeResolver(ro), house_id(house_id)
	{ }
};

/** Scope resolver for houses. */
struct HouseScopeResolver : public CommonHouseScopeResolver {
	TileIndex tile;                    ///< Tile of this house.
	Town *town;                        ///< Town of this house.
	bool not_yet_constructed;          ///< True for construction check.
	uint16_t initial_random_bits;      ///< Random bits during construction checks.
	CargoTypes watched_cargo_triggers; ///< Cargo types that triggered the watched cargo callback.

	/**
	 * Constructor of a house scope resolver.
	 * @param ro Surrounding resolver.
	 * @param house_id House type being queried.
	 * @param tile %Tile containing the house.
	 * @param town %Town containing the house.
	 * @param not_yet_constructed House is still under construction.
	 * @param initial_random_bits Random bits during construction checks.
	 * @param watched_cargo_triggers Cargo types that triggered the watched cargo callback.
	 */
	HouseScopeResolver(ResolverObject &ro, HouseID house_id, TileIndex tile, Town *town,
			bool not_yet_constructed, uint8_t initial_random_bits, CargoTypes watched_cargo_triggers)
		: CommonHouseScopeResolver(ro, house_id), tile(tile), town(town), not_yet_constructed(not_yet_constructed),
		initial_random_bits(initial_random_bits), watched_cargo_triggers(watched_cargo_triggers)
	{
	}

	uint32_t GetRandomBits() const override;
	uint32_t GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra *extra) const override;
	uint32_t GetTriggers() const override;
};

/**
 * Fake scope resolver for nonexistent houses.
 *
 * The purpose of this class is to provide a house resolver for a given house type
 * but not an actual house instatntion. We need this when e.g. drawing houses in
 * GUI to keep backward compatibility with GRFs that were created before this
 * functionality. When querying house sprites, certain GRF may read various house
 * variables e.g. the town zone where the building is located or the XY coordinates.
 * Since the building doesn't exists we have no real values that we can return.
 * Instead of failing, this resolver will return fake values.
 */
struct FakeHouseScopeResolver : public CommonHouseScopeResolver {
	FakeHouseScopeResolver(ResolverObject &ro, HouseID house_id)
		: CommonHouseScopeResolver(ro, house_id)
	{ }

	/* virtual */ uint32_t GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra *extra) const override;
};

/** Resolver object to be used for houses (feature 07 spritegroups). */
struct HouseResolverObject : public ResolverObject {
	HouseScopeResolver house_scope;
	TownScopeResolver  town_scope;

	HouseResolverObject(HouseID house_id, TileIndex tile, Town *town,
			CallbackID callback = CBID_NO_CALLBACK, uint32_t param1 = 0, uint32_t param2 = 0,
			bool not_yet_constructed = false, uint8_t initial_random_bits = 0, CargoTypes watched_cargo_triggers = 0);

	ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, VarSpriteGroupScopeOffset relative = 0) override
	{
		switch (scope) {
			case VSG_SCOPE_SELF:   return &this->house_scope;
			case VSG_SCOPE_PARENT: return &this->town_scope;
			default: return ResolverObject::GetScope(scope, relative);
		}
	}

	GrfSpecFeature GetFeature() const override;
	uint32_t GetDebugID() const override;
};

/** Resolver object to be used for fake houses (feature 07 spritegroups). */
struct FakeHouseResolverObject : public ResolverObject {
	FakeHouseScopeResolver house_scope;
	FakeTownScopeResolver  town_scope;

	FakeHouseResolverObject(HouseID house_id,
			CallbackID callback = CBID_NO_CALLBACK, uint32_t param1 = 0, uint32_t param2 = 0);

	ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, VarSpriteGroupScopeOffset relative = 0) override
	{
		switch (scope) {
			case VSG_SCOPE_SELF:   return &this->house_scope;
			case VSG_SCOPE_PARENT: return &this->town_scope;
			default: return ResolverObject::GetScope(scope, relative);
		}
	}

	GrfSpecFeature GetFeature() const override;
	uint32_t GetDebugID() const override;
};

/**
 * Makes class IDs unique to each GRF file.
 * Houses can be assigned class IDs which are only comparable within the GRF
 * file they were defined in. This mapping ensures that if two houses have the
 * same class as defined by the GRF file, the classes are different within the
 * game. An array of HouseClassMapping structs is created, and the array index
 * of the struct that matches both the GRF ID and the class ID is the class ID
 * used in the game.
 *
 * Although similar to the HouseIDMapping struct above, this serves a different
 * purpose. Since the class ID is not saved anywhere, this mapping does not
 * need to be persistent; it just needs to keep class ids unique.
 */
struct HouseClassMapping {
	uint32_t grfid;     ///< The GRF ID of the file this class belongs to
	uint8_t  class_id;  ///< The class id within the grf file
};

void ResetHouseClassIDs();
HouseClassID AllocateHouseClassID(byte grf_class_id, uint32_t grfid);

void InitializeBuildingCounts();
void IncreaseBuildingCount(Town *t, HouseID house_id);
void DecreaseBuildingCount(Town *t, HouseID house_id);

void DrawNewHouseTile(TileInfo *ti, HouseID house_id);
void DrawNewHouseTileInGUI(int x, int y, HouseID house_id, bool ground);
void AnimateNewHouseTile(TileIndex tile);
void AnimateNewHouseConstruction(TileIndex tile);
uint8_t GetNewHouseTileAnimationSpeed(TileIndex tile);

uint16_t GetHouseCallback(CallbackID callback, uint32_t param1, uint32_t param2, HouseID house_id, Town *town = nullptr, TileIndex tile = INVALID_TILE,
		bool not_yet_constructed = false, uint8_t initial_random_bits = 0, CargoTypes watched_cargo_triggers = 0);
void WatchedCargoCallback(TileIndex tile, CargoTypes trigger_cargoes);

bool HouseAllowsConstruction(HouseID house_id, TileIndex tile, Town *t, byte random_bits);
bool CanDeleteHouse(TileIndex tile);

bool NewHouseTileLoop(TileIndex tile);

enum HouseTrigger {
	/* The tile of the house has been triggered during the tileloop. */
	HOUSE_TRIGGER_TILE_LOOP     = 0x01,
	/*
	 * The top tile of a (multitile) building has been triggered during and all
	 * the tileloop other tiles of the same building get the same random value.
	 */
	HOUSE_TRIGGER_TILE_LOOP_TOP = 0x02,
};
void TriggerHouse(TileIndex t, HouseTrigger trigger);

void AnalyseHouseSpriteGroups();

#endif /* NEWGRF_HOUSE_H */
