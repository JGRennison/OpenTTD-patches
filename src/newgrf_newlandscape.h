/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_newlandscape.h NewGRF handling of new landscape. */

#ifndef NEWGRF_NEWLANDSCAPE_H
#define NEWGRF_NEWLANDSCAPE_H

#include "newgrf_commons.h"
#include "newgrf_spritegroup.h"

extern std::vector<const GRFFile *> _new_landscape_rocks_grfs;

enum NewLandscapeType : uint8 {
	NEW_LANDSCAPE_ROCKS,
};

struct TileInfo;

/** Resolver for the new landscape scope. */
struct NewLandscapeScopeResolver : public ScopeResolver {
	const TileInfo *ti;
	NewLandscapeType landscape_type;

	NewLandscapeScopeResolver(ResolverObject &ro, const TileInfo *ti, NewLandscapeType landscape_type)
		: ScopeResolver(ro), ti(ti), landscape_type(landscape_type)
	{
	}

	uint32 GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const override;
};

struct NewLandscapeResolverObject : public ResolverObject {
	NewLandscapeScopeResolver newlandscape_scope;

	NewLandscapeResolverObject(const GRFFile *grffile, const TileInfo *ti, NewLandscapeType landscape_type, uint32 param1 = 0, uint32 param2 = 0);

	ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, VarSpriteGroupScopeOffset relative = 0) override
	{
		switch (scope) {
			case VSG_SCOPE_SELF: return &this->newlandscape_scope;
			default:             return ResolverObject::GetScope(scope, relative);
		}
	}

	const SpriteGroup *ResolveReal(const RealSpriteGroup *group) const override;

	GrfSpecFeature GetFeature() const override;
};

#endif /* NEWGRF_NEWLANDSCAPE_H */
