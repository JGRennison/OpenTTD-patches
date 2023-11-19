/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_railtype.h NewGRF handling of rail types. */

#ifndef NEWGRF_RAILTYPE_H
#define NEWGRF_RAILTYPE_H

#include "rail.h"
#include "newgrf_commons.h"
#include "newgrf_spritegroup.h"

struct TraceRestrictProgram;

/** Resolver for the railtype scope. */
struct RailTypeScopeResolver : public ScopeResolver {
	TileIndex tile;      ///< Tracktile. For track on a bridge this is the southern bridgehead.
	TileContext context; ///< Are we resolving sprites for the upper halftile, or on a bridge?
	CustomSignalSpriteContext signal_context;
	const RailTypeInfo *rti;
	const TraceRestrictProgram *prog;
	uint z;

	/**
	 * Constructor of the railtype scope resolvers.
	 * @param ro Surrounding resolver.
	 * @param tile %Tile containing the track. For track on a bridge this is the southern bridgehead.
	 * @param context Are we resolving sprites for the upper halftile, or on a bridge?
	 * @param signal_context Signal context.
	 */
	RailTypeScopeResolver(ResolverObject &ro, const RailTypeInfo *rti, TileIndex tile, TileContext context, CustomSignalSpriteContext signal_context, const TraceRestrictProgram *prog, uint z)
		: ScopeResolver(ro), tile(tile), context(context), signal_context(signal_context), rti(rti), prog(prog), z(z)
	{
	}

	uint32 GetRandomBits() const override;
	uint32 GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const override;
};

/** Resolver object for rail types. */
struct RailTypeResolverObject : public ResolverObject {
	RailTypeScopeResolver railtype_scope; ///< Resolver for the railtype scope.

	RailTypeResolverObject(const RailTypeInfo *rti, TileIndex tile, TileContext context, RailTypeSpriteGroup rtsg, uint32 param1 = 0, uint32 param2 = 0,
			CustomSignalSpriteContext signal_context = CSSC_GUI, const TraceRestrictProgram *prog = nullptr, uint z = 0);

	ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, VarSpriteGroupScopeOffset relative = 0) override
	{
		switch (scope) {
			case VSG_SCOPE_SELF: return &this->railtype_scope;
			default:             return ResolverObject::GetScope(scope, relative);
		}
	}

	GrfSpecFeature GetFeature() const override;
	uint32 GetDebugID() const override;
};

struct CustomSignalSpriteResult {
	PalSpriteID sprite;
	bool restricted_valid;
};

SpriteID GetCustomRailSprite(const RailTypeInfo *rti, TileIndex tile, RailTypeSpriteGroup rtsg, TileContext context = TCX_NORMAL, uint *num_results = nullptr);
CustomSignalSpriteResult GetCustomSignalSprite(const RailTypeInfo *rti, TileIndex tile, SignalType type, SignalVariant var, uint8 aspect,
		CustomSignalSpriteContext context, uint8 style, const TraceRestrictProgram *prog = nullptr, uint z = 0);

RailType GetRailTypeTranslation(uint8 railtype, const GRFFile *grffile);
uint8 GetReverseRailTypeTranslation(RailType railtype, const GRFFile *grffile);

#endif /* NEWGRF_RAILTYPE_H */
