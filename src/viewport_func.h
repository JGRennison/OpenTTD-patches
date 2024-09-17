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
struct ViewportDrawerDynamic;

static const int TILE_HEIGHT_STEP = 50; ///< One Z unit tile height difference is displayed as 50m.

void SetSelectionRed(bool);
void SetSelectionPalette(PaletteID);

void ClearViewportCache(Viewport *vp);
void ClearViewportLandPixelCache(Viewport *vp);
void ClearViewportCaches();
void DeleteWindowViewport(Window *w);
void InitializeWindowViewport(Window *w, int x, int y, int width, int height, uint32_t follow_flags, ZoomLevel zoom);
Viewport *IsPtInWindowViewport(const Window *w, int x, int y);
Point TranslateXYToTileCoord(const Viewport *vp, int x, int y, bool clamp_to_map = true);
Point GetTileBelowCursor();
void UpdateNextViewportPosition(Window *w, uint32_t delta_ms);
void ApplyNextViewportPosition(Window *w);
void UpdateViewportSizeZoom(Viewport *vp);

void MarkViewportDirty(Viewport * const vp, int left, int top, int right, int bottom, ViewportMarkDirtyFlags flags);
void MarkAllViewportsDirty(int left, int top, int right, int bottom, ViewportMarkDirtyFlags flags = VMDF_NONE);
void MarkAllViewportMapsDirty(int left, int top, int right, int bottom);
void MarkAllViewportMapLandscapesDirty();
void MarkWholeNonMapViewportsDirty();
void MarkAllViewportOverlayStationLinksDirty(const Station *st);
void MarkViewportLineDirty(Viewport * const vp, const Point from_pt, const Point to_pt, const int block_radius, ViewportMarkDirtyFlags flags);
void MarkTileLineDirty(const TileIndex from_tile, const TileIndex to_tile, ViewportMarkDirtyFlags flags);
void MarkDirtyFocusedRoutePaths(const Vehicle *veh);
void CheckMarkDirtyViewportRoutePaths(const Vehicle *veh);
void CheckMarkDirtyViewportRoutePaths();
void AddFixedViewportRoutePath(VehicleID veh);
void RemoveFixedViewportRoutePath(VehicleID veh);
void ChangeFixedViewportRoutePath(VehicleID from, VehicleID to);

bool DoZoomInOutWindow(ZoomStateChange how, Window *w);
void ZoomInOrOutToCursorWindow(bool in, Window * w);
void ConstrainAllViewportsZoom();
Point GetTileZoomCenterWindow(bool in, Window * w);
void FixTitleGameZoom(int zoom_adjust = 0);
void HandleZoomMessage(Window *w, const Viewport *vp, WidgetID widget_zoom_in, WidgetID widget_zoom_out);

/**
 * Zoom a viewport as far as possible in the given direction.
 * @param how Zooming direction.
 * @param w   Window owning the viewport.
 * @pre \a how should not be #ZOOM_NONE.
 */
inline void MaxZoomInOut(ZoomStateChange how, Window *w)
{
	while (DoZoomInOutWindow(how, w)) {};
}

void OffsetGroundSprite(int x, int y);

enum ViewportSortableSpriteSpecialFlags : uint8_t {
	VSSF_NONE                      =    0,
	VSSSF_SORT_SPECIAL             = 0x80, ///< When sorting sprites, if both sprites have this set, special sorting rules apply
	VSSSF_SORT_SPECIAL_TYPE_MASK   =    1, ///< Mask to use for getting the special type
	VSSSF_SORT_DIAG_VEH            =    0, ///< This is a vehicle moving diagonally with respect to the tile axes (also used for catenary pylons on diagonal track under bridges for similar reasons)
	VSSSF_SORT_SORT_BRIDGE_BB      =    1, ///< This is a bridge BB helper sprite
};
DECLARE_ENUM_AS_BIT_SET(ViewportSortableSpriteSpecialFlags);

void DrawGroundSprite(SpriteID image, PaletteID pal, const SubSprite *sub = nullptr, int extra_offs_x = 0, int extra_offs_y = 0);
void DrawGroundSpriteAt(SpriteID image, PaletteID pal, int32_t x, int32_t y, int z, const SubSprite *sub = nullptr, int extra_offs_x = 0, int extra_offs_y = 0);
void AddSortableSpriteToDraw(SpriteID image, PaletteID pal, int x, int y, int w, int h, int dz, int z, bool transparent = false, int bb_offset_x = 0, int bb_offset_y = 0, int bb_offset_z = 0, const SubSprite *sub = nullptr, ViewportSortableSpriteSpecialFlags special_flags = VSSF_NONE);
void AddChildSpriteScreen(SpriteID image, PaletteID pal, int x, int y, bool transparent = false, const SubSprite *sub = nullptr, bool scale = true, ChildScreenSpritePositionMode position_mode = ChildScreenSpritePositionMode::Relative);
void ViewportAddString(ViewportDrawerDynamic *vdd, const DrawPixelInfo *dpi, ZoomLevel small_from, const ViewportSign *sign, StringID string_normal, StringID string_small, StringID string_small_shadow, uint64_t params_1, uint64_t params_2 = 0, Colours colour = INVALID_COLOUR);


void StartSpriteCombine();
void EndSpriteCombine();

enum HandleViewportClickedResult {
	HVCR_DENY,
	HVCR_SCROLL_ONLY,
	HVCR_ALLOW,
};

bool HandleViewportDoubleClicked(Window *w, int x, int y);
HandleViewportClickedResult HandleViewportClicked(const Viewport *vp, int x, int y, bool double_click);
void SetRedErrorSquare(TileIndex tile);
void SetTileSelectSize(int w, int h);
void SetTileSelectBigSize(int ox, int oy, int sx, int sy);

void ViewportDoDraw(Viewport *vp, int left, int top, int right, int bottom, uint8_t display_flags);
void ViewportDoDrawProcessAllPending();

bool ScrollWindowToTile(TileIndex tile, Window *w, bool instant = false);
bool ScrollWindowTo(int x, int y, int z, Window *w, bool instant = false);

void UpdateActiveScrollingViewport(Window *w);

void RebuildViewportOverlay(Window *w, bool incremental);

bool ScrollMainWindowToTile(TileIndex tile, bool instant = false);
bool ScrollMainWindowTo(int x, int y, int z = -1, bool instant = false);

void UpdateAllVirtCoords();
void ClearAllCachedNames();

extern Point _tile_fract_coords;

void MarkTileDirtyByTile(const TileIndex tile, ViewportMarkDirtyFlags flags, int bridge_level_offset, int tile_height_override);

/**
 * Mark a tile given by its index dirty for repaint.
 * @param tile The tile to mark dirty.
 * @param flags To tell if an update is relevant or not (for example, animations in map mode are not).
 * @param bridge_level_offset Height of bridge on tile to also mark dirty. (Height level relative to north corner.)
 * @ingroup dirty
 */
inline void MarkTileDirtyByTile(TileIndex tile, ViewportMarkDirtyFlags flags = VMDF_NONE, int bridge_level_offset = 0)
{
	MarkTileDirtyByTile(tile, flags, bridge_level_offset, TileHeight(tile));
}

void MarkTileGroundDirtyByTile(TileIndex tile, ViewportMarkDirtyFlags flags);

void ChangeRenderMode(Viewport *vp, bool down);

Point GetViewportStationMiddle(const Viewport *vp, const Station *st);

void ShowTooltipForTile(Window *w, const TileIndex tile);

void ViewportMapStoreTunnel(const TileIndex tile, const TileIndex tile_south, const int tunnel_z, const bool insert_sorted);
void ViewportMapClearTunnelCache();
void ViewportMapInvalidateTunnelCacheByTile(const TileIndex tile, const Axis axis);
void ViewportMapBuildTunnelCache();

void DrawTileSelectionRect(const TileInfo *ti, PaletteID pal);
void DrawSelectionSprite(SpriteID image, PaletteID pal, const TileInfo *ti, int z_offset, FoundationPart foundation_part, int extra_offs_x = 0, int extra_offs_y = 0, const SubSprite *sub = nullptr);

struct Waypoint;
struct Town;
struct TraceRestrictProgram;
void SetViewportCatchmentStation(const Station *st, bool sel);
void SetViewportCatchmentWaypoint(const Waypoint *wp, bool sel);
void SetViewportCatchmentTown(const Town *t, bool sel);
void SetViewportCatchmentTraceRestrictProgram(const TraceRestrictProgram *prog, bool sel);

template<class T>
void SetViewportCatchmentSpecializedStation(const T *st, bool sel);

template<>
inline void SetViewportCatchmentSpecializedStation(const Station *st, bool sel)
{
	SetViewportCatchmentStation(st, sel);
}

template<>
inline void SetViewportCatchmentSpecializedStation(const Waypoint *st, bool sel)
{
	SetViewportCatchmentWaypoint(st, sel);
}

void MarkBridgeDirty(TileIndex begin, TileIndex end, DiagDirection direction, uint bridge_height, ViewportMarkDirtyFlags flags = VMDF_NONE);
void MarkBridgeDirty(TileIndex tile, ViewportMarkDirtyFlags flags = VMDF_NONE);
void MarkBridgeOrTunnelDirty(TileIndex tile, ViewportMarkDirtyFlags flags = VMDF_NONE);
void MarkBridgeOrTunnelDirtyOnReservationChange(TileIndex tile, ViewportMarkDirtyFlags flags = VMDF_NONE);

bool IsViewportMouseHoverActive();

#endif /* VIEWPORT_FUNC_H */
