/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_spritegroup.h Action 2 handling. */

#ifndef NEWGRF_SPRITEGROUP_H
#define NEWGRF_SPRITEGROUP_H

#include "town_type.h"
#include "engine_type.h"
#include "house_type.h"
#include "industry_type.h"

#include "newgrf_callbacks.h"
#include "newgrf_generic.h"
#include "newgrf_storage.h"
#include "newgrf_commons.h"

#include "3rdparty/svector/svector.h"

#include <map>
#include <vector>

/**
 * Gets the value of a so-called newgrf "register".
 * @param i index of the register
 * @pre i < 0x110
 * @return the value of the register
 */
inline uint32_t GetRegister(uint i)
{
	extern TemporaryStorageArray<int32_t, 0x110> _temp_store;
	return _temp_store.GetValue(i);
}

/* List of different sprite group types */
enum SpriteGroupType : uint8_t {
	SGT_REAL,
	SGT_DETERMINISTIC,
	SGT_RANDOMIZED,
	SGT_CALLBACK,
	SGT_RESULT,
	SGT_TILELAYOUT,
	SGT_INDUSTRY_PRODUCTION,
};

struct SpriteGroup;
typedef uint32_t SpriteGroupID;
struct ResolverObject;

/* SPRITE_WIDTH is 24. ECS has roughly 30 sprite groups per real sprite.
 * Adding an 'extra' margin would be assuming 64 sprite groups per real
 * sprite. 64 = 2^6, so 2^30 should be enough (for now) */
using SpriteGroupPool = Pool<SpriteGroup, SpriteGroupID, 1024, 1U << 30, PoolType::Data>;
extern SpriteGroupPool _spritegroup_pool;

enum SpriteGroupFlags : uint8_t {
	SGF_NONE                     = 0,
	SGF_ACTION6                  = 1 << 0,
	SGF_INLINING                 = 1 << 1,
	SGF_SKIP_CB                  = 1 << 2,
};
DECLARE_ENUM_AS_BIT_SET(SpriteGroupFlags)

/* Common wrapper for all the different sprite group types */
struct SpriteGroup : SpriteGroupPool::PoolItem<&_spritegroup_pool> {
protected:
	SpriteGroup(SpriteGroupType type) : nfo_line(0), type(type) {}
	/** Base sprite group resolver */
	virtual const SpriteGroup *Resolve([[maybe_unused]] ResolverObject &object) const { return this; };

public:
	virtual ~SpriteGroup() = default;

	uint32_t nfo_line;
	SpriteGroupType type;
	GrfSpecFeature feature;
	SpriteGroupFlags sg_flags = SGF_NONE;

	virtual SpriteID GetResult() const { return 0; }
	virtual uint8_t GetNumResults() const { return 0; }
	virtual uint16_t GetCallbackResult() const { return CALLBACK_FAILED; }

	static const SpriteGroup *Resolve(const SpriteGroup *group, ResolverObject &object, bool top_level = true);
};


/* 'Real' sprite groups contain a list of other result or callback sprite
 * groups. */
struct RealSpriteGroup : SpriteGroup {
	RealSpriteGroup() : SpriteGroup(SGT_REAL) {}

	/* Loaded = in motion, loading = not moving
	 * Each group contains several spritesets, for various loading stages */

	/* XXX: For stations the meaning is different - loaded is for stations
	 * with small amount of cargo whilst loading is for stations with a lot
	 * of da stuff. */

	ankerl::svector<const SpriteGroup *, 2> loaded;  ///< List of loaded groups (can be SpriteIDs or Callback results)
	ankerl::svector<const SpriteGroup *, 2> loading; ///< List of loading groups (can be SpriteIDs or Callback results)

protected:
	const SpriteGroup *Resolve(ResolverObject &object) const override;
};

/* Shared by deterministic and random groups. */
enum VarSpriteGroupScope : uint8_t {
	VSG_BEGIN,

	VSG_SCOPE_SELF = VSG_BEGIN, ///< Resolved object itself
	VSG_SCOPE_PARENT,           ///< Related object of the resolved one
	VSG_SCOPE_RELATIVE,         ///< Relative position (vehicles only)

	VSG_END
};
DECLARE_INCREMENT_DECREMENT_OPERATORS(VarSpriteGroupScope)

enum VarSpriteGroupScopeRelativeMode : uint8_t {
	VSGSRM_BACKWARD_SELF         = 0,
	VSGSRM_FORWARD_SELF          = 1,
	VSGSRM_BACKWARD_ENGINE       = 2,
	VSGSRM_BACKWARD_SAMEID       = 3,
	VSGSRM_END,
};

/*
 * Decoded relative scope offset:
 * Bits 0..7: offset
 * Bits 8..9: mode (VarSpriteGroupScopeRelativeMode)
 * Bit    15: use var 0x100
 */
typedef uint16_t VarSpriteGroupScopeOffset;

GrfSpecFeature GetGrfSpecFeatureForParentScope(GrfSpecFeature feature);

inline GrfSpecFeature GetGrfSpecFeatureForScope(GrfSpecFeature feature, VarSpriteGroupScope scope)
{
	if (scope == VSG_SCOPE_PARENT) {
		return GetGrfSpecFeatureForParentScope(feature);
	}

	return feature;
}

enum DeterministicSpriteGroupSize : uint8_t {
	DSG_SIZE_BYTE,
	DSG_SIZE_WORD,
	DSG_SIZE_DWORD,
};

enum DeterministicSpriteGroupAdjustType : uint8_t {
	DSGA_TYPE_NONE,
	DSGA_TYPE_DIV,
	DSGA_TYPE_MOD,

	DSGA_TYPE_EQ,
	DSGA_TYPE_NEQ,
};

enum DeterministicSpriteGroupAdjustOperation : uint8_t {
	DSGA_OP_ADD,  ///< a + b
	DSGA_OP_SUB,  ///< a - b
	DSGA_OP_SMIN, ///< (signed) min(a, b)
	DSGA_OP_SMAX, ///< (signed) max(a, b)
	DSGA_OP_UMIN, ///< (unsigned) min(a, b)
	DSGA_OP_UMAX, ///< (unsigned) max(a, b)
	DSGA_OP_SDIV, ///< (signed) a / b
	DSGA_OP_SMOD, ///< (signed) a % b
	DSGA_OP_UDIV, ///< (unsigned) a / b
	DSGA_OP_UMOD, ///< (unsigned) a & b
	DSGA_OP_MUL,  ///< a * b
	DSGA_OP_AND,  ///< a & b
	DSGA_OP_OR,   ///< a | b
	DSGA_OP_XOR,  ///< a ^ b
	DSGA_OP_STO,  ///< store a into temporary storage, indexed by b. return a
	DSGA_OP_RST,  ///< return b
	DSGA_OP_STOP, ///< store a into persistent storage, indexed by b, return a
	DSGA_OP_ROR,  ///< rotate a b positions to the right
	DSGA_OP_SCMP, ///< (signed) comparison (a < b -> 0, a == b = 1, a > b = 2)
	DSGA_OP_UCMP, ///< (unsigned) comparison (a < b -> 0, a == b = 1, a > b = 2)
	DSGA_OP_SHL,  ///< a << b
	DSGA_OP_SHR,  ///< (unsigned) a >> b
	DSGA_OP_SAR,  ///< (signed) a >> b

	DSGA_OP_END,

	DSGA_OP_TERNARY = 0x80, ///< a == 0 ? b : c,
	DSGA_OP_EQ,             ///< a == b ? 1 : 0,
	DSGA_OP_SLT,            ///< (signed) a < b ? 1 : 0,
	DSGA_OP_SGE,            ///< (signed) a >= b ? 1 : 0,
	DSGA_OP_SLE,            ///< (signed) a <= b ? 1 : 0,
	DSGA_OP_SGT,            ///< (signed) a > b ? 1 : 0,
	DSGA_OP_RSUB,           ///< b - a
	DSGA_OP_STO_NC,         ///< store b into temporary storage, indexed by c. return a
	DSGA_OP_ABS,            ///< abs(a)
	DSGA_OP_JZ,             ///< jump forward fixed number of adjusts (to adjust after DSGAF_END_BLOCK marker (taking into account nesting)) if b is zero. return 0 if jumped, return a if not jumped
	DSGA_OP_JNZ,            ///< jump forward fixed number of adjusts (to adjust after DSGAF_END_BLOCK marker (taking into account nesting)) if b is non-zero. return b if jumped, return a if not jumped
	DSGA_OP_JZ_LV,          ///< jump forward fixed number of adjusts (to adjust after DSGAF_END_BLOCK marker (taking into account nesting)) if a is zero. return a
	DSGA_OP_JNZ_LV,         ///< jump forward fixed number of adjusts (to adjust after DSGAF_END_BLOCK marker (taking into account nesting)) if a is non-zero. return a
	DSGA_OP_NOOP,           ///< a

	DSGA_OP_SPECIAL_END,
};

static_assert((DSGA_OP_SLT ^ 1) == DSGA_OP_SGE);
static_assert((DSGA_OP_SLE ^ 1) == DSGA_OP_SGT);

enum DeterministicSpriteGroupAdjustFlags : uint8_t {
	DSGAF_NONE               = 0,
	DSGAF_SKIP_ON_ZERO       = 1 << 0,
	DSGAF_SKIP_ON_LSB_SET    = 1 << 1,
	DSGAF_LAST_VAR_READ      = 1 << 2,
	DSGAF_JUMP_INS_HINT      = 1 << 3,
	DSGAF_END_BLOCK          = 1 << 4,
};
DECLARE_ENUM_AS_BIT_SET(DeterministicSpriteGroupAdjustFlags);

inline bool IsEvalAdjustWithZeroRemovable(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_ADD:
		case DSGA_OP_SUB:
		case DSGA_OP_OR:
		case DSGA_OP_XOR:
		case DSGA_OP_ROR:
		case DSGA_OP_SHL:
		case DSGA_OP_SHR:
		case DSGA_OP_SAR:
		case DSGA_OP_UMAX:
			return true;

		default:
			return false;
	}
}

inline bool IsEvalAdjustWithZeroAlwaysZero(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_UMIN:
		case DSGA_OP_MUL:
		case DSGA_OP_AND:
		case DSGA_OP_RST:
			return true;

		default:
			return false;
	}
}

inline bool IsEvalAdjustWithOneRemovable(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_MUL:
		case DSGA_OP_SDIV:
		case DSGA_OP_UDIV:
			return true;

		default:
			return false;
	}
}

inline bool IsEvalAdjustWithSideEffects(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_STO:
		case DSGA_OP_STOP:
			return true;

		default:
			return false;
	}
}

inline bool IsEvalAdjustUsableForConstantPropagation(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_ADD:
		case DSGA_OP_SUB:
		case DSGA_OP_SMIN:
		case DSGA_OP_SMAX:
		case DSGA_OP_UMIN:
		case DSGA_OP_UMAX:
		case DSGA_OP_SDIV:
		case DSGA_OP_SMOD:
		case DSGA_OP_UDIV:
		case DSGA_OP_UMOD:
		case DSGA_OP_MUL:
		case DSGA_OP_AND:
		case DSGA_OP_OR:
		case DSGA_OP_XOR:
		case DSGA_OP_ROR:
		case DSGA_OP_SCMP:
		case DSGA_OP_UCMP:
		case DSGA_OP_SHL:
		case DSGA_OP_SHR:
		case DSGA_OP_SAR:
			return true;

		default:
			return false;
	}
}

inline bool IsEvalAdjustOperationCommutative(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_ADD:
		case DSGA_OP_MUL:
		case DSGA_OP_AND:
		case DSGA_OP_OR:
		case DSGA_OP_XOR:
			return true;

		default:
			return false;
	}
}

inline bool IsEvalAdjustOperationAntiCommutative(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_SUB:
		case DSGA_OP_RSUB:
			return true;

		default:
			return false;
	}
}

inline bool IsEvalAdjustOperationReversable(DeterministicSpriteGroupAdjustOperation op)
{
	return IsEvalAdjustOperationCommutative(op) || IsEvalAdjustOperationAntiCommutative(op);
}

inline DeterministicSpriteGroupAdjustOperation ReverseEvalAdjustOperation(DeterministicSpriteGroupAdjustOperation op)
{
	if (IsEvalAdjustOperationCommutative(op)) return op;

	switch (op) {
		case DSGA_OP_SUB:
			return DSGA_OP_RSUB;
		case DSGA_OP_RSUB:
			return DSGA_OP_SUB;

		default:
			NOT_REACHED();
	}
}

inline bool IsEvalAdjustOperationRelationalComparison(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_SLT:
		case DSGA_OP_SGE:
		case DSGA_OP_SLE:
		case DSGA_OP_SGT:
			return true;

		default:
			return false;
	}
}

inline DeterministicSpriteGroupAdjustOperation InvertEvalAdjustRelationalComparisonOperation(DeterministicSpriteGroupAdjustOperation op)
{
	assert(IsEvalAdjustOperationRelationalComparison(op));
	return (DeterministicSpriteGroupAdjustOperation)(op ^ 1);
}

inline bool IsEvalAdjustOperationOnConstantEffectiveLoad(DeterministicSpriteGroupAdjustOperation op, uint32_t constant)
{
	switch (op) {
		case DSGA_OP_ADD:
		case DSGA_OP_OR:
		case DSGA_OP_XOR:
			return constant == 0;

		case DSGA_OP_MUL:
			return constant == 1;

		default:
			return false;
	}
}

inline bool IsEvalAdjustWithZeroLastValueAlwaysZero(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_SDIV:
		case DSGA_OP_SMOD:
		case DSGA_OP_UDIV:
		case DSGA_OP_UMOD:
		case DSGA_OP_UMIN:
		case DSGA_OP_MUL:
		case DSGA_OP_AND:
		case DSGA_OP_ROR:
		case DSGA_OP_SHL:
		case DSGA_OP_SHR:
		case DSGA_OP_SAR:
			return true;

		default:
			return false;
	}
}

inline bool IsEvalAdjustJumpOperation(DeterministicSpriteGroupAdjustOperation op)
{
	switch (op) {
		case DSGA_OP_JZ:
		case DSGA_OP_JNZ:
		case DSGA_OP_JZ_LV:
		case DSGA_OP_JNZ_LV:
			return true;

		default:
			return false;
	}
}

inline bool IsConstantComparisonAdjustType(DeterministicSpriteGroupAdjustType adjust_type)
{
	switch (adjust_type) {
		case DSGA_TYPE_EQ:
		case DSGA_TYPE_NEQ:
			return true;

		default:
			return false;
	}
}

inline DeterministicSpriteGroupAdjustType InvertConstantComparisonAdjustType(DeterministicSpriteGroupAdjustType adjust_type)
{
	assert(IsConstantComparisonAdjustType(adjust_type));
	return (adjust_type == DSGA_TYPE_EQ) ? DSGA_TYPE_NEQ : DSGA_TYPE_EQ;
}

struct DeterministicSpriteGroupAdjust {
	DeterministicSpriteGroupAdjustOperation operation;
	DeterministicSpriteGroupAdjustType type;
	uint16_t variable;
	uint8_t shift_num;
	DeterministicSpriteGroupAdjustFlags adjust_flags = DSGAF_NONE;
	uint32_t parameter;  ///< Used for variables between 0x60 and 0x7F inclusive.
	uint32_t and_mask;
	uint32_t add_val;    ///< Also used for DSGA_TYPE_EQ/DSGA_TYPE_NEQ constants and DSGA_OP_TERNARY false value
	uint32_t divmod_val; ///< Also used for DSGA_OP_STO_NC
	union {
		const SpriteGroup *subroutine;
		uint32_t jump;
	};
};

struct DeterministicSpriteGroupRange {
	const SpriteGroup *group;
	uint32_t low;
	uint32_t high;
};

enum DeterministicSpriteGroupFlags : uint16_t {
	DSGF_NONE                    = 0,
	DSGF_NO_DSE                  = 1 << 0,
	DSGF_CB_RESULT               = 1 << 1,
	DSGF_VAR_TRACKING_PENDING    = 1 << 2,
	DSGF_REQUIRES_VAR1C          = 1 << 3,
	DSGF_CHECK_EXPENSIVE_VARS    = 1 << 4,
	DSGF_CHECK_INSERT_JUMP       = 1 << 5,
	DSGF_CB_HANDLER              = 1 << 6,
	DSGF_INLINE_CANDIDATE        = 1 << 7,
	DSGF_CALCULATED_RESULT       = 1 << 8,
};
DECLARE_ENUM_AS_BIT_SET(DeterministicSpriteGroupFlags)

struct DeterministicSpriteGroup : SpriteGroup {
	DeterministicSpriteGroup() : SpriteGroup(SGT_DETERMINISTIC) {}

	VarSpriteGroupScope var_scope;
	VarSpriteGroupScopeOffset var_scope_count;
	DeterministicSpriteGroupSize size;
	DeterministicSpriteGroupFlags dsg_flags = DSGF_NONE;
	std::vector<DeterministicSpriteGroupAdjust> adjusts;
	std::vector<DeterministicSpriteGroupRange> ranges; // Dynamically allocated

	/* Dynamically allocated, this is the sole owner */
	const SpriteGroup *default_group;

	const SpriteGroup *error_group; // was first range, before sorting ranges

	bool GroupMayBeBypassed() const;

	bool IsCalculatedResult() const { return this->dsg_flags & DSGF_CALCULATED_RESULT; }

protected:
	const SpriteGroup *Resolve(ResolverObject &object) const override;
};

enum RandomizedSpriteGroupCompareMode : uint8_t {
	RSG_CMP_ANY,
	RSG_CMP_ALL,
};

struct RandomizedSpriteGroup : SpriteGroup {
	RandomizedSpriteGroup() : SpriteGroup(SGT_RANDOMIZED) {}

	VarSpriteGroupScope var_scope;  ///< Take this object:
	VarSpriteGroupScopeOffset var_scope_count;

	RandomizedSpriteGroupCompareMode cmp_mode; ///< Check for these triggers:
	uint8_t triggers;

	uint8_t lowest_randbit; ///< Look for this in the per-object randomized bitmask:

	std::vector<const SpriteGroup *> groups; ///< Take the group with appropriate index:

protected:
	const SpriteGroup *Resolve(ResolverObject &object) const override;
};

extern bool _grfs_loaded_with_sg_shadow_enable;

/* This contains a callback result. A failed callback has a value of
 * CALLBACK_FAILED */
struct CallbackResultSpriteGroup : SpriteGroup {
	/**
	 * Creates a spritegroup representing a callback result
	 * @param result The result as returned from TransformResultValue
	 */
	CallbackResultSpriteGroup(uint16_t result) :
		SpriteGroup(SGT_CALLBACK),
		result(result) {}

	/**
	 * Transforms a callback result value
	 * @param value The value that was used to represent this callback result
	 * @param grf_version8 True, if we are dealing with a new NewGRF which uses GRF version >= 8.
	 */
	static uint16_t TransformResultValue(uint16_t value, bool grf_version8)
	{
		/* Old style callback results (only valid for version < 8) have the highest byte 0xFF so signify it is a callback result.
		 * New style ones only have the highest bit set (allows 15-bit results, instead of just 8) */
		if (!grf_version8 && (value >> 8) == 0xFF) {
			return value & ~0xFF00;
		} else {
			return value & ~0x8000;
		}
	}

	uint16_t result;
	uint16_t GetCallbackResult() const override { return this->result; }
};


/* A result sprite group returns the first SpriteID and the number of
 * sprites in the set */
struct ResultSpriteGroup : SpriteGroup {
	/**
	 * Creates a spritegroup representing a sprite number result.
	 * @param sprite The sprite number.
	 * @param num_sprites The number of sprites per set.
	 * @return A spritegroup representing the sprite number result.
	 */
	ResultSpriteGroup(SpriteID sprite, uint8_t num_sprites) :
		SpriteGroup(SGT_RESULT),
		num_sprites(num_sprites),
		sprite(sprite)
	{
	}

	uint8_t num_sprites;
	SpriteID sprite;

	SpriteID GetResult() const override { return this->sprite; }
	uint8_t GetNumResults() const override { return this->num_sprites; }
};

/**
 * Action 2 sprite layout for houses, industry tiles, objects and airport tiles.
 */
struct TileLayoutSpriteGroup : SpriteGroup {
	TileLayoutSpriteGroup() : SpriteGroup(SGT_TILELAYOUT) {}
	~TileLayoutSpriteGroup() {}

	NewGRFSpriteLayout dts;

	const DrawTileSprites *ProcessRegisters(uint8_t *stage) const;
};

struct IndustryProductionSpriteGroup : SpriteGroup {
	IndustryProductionSpriteGroup() : SpriteGroup(SGT_INDUSTRY_PRODUCTION) {}

	uint8_t version;                              ///< Production callback version used, or 0xFF if marked invalid
	uint8_t num_input;                            ///< How many subtract_input values are valid
	int16_t subtract_input[INDUSTRY_NUM_INPUTS];  ///< Take this much of the input cargo (can be negative, is indirect in cb version 1+)
	CargoType cargo_input[INDUSTRY_NUM_INPUTS];   ///< Which input cargoes to take from (only cb version 2)
	uint8_t num_output;                           ///< How many add_output values are valid
	uint16_t add_output[INDUSTRY_NUM_OUTPUTS];    ///< Add this much output cargo when successful (unsigned, is indirect in cb version 1+)
	CargoType cargo_output[INDUSTRY_NUM_OUTPUTS]; ///< Which output cargoes to add to (only cb version 2)
	uint8_t again;

};

struct GetVariableExtra {
	bool available;
	uint32_t mask;

	GetVariableExtra(uint32_t mask_ = 0xFFFFFFFF)
			: available(true), mask(mask_) {}
};

/**
 * Interface to query and set values specific to a single #VarSpriteGroupScope (action 2 scope).
 *
 * Multiple of these interfaces are combined into a #ResolverObject to allow access
 * to different game entities from a #SpriteGroup-chain (action 1-2-3 chain).
 */
struct ScopeResolver {
	ResolverObject &ro; ///< Surrounding resolver object.

	ScopeResolver(ResolverObject &ro) : ro(ro) {}
	virtual ~ScopeResolver() = default;

	virtual uint32_t GetRandomBits() const;
	virtual uint32_t GetTriggers() const;

	virtual uint32_t GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra &extra) const;
	virtual void StorePSA(uint reg, int32_t value);
};

/**
 * Interface for #SpriteGroup-s to access the gamestate.
 *
 * Using this interface #SpriteGroup-chains (action 1-2-3 chains) can be resolved,
 * to get the results of callbacks, rerandomisations or normal sprite lookups.
 */
struct ResolverObject {
	/**
	 * Resolver constructor.
	 * @param grffile NewGRF file associated with the object (or \c nullptr if none).
	 * @param callback Callback code being resolved (default value is #CBID_NO_CALLBACK).
	 * @param callback_param1 First parameter (var 10) of the callback (only used when \a callback is also set).
	 * @param callback_param2 Second parameter (var 18) of the callback (only used when \a callback is also set).
	 */
	ResolverObject(const GRFFile *grffile, CallbackID callback = CBID_NO_CALLBACK, uint32_t callback_param1 = 0, uint32_t callback_param2 = 0)
		: default_scope(*this), callback(callback), callback_param1(callback_param1), callback_param2(callback_param2), grffile(grffile), root_spritegroup(nullptr)
	{
		this->ResetState();
	}

	virtual ~ResolverObject() = default;

	ScopeResolver default_scope;  ///< Default implementation of the grf scope.

	CallbackID callback;          ///< Callback being resolved.
	uint32_t callback_param1;     ///< First parameter (var 10) of the callback.
	uint32_t callback_param2;     ///< Second parameter (var 18) of the callback.

	uint32_t last_value;          ///< Result of most recent DeterministicSpriteGroup (including procedure calls)

	uint32_t waiting_triggers;    ///< Waiting triggers to be used by any rerandomisation. (scope independent)
	uint32_t used_triggers;       ///< Subset of cur_triggers, which actually triggered some rerandomisation. (scope independent)
	uint32_t reseed[VSG_END];     ///< Collects bits to rerandomise while triggering triggers.

	const GRFFile *grffile;       ///< GRFFile the resolved SpriteGroup belongs to
	const SpriteGroup *root_spritegroup; ///< Root SpriteGroup to use for resolving

	/**
	 * Resolve SpriteGroup.
	 * @return Result spritegroup.
	 */
	const SpriteGroup *Resolve()
	{
		return SpriteGroup::Resolve(this->root_spritegroup, *this);
	}

	/**
	 * Resolve callback.
	 * @return Callback result.
	 */
	uint16_t ResolveCallback()
	{
		const SpriteGroup *result = Resolve();
		return result != nullptr ? result->GetCallbackResult() : CALLBACK_FAILED;
	}

	virtual const SpriteGroup *ResolveReal(const RealSpriteGroup *group) const;

	virtual ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, VarSpriteGroupScopeOffset relative = 0);

	/**
	 * Returns the waiting triggers that did not trigger any rerandomisation.
	 */
	uint32_t GetRemainingTriggers() const
	{
		return this->waiting_triggers & ~this->used_triggers;
	}

	/**
	 * Returns the OR-sum of all bits that need reseeding
	 * independent of the scope they were accessed with.
	 * @return OR-sum of the bits.
	 */
	uint32_t GetReseedSum() const
	{
		uint32_t sum = 0;
		for (VarSpriteGroupScope vsg = VSG_BEGIN; vsg < VSG_END; vsg++) {
			sum |= this->reseed[vsg];
		}
		return sum;
	}

	/**
	 * Resets the dynamic state of the resolver object.
	 * To be called before resolving an Action-1-2-3 chain.
	 */
	void ResetState()
	{
		this->last_value = 0;
		this->waiting_triggers = 0;
		this->used_triggers = 0;
		memset(this->reseed, 0, sizeof(this->reseed));
	}

	/**
	 * Get the feature number being resolved for.
	 * This function is mainly intended for the callback profiling feature.
	 */
	virtual GrfSpecFeature GetFeature() const { return GSF_INVALID; }
	/**
	 * Get an identifier for the item being resolved.
	 * This function is mainly intended for the callback profiling feature,
	 * and should return an identifier recognisable by the NewGRF developer.
	 */
	virtual uint32_t GetDebugID() const { return 0; }
};

uint32_t EvaluateDeterministicSpriteGroupAdjust(DeterministicSpriteGroupSize size, const DeterministicSpriteGroupAdjust &adjust, ScopeResolver *scope, uint32_t last_value, uint32_t value);

#endif /* NEWGRF_SPRITEGROUP_H */
