/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_newsignals.h NewGRF handling of new signals. */

#ifndef NEWGRF_NEWSIGNALS_H
#define NEWGRF_NEWSIGNALS_H

#include "newgrf_commons.h"
#include "newgrf_spritegroup.h"
#include "tunnel_map.h"
#include "gfx_type.h"

#include <vector>
#include <array>

extern std::vector<const GRFFile *> _new_signals_grfs;

struct TraceRestrictProgram;
struct GRFFile;

enum {
	MAX_NEW_SIGNAL_STYLES = 15,
};

enum NewSignalStyleFlags {
	NSSF_NO_ASPECT_INC                  = 0,
	NSSF_ALWAYS_RESERVE_THROUGH         = 1,
	NSSF_LOOKAHEAD_ASPECTS_SET          = 2,
	NSSF_OPPOSITE_SIDE                  = 3,
	NSSF_LOOKAHEAD_SINGLE_SIGNAL        = 4,
	NSSF_COMBINED_NORMAL_SHUNT          = 5,
	NSSF_REALISTIC_BRAKING_ONLY         = 6,
};

struct NewSignalStyle {
	const GRFFile *grffile;
	StringID name;
	uint8_t grf_local_id;
	uint8_t style_flags;
	uint8_t lookahead_extra_aspects;
	uint8_t semaphore_mask;
	uint8_t electric_mask;

	PalSpriteID signals[SIGTYPE_END][2][2];
};
extern std::array<NewSignalStyle, MAX_NEW_SIGNAL_STYLES> _new_signal_styles;
struct NewSignalStyleMapping {
	uint32_t grfid = 0;
	uint8_t grf_local_id = 0;

	inline bool operator==(const NewSignalStyleMapping& o) const { return grfid == o.grfid && grf_local_id == o.grf_local_id; }
};
extern std::array<NewSignalStyleMapping, MAX_NEW_SIGNAL_STYLES> _new_signal_style_mapping;
extern uint8_t _num_new_signal_styles;
extern uint16_t _enabled_new_signal_styles_mask;

/** Resolver for the new signals scope. */
struct NewSignalsScopeResolver : public ScopeResolver {
	TileIndex tile;      ///< Tracktile. For track on a bridge this is the southern bridgehead.
	TileContext context; ///< Are we resolving sprites for the upper halftile, or on a bridge?
	CustomSignalSpriteContext signal_context;
	uint8_t signal_style;
	const TraceRestrictProgram *prog;
	uint z;

	/**
	 * Constructor of the railtype scope resolvers.
	 * @param ro Surrounding resolver.
	 * @param tile %Tile containing the track. For track on a bridge this is the southern bridgehead.
	 * @param context Are we resolving sprites for the upper halftile, or on a bridge?
	 * @param signal_context Signal context.
	 */
	NewSignalsScopeResolver(ResolverObject &ro, TileIndex tile, TileContext context, CustomSignalSpriteContext signal_context, uint8_t signal_style, const TraceRestrictProgram *prog, uint z)
		: ScopeResolver(ro), tile(tile), context(context), signal_context(signal_context), signal_style(signal_style), prog(prog), z(z)
	{
	}

	uint32_t GetRandomBits() const override;
	uint32_t GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra *extra) const override;
};

/** Resolver object for rail types. */
struct NewSignalsResolverObject : public ResolverObject {
	NewSignalsScopeResolver newsignals_scope; ///< Resolver for the new signals scope.

	NewSignalsResolverObject(const GRFFile *grffile, TileIndex tile, TileContext context, uint32_t param1, uint32_t param2,
			CustomSignalSpriteContext signal_context, uint8_t signal_style, const TraceRestrictProgram *prog = nullptr, uint z = 0);

	ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, VarSpriteGroupScopeOffset relative = 0) override
	{
		switch (scope) {
			case VSG_SCOPE_SELF: return &this->newsignals_scope;
			default:             return ResolverObject::GetScope(scope, relative);
		}
	}

	const SpriteGroup *ResolveReal(const RealSpriteGroup *group) const override;

	GrfSpecFeature GetFeature() const override;
};

uint GetNewSignalsRestrictedSignalsInfo(const TraceRestrictProgram *prog, TileIndex tile, uint8_t signal_style);
uint GetNewSignalsVerticalClearanceInfo(TileIndex tile, uint z);

inline uint GetNewSignalsSignalContext(CustomSignalSpriteContext signal_context, TileIndex tile)
{
	uint result = signal_context;
	if ((signal_context == CSSC_TUNNEL_BRIDGE_ENTRANCE || signal_context == CSSC_TUNNEL_BRIDGE_EXIT) && IsTunnel(tile)) result |= 0x100;
	return result;
}

uint32_t GetNewSignalsSideVariable();

#endif /* NEWGRF_NEWSIGNALS_H */
