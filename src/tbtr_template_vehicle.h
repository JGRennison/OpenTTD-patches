/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_vehicle.h Template-based train replacement: template vehicle header. */

#ifndef TEMPLATE_VEH_H
#define TEMPLATE_VEH_H

#include "company_func.h"

#include "vehicle_type.h"
#include "vehicle_base.h"
#include "vehicle_func.h"

#include "articulated_vehicles.h"
#include "newgrf_callbacks.h"
#include "newgrf_engine.h"
#include "newgrf_spritegroup.h"

#include "engine_base.h"
#include "engine_type.h"
#include "engine_func.h"

#include "sortlist_type.h"

#include "saveload/saveload_common.h"

#include "zoom_func.h"

struct TemplateVehicle;
struct TemplateReplacement;

typedef uint16 TemplateID;
static const TemplateID INVALID_TEMPLATE = 0xFFFF;

static const uint16 CONSIST_HEAD = 0x0;
static const uint16 CONSIST_TAIL = 0xffff;

/** A pool allowing to store up to ~64k templates */
typedef Pool<TemplateVehicle, TemplateID, 512, 64000> TemplatePool;
extern TemplatePool _template_pool;

extern bool _template_vehicle_images_valid;

/// listing/sorting templates
typedef GUIList<const TemplateVehicle *> GUITemplateList;

struct TemplateVehicleImageDimensions {
	int reference_width;
	int vehicle_pitch;
	int cached_veh_length;
	int vehicle_flip_length;

	void SetFromTrain(const Train *t);

	int GetDisplayImageWidth() const
	{
		return ScaleSpriteTrad(this->cached_veh_length * this->reference_width / VEHICLE_LENGTH);
	}

	int GetOffsetX() const
	{
		if (this->vehicle_flip_length >= 0) {
			return ScaleSpriteTrad((this->vehicle_flip_length - VEHICLE_LENGTH / 2) * this->reference_width / VEHICLE_LENGTH);
		}
		return ScaleSpriteTrad(this->reference_width) / 2;
	}

	int GetOffsetY() const
	{
		return ScaleSpriteTrad(this->vehicle_pitch);
	}
};

/** Template vehicle control flags. */
enum TemplateVehicleControlFlags {
	TVCF_REVERSED                     = 0,      ///< Vehicle is reversed (VRF_REVERSE_DIRECTION)
};

struct TemplateVehicle : TemplatePool::PoolItem<&_template_pool>, BaseVehicle {
private:
	TemplateVehicle *next;                      ///< pointer to the next vehicle in the chain
	TemplateVehicle *previous;                  ///< NOSAVE: pointer to the previous vehicle in the chain
	TemplateVehicle *first;                     ///< NOSAVE: pointer to the first vehicle in the chain

public:
	friend const SaveLoadTable GTD();
	friend void AfterLoadTemplateVehicles();

	// Template usage configuration
	bool reuse_depot_vehicles;
	bool keep_remaining_vehicles;
	bool refit_as_template;
	bool replace_old_only;

	// Things derived from a virtual train
	Owner owner;

	EngineID engine_type;               ///< The type of engine used for this vehicle.
	CargoID cargo_type;                 ///< type of cargo this vehicle is carrying
	uint16 cargo_cap;                   ///< total capacity
	byte cargo_subtype;

	byte subtype;
	RailType railtype;

	VehicleID index;

	uint16 real_consist_length;

	uint16 max_speed;
	uint32 power;
	uint32 empty_weight;
	uint32 full_weight;
	uint32 max_te;
	uint32 air_drag;

	uint32 ctrl_flags;                  ///< See: TemplateVehicleControlFlags
	std::string name;

	VehicleSpriteSeq sprite_seq;                     ///< NOSAVE: Vehicle appearance.
	TemplateVehicleImageDimensions image_dimensions; ///< NOSAVE: image dimensions
	SpriteID colourmap;                              ///< NOSAVE: cached colour mapping

	TemplateVehicle(VehicleType type = VEH_INVALID, EngineID e = INVALID_ENGINE, Owner = _local_company);

	TemplateVehicle(EngineID eid)
	{
		next = nullptr;
		previous = nullptr;
		first = this;
		engine_type = eid;
		this->reuse_depot_vehicles = false;
		this->keep_remaining_vehicles = false;
		this->refit_as_template = true;
		this->replace_old_only = false;
		this->sprite_seq.count = 1;
	}

	~TemplateVehicle();

	inline TemplateVehicle *Next() const { return this->next; }
	inline TemplateVehicle *Prev() const { return this->previous; }
	inline TemplateVehicle *First() const { return this->first; }

	void SetNext(TemplateVehicle *v);
	void SetPrev(TemplateVehicle *v);
	void SetFirst(TemplateVehicle *v);

	TemplateVehicle *GetNextUnit() const;
	TemplateVehicle *GetPrevUnit();

	bool IsSetReuseDepotVehicles() const { return this->reuse_depot_vehicles; }
	bool IsSetKeepRemainingVehicles() const { return this->keep_remaining_vehicles; }
	bool IsSetRefitAsTemplate() const { return this->refit_as_template; }
	bool IsReplaceOldOnly() const { return this->replace_old_only; }
	void ToggleReuseDepotVehicles() { this->reuse_depot_vehicles = !this->reuse_depot_vehicles; }
	void ToggleKeepRemainingVehicles() { this->keep_remaining_vehicles = !this->keep_remaining_vehicles; }
	void ToggleRefitAsTemplate() { this->refit_as_template = !this->refit_as_template; }
	void ToggleReplaceOldOnly() { this->replace_old_only = !this->replace_old_only; }

	bool IsPrimaryVehicle() const { return this->IsFrontEngine(); }
	inline bool IsFrontEngine() const { return HasBit(this->subtype, GVSF_FRONT); }
	inline bool HasArticulatedPart() const { return this->Next() != nullptr && this->Next()->IsArticulatedPart(); }

	inline bool IsEngine() const { return HasBit(this->subtype, GVSF_ENGINE); }
	inline bool IsWagon() const { return HasBit(this->subtype, GVSF_WAGON); }

	inline bool IsArticulatedPart() const { return HasBit(this->subtype, GVSF_ARTICULATED_PART); }
	inline bool IsMultiheaded() const { return HasBit(this->subtype, GVSF_MULTIHEADED); }
	inline bool IsRearDualheaded() const { return this->IsMultiheaded() && !this->IsEngine(); }

	inline bool IsFreeWagonChain() const { return HasBit(this->subtype, GVSF_FREE_WAGON); }

	inline void SetFrontEngine()     { SetBit(this->subtype, GVSF_FRONT); }
	inline void SetEngine()          { SetBit(this->subtype, GVSF_ENGINE); }
	inline void SetArticulatedPart() { SetBit(this->subtype, GVSF_ARTICULATED_PART); }
	inline void SetMultiheaded()     { SetBit(this->subtype, GVSF_MULTIHEADED); }

	inline void SetWagon() { SetBit(this->subtype, GVSF_WAGON); }
	inline void SetFreeWagon() { SetBit(this->subtype, GVSF_FREE_WAGON); }

	inline uint16 GetRealLength() const { return this->real_consist_length; }
	inline void SetRealLength(uint16 len) { this->real_consist_length = len; }

	int Length() const;

	SpriteID GetImage(Direction) const;
	SpriteID GetSpriteID() const;

	uint NumGroupsUsingTemplate() const;

};

// TemplateReplacement stuff

typedef Pool<TemplateReplacement, uint16, 16, 1024> TemplateReplacementPool;
extern TemplateReplacementPool _template_replacement_pool;

struct TemplateReplacement : TemplateReplacementPool::PoolItem<&_template_replacement_pool> {
	GroupID group;
	TemplateID sel_template;

	TemplateReplacement(GroupID gid, TemplateID tid) { this->group = gid; this->sel_template = tid; }
	TemplateReplacement() {}
	~TemplateReplacement();

	inline GroupID Group() { return this->group; }
	inline GroupID Template() { return this->sel_template; }

	inline void SetGroup(GroupID gid) { this->group = gid; }
	inline void SetTemplate(TemplateID tid) { this->sel_template = tid; }

	inline TemplateID GetTemplateVehicleID() { return sel_template; }

	static void PreCleanPool();
};

TemplateReplacement *GetTemplateReplacementByGroupID(GroupID gid);
TemplateID GetTemplateIDByGroupID(GroupID gid);
TemplateID GetTemplateIDByGroupIDRecursive(GroupID gid);
bool IssueTemplateReplacement(GroupID gid, TemplateID tid);
bool ShouldServiceTrainForTemplateReplacement(const Train *t, const TemplateVehicle *tv);
void MarkTrainsUsingTemplateAsPendingTemplateReplacement(const TemplateVehicle *tv);

uint DeleteTemplateReplacementsByGroupID(const Group *g);

void ReindexTemplateReplacements();
void ReindexTemplateReplacementsRecursive();

/**
 * Guard to inhibit re-indexing of the recursive group to template replacement cache,
 * and to disable group-based VF_REPLACEMENT_PENDING changes.
 * May be used recursively.
 */
struct ReindexTemplateReplacementsRecursiveGuard {
	ReindexTemplateReplacementsRecursiveGuard();
	~ReindexTemplateReplacementsRecursiveGuard();

	ReindexTemplateReplacementsRecursiveGuard(const ReindexTemplateReplacementsRecursiveGuard &copysrc) = delete;
	ReindexTemplateReplacementsRecursiveGuard(ReindexTemplateReplacementsRecursiveGuard &&movesrc) = delete;
	ReindexTemplateReplacementsRecursiveGuard &operator=(const ReindexTemplateReplacementsRecursiveGuard &) = delete;
	ReindexTemplateReplacementsRecursiveGuard &operator=(ReindexTemplateReplacementsRecursiveGuard &&) = delete;
};

int GetTemplateVehicleEstimatedMaxAchievableSpeed(const TemplateVehicle *tv, int mass, const int speed_cap);

#endif /* TEMPLATE_VEH_H */
