/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_object.cpp Handling of object NewGRFs. */

#include "stdafx.h"
#include "company_base.h"
#include "company_func.h"
#include "debug.h"
#include "genworld.h"
#include "newgrf_badge.h"
#include "newgrf_class_func.h"
#include "newgrf_object.h"
#include "newgrf_sound.h"
#include "object_base.h"
#include "object_map.h"
#include "tile_cmd.h"
#include "town.h"
#include "water.h"
#include "clear_func.h"
#include "newgrf_animation_base.h"
#include "newgrf_extension.h"
#include "newgrf_dump.h"

#include "safeguards.h"

/** The override manager for our objects. */
ObjectOverrideManager _object_mngr(NEW_OBJECT_OFFSET, NUM_OBJECTS, INVALID_OBJECT_TYPE);

extern const ObjectSpec _original_objects[NEW_OBJECT_OFFSET];
/** All the object specifications. */
std::vector<ObjectSpec> _object_specs;

const std::vector<ObjectSpec> &ObjectSpec::Specs()
{
	return _object_specs;
}

size_t ObjectSpec::Count()
{
	return _object_specs.size();
}

/**
 * Get the specification associated with a specific ObjectType.
 * @param index The object type to fetch.
 * @return The specification.
 */
/* static */ const ObjectSpec *ObjectSpec::Get(ObjectType index)
{
	/* Empty object if index is out of range -- this might happen if NewGRFs are changed. */
	static ObjectSpec empty = {};

	assert(index < NUM_OBJECTS);
	if (index >= _object_specs.size()) return &empty;
	return &_object_specs[index];
}

/**
 * Get the specification associated with a tile.
 * @param tile The tile to fetch the data for.
 * @return The specification.
 */
/* static */ const ObjectSpec *ObjectSpec::GetByTile(TileIndex tile)
{
	return ObjectSpec::Get(GetObjectType(tile));
}

/**
 * Check whether the object might be available at some point in this game with the current game mode.
 * @return true if it might be available.
 */
bool ObjectSpec::IsEverAvailable() const
{
	return this->IsEnabled() && this->climate.Test(_settings_game.game_creation.landscape) &&
			!this->flags.Test((_game_mode != GM_EDITOR && !_generating_world) ? ObjectFlag::OnlyInScenedit : ObjectFlag::OnlyInGame);
}

/**
 * Check whether the object was available at some point in the past or present in this game with the current game mode.
 * @return true if it was ever or is available.
 */
bool ObjectSpec::WasEverAvailable() const
{
	return this->IsEverAvailable() && ((CalTime::CurDate() > this->introduction_date) || (_settings_game.construction.ignore_object_intro_dates && !_generating_world));
}

/**
 * Check whether the object is available at this time.
 * @return true if it is available.
 */
bool ObjectSpec::IsAvailable() const
{
	return this->WasEverAvailable() &&
			((CalTime::CurDate() < this->end_of_life_date) || (this->end_of_life_date < this->introduction_date + 365) ||
			(_settings_game.construction.no_expire_objects_after != 0 && CalTime::CurYear() >= _settings_game.construction.no_expire_objects_after));
}

/**
 * Gets the index of this spec.
 * @return The index.
 */
uint ObjectSpec::Index() const
{
	return this - _object_specs.data();
}

/**
 * Tie all ObjectSpecs to their class.
 */
/* static */ void ObjectSpec::BindToClasses()
{
	for (auto &spec : _object_specs) {
		if (spec.IsEnabled() && spec.class_index != INVALID_OBJECT_CLASS) {
			ObjectClass::Assign(&spec);
		}
	}
}

/** This function initialize the spec arrays of objects. */
void ResetObjects()
{
	/* Clean the pool. */
	_object_specs.clear();

	/* And add our originals. */
	_object_specs.reserve(lengthof(_original_objects));

	for (uint16_t i = 0; i < lengthof(_original_objects); i++) {
		ObjectSpec &spec = _object_specs.emplace_back(_original_objects[i]);
		spec.grf_prop.local_id = i;
	}

	/* Set class for originals. */
	_object_specs[OBJECT_LIGHTHOUSE].class_index = ObjectClass::Allocate('LTHS');
	_object_specs[OBJECT_TRANSMITTER].class_index = ObjectClass::Allocate('TRNS');
}

template <>
/* static */ void ObjectClass::InsertDefaults()
{
	ObjectClass::Get(ObjectClass::Allocate('LTHS'))->name = STR_OBJECT_CLASS_LTHS;
	ObjectClass::Get(ObjectClass::Allocate('TRNS'))->name = STR_OBJECT_CLASS_TRNS;
}

template <>
bool ObjectClass::IsUIAvailable(uint index) const
{
	return this->GetSpec(index)->IsEverAvailable();
}

/* Instantiate ObjectClass. */
template class NewGRFClass<ObjectSpec, ObjectClassID, OBJECT_CLASS_MAX>;

/* virtual */ uint32_t ObjectScopeResolver::GetRandomBits() const
{
	return IsValidTile(this->tile) && IsTileType(this->tile, MP_OBJECT) ? GetObjectRandomBits(this->tile) : 0;
}

/**
 * Make an analysis of a tile and get the object type.
 * @param tile TileIndex of the tile to query
 * @param cur_grfid GRFID of the current callback chain
 * @return value encoded as per NFO specs
 */
static uint32_t GetObjectIDAtOffset(TileIndex tile, uint32_t cur_grfid)
{
	if (!IsTileType(tile, MP_OBJECT)) {
		return 0xFFFF;
	}

	const Object *o = Object::GetByTile(tile);
	const ObjectSpec *spec = ObjectSpec::Get(o->type);

	/* Default objects have no associated NewGRF file */
	if (!spec->grf_prop.HasGrfFile()) {
		return 0xFFFE; // Defined in another grf file
	}

	if (spec->grf_prop.grfid == cur_grfid) { // same object, same grf ?
		return spec->grf_prop.local_id | o->view << 16;
	}

	return 0xFFFE; // Defined in another grf file
}

/**
 * Based on newhouses equivalent, but adapted for newobjects
 * @param parameter from callback.  It's in fact a pair of coordinates
 * @param tile TileIndex from which the callback was initiated
 * @param index of the object been queried for
 * @param grf_version8 True, if we are dealing with a new NewGRF which uses GRF version >= 8.
 * @return a construction of bits obeying the newgrf format
 */
static uint32_t GetNearbyObjectTileInformation(uint8_t parameter, TileIndex tile, ObjectID index, bool grf_version8, uint32_t mask)
{
	if (parameter != 0) tile = GetNearbyTile(parameter, tile); // only perform if it is required
	bool is_same_object = (IsTileType(tile, MP_OBJECT) && GetObjectIndex(tile) == index);

	uint32_t result = (is_same_object ? 1 : 0) << 8;
	if (mask & ~0x100) result |= GetNearbyTileInformation(tile, grf_version8, mask);
	return result;
}

/**
 * Get the closest object of a given type.
 * @param tile    The tile to start searching from.
 * @param type    The type of the object to search for.
 * @param current The current object (to ignore).
 * @return The distance to the closest object.
 */
static uint32_t GetClosestObject(TileIndex tile, ObjectType type, const Object *current)
{
	uint32_t best_dist = UINT32_MAX;
	for (const Object *o : Object::Iterate()) {
		if (o->type != type || o == current) continue;

		best_dist = std::min(best_dist, DistanceManhattan(tile, o->location.tile));
	}

	return best_dist;
}

/**
 * Implementation of var 65
 * @param local_id Parameter given to the callback, which is the set id, or the local id, in our terminology.
 * @param grfid    The object's GRFID.
 * @param tile     The tile to look from.
 * @param current  Object for which the inquiry is made
 * @return The formatted answer to the callback : rr(reserved) cc(count) dddd(manhattan distance of closest sister)
 */
static uint32_t GetCountAndDistanceOfClosestInstance(uint32_t local_id, uint32_t grfid, TileIndex tile, const Object *current)
{
	uint32_t grf_id = GetRegister(0x100);  // Get the GRFID of the definition to look for in register 100h
	uint32_t idx;

	/* Determine what will be the object type to look for */
	switch (grf_id) {
		case 0:  // this is a default object type
			idx = local_id;
			break;

		case 0xFFFFFFFF: // current grf
			grf_id = grfid;
			[[fallthrough]];

		default: // use the grfid specified in register 100h
			idx = _object_mngr.GetID(local_id, grf_id);
			break;
	}

	/* If the object type is invalid, there is none and the closest is far away. */
	if (idx >= NUM_OBJECTS) return 0 | 0xFFFF;

	return Object::GetTypeCount(idx) << 16 | ClampTo<uint16_t>(GetClosestObject(tile, idx, current));
}

/** Used by the resolver to get values for feature 0F deterministic spritegroups. */
/* virtual */ uint32_t ObjectScopeResolver::GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra &extra) const
{
	/* We get the town from the object, or we calculate the closest
	 * town if we need to when there's no object. */
	const Town *t = nullptr;

	if (this->obj == nullptr) {
		switch (variable) {
			/* Allow these when there's no object. */
			case 0x41:
			case 0x60:
			case 0x61:
			case 0x62:
			case 0x64:
				break;

			/* Allow these, but find the closest town. */
			case 0x45:
			case 0x46:
				if (!IsValidTile(this->tile)) goto unhandled;
				t = ClosestTownFromTile(this->tile, UINT_MAX);
				break;

			/* Construction date */
			case 0x42: return CalTime::CurDate().base();

			/* Object founder information */
			case 0x44: return _current_company;

			/* Object view */
			case 0x48: return this->view;

			case 0x7A: return GetBadgeVariableResult(*this->ro.grffile, this->spec->badges, parameter);

			case A2VRI_OBJECT_FOUNDATION_SLOPE:
				return GetTileSlope(this->tile);

			case A2VRI_OBJECT_FOUNDATION_SLOPE_CHANGE:
				return 0;

			/*
			 * Disallow the rest:
			 * 0x40: Relative position is passed as parameter during construction.
			 * 0x43: Animation counter is only for actual tiles.
			 * 0x47: Object colour is only valid when its built.
			 * 0x63: Animation counter of nearby tile, see above.
			 */
			default:
				goto unhandled;
		}

		/* If there's an invalid tile, then we don't have enough information at all. */
		if (!IsValidTile(this->tile)) goto unhandled;
	} else {
		t = this->obj->town;
	}

	switch (variable) {
		/* Relative position. */
		case 0x40: {
			TileIndexDiffCUnsigned offset = TileIndexToTileIndexDiffCUnsigned(this->tile, this->obj->location.tile);
			return offset.y << 20 | offset.x << 16 | offset.y << 8 | offset.x;
		}

		/* Tile information. */
		case 0x41: return GetTileSlope(this->tile) << 8 | GetTerrainType(this->tile);

		/* Construction date */
		case 0x42: return this->obj->build_date.base();

		/* Animation counter */
		case 0x43: return GetAnimationFrame(this->tile);

		/* Object founder information */
		case 0x44: return GetTileOwner(this->tile);

		/* Get town zone and Manhattan distance of closest town */
		case 0x45: return (t == nullptr) ? 0 : (GetTownRadiusGroup(t, this->tile) << 16 | ClampTo<uint16_t>(DistanceManhattan(this->tile, t->xy)));

		/* Get square of Euclidean distance of closest town */
		case 0x46: return (t == nullptr) ? 0 : DistanceSquare(this->tile, t->xy);

		/* Object colour */
		case 0x47: return this->obj->colour;

		/* Object view */
		case 0x48: return this->obj->view;

		/* Get object ID at offset param */
		case 0x60: return GetObjectIDAtOffset(GetNearbyTile(parameter, this->tile), this->ro.grffile->grfid);

		/* Get random tile bits at offset param */
		case 0x61: {
			TileIndex tile = GetNearbyTile(parameter, this->tile);
			return (IsTileType(tile, MP_OBJECT) && Object::GetByTile(tile) == this->obj) ? GetObjectRandomBits(tile) : 0;
		}

		/* Land info of nearby tiles */
		case 0x62: return GetNearbyObjectTileInformation(parameter, this->tile, this->obj == nullptr ? INVALID_OBJECT : this->obj->index, this->ro.grffile->grf_version >= 8, extra.mask);

		/* Animation counter of nearby tile */
		case 0x63: {
			TileIndex tile = GetNearbyTile(parameter, this->tile);
			return (IsTileType(tile, MP_OBJECT) && Object::GetByTile(tile) == this->obj) ? GetAnimationFrame(tile) : 0;
		}

		/* Count of object, distance of closest instance */
		case 0x64: return GetCountAndDistanceOfClosestInstance(parameter, this->ro.grffile->grfid, this->tile, this->obj);

		case 0x7A: return GetBadgeVariableResult(*this->ro.grffile, this->spec->badges, parameter);

		case A2VRI_OBJECT_FOUNDATION_SLOPE: {
			extern Foundation GetFoundation_Object(TileIndex tile, Slope tileh);
			Slope slope = GetTileSlope(this->tile);
			ApplyFoundationToSlope(GetFoundation_Object(this->tile, slope), slope);
			return slope;
		}

		case A2VRI_OBJECT_FOUNDATION_SLOPE_CHANGE: {
			extern Foundation GetFoundation_Object(TileIndex tile, Slope tileh);
			Slope slope = GetTileSlope(this->tile);
			Slope orig_slope = slope;
			ApplyFoundationToSlope(GetFoundation_Object(this->tile, slope), slope);
			return slope ^ orig_slope;
		}
	}

unhandled:
	Debug(grf, 1, "Unhandled object variable 0x{:X}", variable);

	extra.available = false;
	return UINT_MAX;
}

/**
 * Constructor of the object resolver.
 * @param obj Object being resolved.
 * @param tile %Tile of the object.
 * @param view View of the object.
 * @param callback Callback ID.
 * @param param1 First parameter (var 10) of the callback.
 * @param param2 Second parameter (var 18) of the callback.
 */
ObjectResolverObject::ObjectResolverObject(const ObjectSpec *spec, Object *obj, TileIndex tile, uint8_t view,
		CallbackID callback, uint32_t param1, uint32_t param2)
	: ResolverObject(spec->grf_prop.grffile, callback, param1, param2), object_scope(*this, obj, spec, tile, view)
{
	this->root_spritegroup = (obj == nullptr) ? spec->grf_prop.GetSpriteGroup(OBJECT_SPRITE_GROUP_PURCHASE) : nullptr;
	if (this->root_spritegroup == nullptr) this->root_spritegroup = spec->grf_prop.GetSpriteGroup(OBJECT_SPRITE_GROUP_DEFAULT);
}

/**
 * Get the town resolver scope that belongs to this object resolver.
 * On the first call, the town scope is created (if possible).
 * @return Town scope, if available.
 */
TownScopeResolver *ObjectResolverObject::GetTown()
{
	if (!this->town_scope.has_value()) {
		Town *t;
		if (this->object_scope.obj != nullptr) {
			t = this->object_scope.obj->town;
		} else {
			t = ClosestTownFromTile(this->object_scope.tile, UINT_MAX);
		}
		if (t == nullptr) return nullptr;
		this->town_scope.emplace(*this, t, this->object_scope.obj == nullptr);
	}
	return &*this->town_scope;
}

GrfSpecFeature ObjectResolverObject::GetFeature() const
{
	return GSF_OBJECTS;
}

uint32_t ObjectResolverObject::GetDebugID() const
{
	return this->object_scope.spec->grf_prop.local_id;
}

/**
 * Perform a callback for an object.
 * @param callback The callback to perform.
 * @param param1   The first parameter to pass to the NewGRF.
 * @param param2   The second parameter to pass to the NewGRF.
 * @param spec     The specification of the object / the entry point.
 * @param o        The object to call the callback for.
 * @param tile     The tile the callback is called for.
 * @param view     The view of the object (only used when o == nullptr).
 * @return The result of the callback.
 */
uint16_t GetObjectCallback(CallbackID callback, uint32_t param1, uint32_t param2, const ObjectSpec *spec, Object *o, TileIndex tile, uint8_t view)
{
	ObjectResolverObject object(spec, o, tile, view, callback, param1, param2);
	return object.ResolveCallback();
}

void DrawObjectLandscapeGround(TileInfo *ti)
{
	if (IsTileOnWater(ti->tile) && GetObjectGroundType(ti->tile) != OBJECT_GROUND_SHORE) {
		DrawWaterClassGround(ti);
	} else {
		switch (GetObjectGroundType(ti->tile)) {
			case OBJECT_GROUND_GRASS:
				DrawClearLandTile(ti, GetObjectGroundDensity(ti->tile));
				break;

			case OBJECT_GROUND_SNOW_DESERT:
				DrawGroundSprite(GetSpriteIDForSnowDesert(ti->tileh, GetObjectGroundDensity(ti->tile)), PAL_NONE);
				break;

			case OBJECT_GROUND_SHORE:
				DrawShoreTile(ti->tileh);
				break;

			default:
				/* This should never be reached, just draw a black sprite to make the problem clear without being unnecessarily punitive */
				DrawGroundSprite(SPR_FLAT_BARE_LAND + SlopeToSpriteOffset(ti->tileh), PALETTE_ALL_BLACK);
				break;
		}
	}
}

/**
 * Draw an group of sprites on the map.
 * @param ti    Information about the tile to draw on.
 * @param group The group of sprites to draw.
 * @param spec  Object spec to draw.
 */
static void DrawTileLayout(TileInfo *ti, const TileLayoutSpriteGroup *group, const ObjectSpec *spec, int building_z_offset)
{
	const DrawTileSprites *dts = group->ProcessRegisters(nullptr);
	PaletteID palette = (spec->flags.Test(ObjectFlag::Uses2CC) ? SPR_2CCMAP_BASE : PALETTE_RECOLOUR_START) + Object::GetByTile(ti->tile)->colour;

	SpriteID image = dts->ground.sprite;
	PaletteID pal  = dts->ground.pal;

	if (spec->ctrl_flags.Test(ObjectCtrlFlag::UseLandGround)) {
		DrawObjectLandscapeGround(ti);
	} else if (GB(image, 0, SPRITE_WIDTH) != 0) {
		/* If the ground sprite is the default flat water sprite, draw also canal/river borders
		 * Do not do this if the tile's WaterClass is 'land'. */
		if ((image == SPR_FLAT_WATER_TILE || spec->flags.Test(ObjectFlag::DrawWater)) && IsTileOnWater(ti->tile)) {
			DrawWaterClassGround(ti);
		} else {
			DrawGroundSprite(image, GroundSpritePaletteTransform(image, pal, palette));
		}
	}

	if (building_z_offset) ti->z += building_z_offset;
	DrawNewGRFTileSeq(ti, dts, TO_STRUCTURES, 0, palette);
	if (building_z_offset) ti->z -= building_z_offset;
}

/**
 * Draw an object on the map.
 * @param ti   Information about the tile to draw on.
 * @param spec Object spec to draw.
 */
void DrawNewObjectTile(TileInfo *ti, const ObjectSpec *spec, int building_z_offset)
{
	Object *o = Object::GetByTile(ti->tile);
	ObjectResolverObject object(spec, o, ti->tile);

	const SpriteGroup *group = object.Resolve();
	if (group == nullptr || group->type != SGT_TILELAYOUT) return;

	DrawTileLayout(ti, (const TileLayoutSpriteGroup *)group, spec, building_z_offset);
}

/**
 * Draw representation of an object (tile) for GUI purposes.
 * @param x    Position x of image.
 * @param y    Position y of image.
 * @param spec Object spec to draw.
 * @param view The object's view.
 */
void DrawNewObjectTileInGUI(int x, int y, const ObjectSpec *spec, uint8_t view)
{
	ObjectResolverObject object(spec, nullptr, INVALID_TILE, view);
	const SpriteGroup *group = object.Resolve();
	if (group == nullptr || group->type != SGT_TILELAYOUT) return;

	const DrawTileSprites *dts = ((const TileLayoutSpriteGroup *)group)->ProcessRegisters(nullptr);

	PaletteID palette;
	if (Company::IsValidID(_local_company)) {
		/* Get the colours of our company! */
		if (spec->flags.Test(ObjectFlag::Uses2CC)) {
			const Livery *l = Company::Get(_local_company)->livery;
			palette = SPR_2CCMAP_BASE + l->colour1 + l->colour2 * 16;
		} else {
			palette = COMPANY_SPRITE_COLOUR(_local_company);
		}
	} else {
		/* There's no company, so just take the base palette. */
		palette = spec->flags.Test(ObjectFlag::Uses2CC) ? SPR_2CCMAP_BASE : PALETTE_RECOLOUR_START;
	}

	SpriteID image = dts->ground.sprite;
	PaletteID pal  = dts->ground.pal;

	if (GB(image, 0, SPRITE_WIDTH) != 0) {
		DrawSprite(image, GroundSpritePaletteTransform(image, pal, palette), x, y);
	}

	DrawNewGRFTileSeqInGUI(x, y, dts, 0, palette);
}

/**
 * Perform a callback for an object.
 * @param callback The callback to perform.
 * @param param1   The first parameter to pass to the NewGRF.
 * @param param2   The second parameter to pass to the NewGRF.
 * @param spec     The specification of the object / the entry point.
 * @param o        The object to call the callback for.
 * @param tile     The tile the callback is called for.
 * @return The result of the callback.
 */
uint16_t StubGetObjectCallback(CallbackID callback, uint32_t param1, uint32_t param2, const ObjectSpec *spec, Object *o, TileIndex tile, int extra_data)
{
	return GetObjectCallback(callback, param1, param2, spec, o, tile);
}

/** Helper class for animation control. */
struct ObjectAnimationBase : public AnimationBase<ObjectAnimationBase, ObjectSpec, Object, int, StubGetObjectCallback, TileAnimationFrameAnimationHelper<Object> > {
	static const CallbackID cb_animation_speed      = CBID_OBJECT_ANIMATION_SPEED;
	static const CallbackID cb_animation_next_frame = CBID_OBJECT_ANIMATION_NEXT_FRAME;

	static const ObjectCallbackMask cbm_animation_speed      = ObjectCallbackMask::AnimationSpeed;
	static const ObjectCallbackMask cbm_animation_next_frame = ObjectCallbackMask::AnimationNextFrame;
};

/**
 * Handle the animation of the object tile.
 * @param tile The tile to animate.
 */
void AnimateNewObjectTile(TileIndex tile)
{
	const ObjectSpec *spec = ObjectSpec::GetByTile(tile);
	if (spec == nullptr || !spec->flags.Test(ObjectFlag::Animation)) return;

	ObjectAnimationBase::AnimateTile(spec, Object::GetByTile(tile), tile, spec->flags.Test(ObjectFlag::AnimRandomBits));
}

uint8_t GetNewObjectTileAnimationSpeed(TileIndex tile)
{
	const ObjectSpec *spec = ObjectSpec::GetByTile(tile);
	if (spec == nullptr || !spec->flags.Test(ObjectFlag::Animation)) return 0;

	return ObjectAnimationBase::GetAnimationSpeed(spec);
}

/**
 * Trigger the update of animation on a single tile.
 * @param o       The object that got triggered.
 * @param tile    The location of the triggered tile.
 * @param trigger The trigger that is triggered.
 * @param spec    The spec associated with the object.
 */
void TriggerObjectTileAnimation(Object *o, TileIndex tile, ObjectAnimationTrigger trigger, const ObjectSpec *spec)
{
	if (!HasBit(spec->animation.triggers, trigger)) return;

	ObjectAnimationBase::ChangeAnimationFrame(CBID_OBJECT_ANIMATION_START_STOP, spec, o, tile, Random(), trigger);
}

/**
 * Trigger the update of animation on a whole object.
 * @param o       The object that got triggered.
 * @param trigger The trigger that is triggered.
 * @param spec    The spec associated with the object.
 */
void TriggerObjectAnimation(Object *o, ObjectAnimationTrigger trigger, const ObjectSpec *spec)
{
	if (!HasBit(spec->animation.triggers, trigger)) return;

	for (TileIndex tile : o->location) {
		TriggerObjectTileAnimation(o, tile, trigger, spec);
	}
}

void DumpObjectSpriteGroup(const ObjectSpec *spec, SpriteGroupDumper &dumper)
{
	const SpriteGroup *def = spec->grf_prop.GetSpriteGroup(OBJECT_SPRITE_GROUP_DEFAULT);
	dumper.DumpSpriteGroup(def, 0);

	const SpriteGroup *purchase = spec->grf_prop.GetSpriteGroup(OBJECT_SPRITE_GROUP_PURCHASE);
	if (purchase != nullptr && purchase != def) {
		dumper.Print("");
		dumper.Print("PURCHASE:");
		dumper.DumpSpriteGroup(purchase, 0);
	}
}
