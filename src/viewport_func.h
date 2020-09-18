/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file viewport_func.h Functions related to (drawing on) viewports. */

#ifndef VIEWPORT_FUNC_H
#define VIEWPORT_FUNC_H

#include "gfx_type.h"
#include "viewport_type.h"
#include "window_type.h"
#include "tile_map.h"
#include "station_type.h"
#include "vehicle_base.h"

struct TileInfo;

static const int TILE_HEIGHT_STEP = 50; ///< One Z unit tile height difference is displayed as 50m.

void SetSelectionRed(bool);

void ClearViewPortCache(Viewport *vp);
void ClearViewPortCaches();
void DeleteWindowViewport(Window *w);
void InitializeWindowViewport(Window *w, int x, int y, int width, int height, uint32 follow_flags, ZoomLevel zoom);
Viewport *IsPtInWindowViewport(const Window *w, int x, int y);
Point TranslateXYToTileCoord(const Viewport *vp, int x, int y, bool clamp_to_map = true);
Point GetTileBelowCursor();
void UpdateViewportPosition(Window *w);
void UpdateViewportSizeZoom(Viewport *vp);

void MarkAllViewportsDirty(int left, int top, int right, int bottom, const ZoomLevel mark_dirty_if_zoomlevel_is_below = ZOOM_LVL_END);
void MarkAllViewportMapsDirty(int left, int top, int right, int bottom);
void MarkAllRouteStepsDirty(const Vehicle *veh);
void MarkTileLineDirty(const TileIndex from_tile, const TileIndex to_tile);
void MarkAllRoutePathsDirty(const Vehicle *veh);
void CheckMarkDirtyFocusedRoutePaths(const Vehicle *veh);

bool DoZoomInOutWindow(ZoomStateChange how, Window *w);
void ZoomInOrOutToCursorWindow(bool in, Window * w);
Point GetTileZoomCenterWindow(bool in, Window * w);
void FixTitleGameZoom();
void HandleZoomMessage(Window *w, const Viewport *vp, byte widget_zoom_in, byte widget_zoom_out);

/**
 * Zoom a viewport as far as possible in the given direction.
 * @param how Zooming direction.
 * @param w   Window owning the viewport.
 * @pre \a how should not be #ZOOM_NONE.
 */
static inline void MaxZoomInOut(ZoomStateChange how, Window *w)
{
	while (DoZoomInOutWindow(how, w)) {};
}

void OffsetGroundSprite(int x, int y);

void DrawGroundSprite(SpriteID image, PaletteID pal, const SubSprite *sub = nullptr, int extra_offs_x = 0, int extra_offs_y = 0);
void DrawGroundSpriteAt(SpriteID image, PaletteID pal, int32 x, int32 y, int z, const SubSprite *sub = nullptr, int extra_offs_x = 0, int extra_offs_y = 0);
void AddSortableSpriteToDraw(SpriteID image, PaletteID pal, int x, int y, int w, int h, int dz, int z, bool transparent = false, int bb_offset_x = 0, int bb_offset_y = 0, int bb_offset_z = 0, const SubSprite *sub = nullptr);
void AddChildSpriteScreen(SpriteID image, PaletteID pal, int x, int y, bool transparent = false, const SubSprite *sub = nullptr, bool scale = true, bool relative = true);
void ViewportAddString(const DrawPixelInfo *dpi, ZoomLevel small_from, const ViewportSign *sign, StringID string_normal, StringID string_small, StringID string_small_shadow, uint64 params_1, uint64 params_2 = 0, Colours colour = INVALID_COLOUR);


void StartSpriteCombine();
void EndSpriteCombine();

bool HandleViewportDoubleClicked(Window *w, int x, int y);
bool HandleViewportClicked(const Viewport *vp, int x, int y, bool double_click);
void SetRedErrorSquare(TileIndex tile);
void SetTileSelectSize(int w, int h);
void SetTileSelectBigSize(int ox, int oy, int sx, int sy);

void ViewportDoDraw(Viewport *vp, int left, int top, int right, int bottom);

bool ScrollWindowToTile(TileIndex tile, Window *w, bool instant = false);
bool ScrollWindowTo(int x, int y, int z, Window *w, bool instant = false);

void UpdateActiveScrollingViewport(Window *w);

void RebuildViewportOverlay(Window *w, bool incremental);
bool IsViewportOverlayOutsideCachedRegion(Window *w);

bool ScrollMainWindowToTile(TileIndex tile, bool instant = false);
bool ScrollMainWindowTo(int x, int y, int z = -1, bool instant = false);

void UpdateAllVirtCoords();
void ClearAllCachedNames();

extern Point _tile_fract_coords;

void MarkTileDirtyByTile(const TileIndex tile, const ZoomLevel mark_dirty_if_zoomlevel_is_below, int bridge_level_offset, int tile_height_override);

/**
 * Mark a tile given by its index dirty for repaint.
 * @param tile The tile to mark dirty.
 * @param mark_dirty_if_zoomlevel_is_below To tell if an update is relevant or not (for example, animations in map mode are not).
 * @param bridge_level_offset Height of bridge on tile to also mark dirty. (Height level relative to north corner.)
 * @ingroup dirty
 */
static inline void MarkTileDirtyByTile(TileIndex tile, const ZoomLevel mark_dirty_if_zoomlevel_is_below = ZOOM_LVL_END, int bridge_level_offset = 0)
{
	MarkTileDirtyByTile(tile, mark_dirty_if_zoomlevel_is_below, bridge_level_offset, TileHeight(tile));
}

void MarkTileGroundDirtyByTile(TileIndex tile, const ZoomLevel mark_dirty_if_zoomlevel_is_below);

ViewportMapType ChangeRenderMode(const Viewport *vp, bool down);

Point GetViewportStationMiddle(const Viewport *vp, const Station *st);

void ShowTooltipForTile(Window *w, const TileIndex tile);

void ViewportMapStoreTunnel(const TileIndex tile, const TileIndex tile_south, const int tunnel_z, const bool insert_sorted);
void ViewportMapClearTunnelCache();
void ViewportMapInvalidateTunnelCacheByTile(const TileIndex tile, const Axis axis);
void ViewportMapBuildTunnelCache();

void DrawTileSelectionRect(const TileInfo *ti, PaletteID pal);
void DrawSelectionSprite(SpriteID image, PaletteID pal, const TileInfo *ti, int z_offset, FoundationPart foundation_part, const SubSprite *sub = nullptr);

struct Town;
void SetViewportCatchmentStation(const Station *st, bool sel);
void SetViewportCatchmentTown(const Town *t, bool sel);

#endif /* VIEWPORT_FUNC_H */
