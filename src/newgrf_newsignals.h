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

extern std::vector<const GRFFile *> _new_signals_grfs;

/** Resolver for the new signals scope. */
struct NewSignalsScopeResolver : public ScopeResolver {
	TileIndex tile;      ///< Tracktile. For track on a bridge this is the southern bridgehead.
	TileContext context; ///< Are we resolving sprites for the upper halftile, or on a bridge?

	/**
	 * Constructor of the railtype scope resolvers.
	 * @param ro Surrounding resolver.
	 * @param tile %Tile containing the track. For track on a bridge this is the southern bridgehead.
	 * @param context Are we resolving sprites for the upper halftile, or on a bridge?
	 */
	NewSignalsScopeResolver(ResolverObject &ro, TileIndex tile, TileContext context)
		: ScopeResolver(ro), tile(tile), context(context)
	{
	}

	uint32 GetRandomBits() const override;
	uint32 GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const override;
};

/** Resolver object for rail types. */
struct NewSignalsResolverObject : public ResolverObject {
	NewSignalsScopeResolver newsignals_scope; ///< Resolver for the new signals scope.

	NewSignalsResolverObject(const GRFFile *grffile, TileIndex tile, TileContext context, uint32 param1 = 0, uint32 param2 = 0);

	ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, byte relative = 0) override
	{
		switch (scope) {
			case VSG_SCOPE_SELF: return &this->newsignals_scope;
			default:             return ResolverObject::GetScope(scope, relative);
		}
	}

	const SpriteGroup *ResolveReal(const RealSpriteGroup *group) const override;

	GrfSpecFeature GetFeature() const override;
};

#endif /* NEWGRF_RAILTYPE_H */
