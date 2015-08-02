/** @file zoning.h */

#ifndef ZONING_H_
#define ZONING_H_

#include "openttd.h"
#include "tile_cmd.h"

enum EvaluationMode {
	CHECKNOTHING = 0,
	CHECKOPINION = 1,  ///< Check the local authority's opinion.
	CHECKBUILD = 2,    ///< Check wither or not the player can build.
	CHECKSTACATCH = 3, ///< Check catchment area for stations
	CHECKINDCATCH = 4, ///< Check catchment area for industries
	CHECKBULCATCH = 5, ///< Check catchment area for buildings
	CHECKBULUNSER = 6, ///< Check for unserved buildings
	CHECKINDUNSER = 7, ///< Check for unserved industries
};

struct Zoning {
	EvaluationMode inner;
	EvaluationMode outer;
	int inner_val;
	int outer_val;
};

VARDEF Zoning _zoning;

SpriteID TileZoningSpriteEvaluation(TileIndex tile, Owner owner, EvaluationMode ev_mode);

int TileZoningEvaluation(TileIndex tile, Owner owner, EvaluationMode ev_mode);

void DrawTileZoning(const TileInfo *ti);

void ShowZoningToolbar();

EvaluationMode GetEvaluationModeFromInt(int ev_mode);

#endif /*ZONING_H_*/
