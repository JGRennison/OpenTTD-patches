/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_town.h Functions to handle the town part of NewGRF towns. */

#ifndef NEWGRF_TOWN_H
#define NEWGRF_TOWN_H

#include "town_type.h"
#include "newgrf_spritegroup.h"

/**
 * Scope resolver for a town.
 * @note Currently there is no direct town resolver; we only need to get town
 *       variable results from inside stations, house tiles and industries,
 *       and to check the town's persistent storage.
 */
struct TownScopeResolver : public ScopeResolver {
	Town *t;       ///< %Town of the scope.
	bool readonly; ///< When set, persistent storage of the town is read-only,

	/**
	 * Resolver of a town scope.
	 * @param ro Surrounding resolver.
	 * @param t %Town of the scope.
	 * @param readonly Scope may change persistent storage of the town.
	 */
	TownScopeResolver(ResolverObject &ro, Town *t, bool readonly)
		: ScopeResolver(ro), t(t), readonly(readonly)
	{
	}

	virtual uint32 GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const override;
	virtual void StorePSA(uint reg, int32 value) override;
};

/**
 * Fake scope resolver for nonexistent towns.
 *
 * The purpose of this class is to provide a house resolver for a given house type
 * but not an actual house instatntion. We need this when e.g. drawing houses in
 * GUI to keep backward compatibility with GRFs that were created before this
 * functionality. When querying house sprites, certain GRF may read various town
 * variables e.g. the population. Since the building doesn't exists and is not
 * bounded to any town we have no real values that we can return. Instead of
 * failing, this resolver will return fake values.
 */
struct FakeTownScopeResolver : public ScopeResolver {
	FakeTownScopeResolver(ResolverObject &ro) : ScopeResolver(ro)
	{ }

	virtual uint32 GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const override;
};

/** Resolver of town properties. */
struct TownResolverObject : public ResolverObject {
	TownScopeResolver town_scope; ///< Scope resolver specific for towns.

	TownResolverObject(const struct GRFFile *grffile, Town *t, bool readonly);

	ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, VarSpriteGroupScopeOffset relative = 0) override
	{
		switch (scope) {
			case VSG_SCOPE_SELF: return &town_scope;
			default: return ResolverObject::GetScope(scope, relative);
		}
	}
};

#endif /* NEWGRF_TOWN_H */
