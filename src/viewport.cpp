/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file viewport.cpp Handling of all viewports.
 *
 * \verbatim
 * The in-game coordinate system looks like this *
 *                                               *
 *                    ^ Z                        *
 *                    |                          *
 *                    |                          *
 *                    |                          *
 *                    |                          *
 *                 /     \                       *
 *              /           \                    *
 *           /                 \                 *
 *        /                       \              *
 *   X <                             > Y         *
 * \endverbatim
 */

/**
 * @defgroup vp_column_row Rows and columns in the viewport
 *
 * Columns are vertical sections of the viewport that are half a tile wide.
 * The origin, i.e. column 0, is through the northern and southern most tile.
 * This means that the column of e.g. Tile(0, 0) and Tile(100, 100) are in
 * column number 0. The negative columns are towards the left of the screen,
 * or towards the west, whereas the positive ones are towards respectively
 * the right and east.
 * With half a tile wide is meant that the next column of tiles directly west
 * or east of the centre line are respectively column -1 and 1. Their tile
 * centers are only half a tile from the center of their adjoining tile when
 * looking only at the X-coordinate.
 *
 * \verbatim
 *        ╳        *
 *       ╱ ╲       *
 *      ╳ 0 ╳      *
 *     ╱ ╲ ╱ ╲     *
 *    ╳-1 ╳ 1 ╳    *
 *   ╱ ╲ ╱ ╲ ╱ ╲   *
 *  ╳-2 ╳ 0 ╳ 2 ╳  *
 *   ╲ ╱ ╲ ╱ ╲ ╱   *
 *    ╳-1 ╳ 1 ╳    *
 *     ╲ ╱ ╲ ╱     *
 *      ╳ 0 ╳      *
 *       ╲ ╱       *
 *        ╳        *
 * \endverbatim
 *
 *
 * Rows are horizontal sections of the viewport, also half a tile wide.
 * This time the northern most tile on the map defines 0 and
 * everything south of that has a positive number.
 */

#include "stdafx.h"
#include "clear_map.h"
#include "tree_map.h"
#include "industry.h"
#include "smallmap_gui.h"
#include "smallmap_colours.h"
#include "table/tree_land.h"
#include "blitter/32bpp_base.hpp"
#include "core/math_func.hpp"
#include "core/smallvec_type.hpp"
#include "landscape.h"
#include "viewport_func.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "town.h"
#include "signs_base.h"
#include "signs_func.h"
#include "plans_base.h"
#include "plans_func.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "blitter/factory.hpp"
#include "strings_func.h"
#include "zoom_func.h"
#include "vehicle_func.h"
#include "company_func.h"
#include "waypoint_func.h"
#include "window_func.h"
#include "tilehighlight_func.h"
#include "zoning.h"
#include "window_gui.h"
#include "linkgraph/linkgraph_gui.h"
#include "viewport_kdtree.h"
#include "town_kdtree.h"
#include "viewport_sprite_sorter.h"
#include "bridge_map.h"
#include "company_base.h"
#include "command_func.h"
#include "network/network_func.h"
#include "framerate_type.h"
#include "depot_base.h"
#include "tunnelbridge_map.h"
#include "gui.h"
#include "core/container_func.hpp"
#include "tunnelbridge_map.h"
#include "video/video_driver.hpp"
#include "scope_info.h"

#include <map>
#include <vector>
#include <math.h>
#include <algorithm>
#include <tuple>

#include "table/strings.h"
#include "table/string_colours.h"

#include "safeguards.h"

Point _tile_fract_coords;


ViewportSignKdtree _viewport_sign_kdtree(&Kdtree_ViewportSignXYFunc);
bool _viewport_sign_kdtree_valid = false;
static int _viewport_sign_maxwidth = 0;


//static const int MAX_TILE_EXTENT_LEFT   = ZOOM_LVL_BASE * TILE_PIXELS;                     ///< Maximum left   extent of tile relative to north corner.
//static const int MAX_TILE_EXTENT_RIGHT  = ZOOM_LVL_BASE * TILE_PIXELS;                     ///< Maximum right  extent of tile relative to north corner.
static const int MAX_TILE_EXTENT_TOP    = ZOOM_LVL_BASE * MAX_BUILDING_PIXELS;             ///< Maximum top    extent of tile relative to north corner (not considering bridges).
static const int MAX_TILE_EXTENT_BOTTOM = ZOOM_LVL_BASE * (TILE_PIXELS + 2 * TILE_HEIGHT); ///< Maximum bottom extent of tile relative to north corner (worst case: #SLOPE_STEEP_N).

struct StringSpriteToDraw {
	StringID string;
	Colours colour;
	int32 x;
	int32 y;
	uint64 params[2];
	uint16 width;
};

struct TileSpriteToDraw {
	SpriteID image;
	PaletteID pal;
	const SubSprite *sub;           ///< only draw a rectangular part of the sprite
	int32 x;                        ///< screen X coordinate of sprite
	int32 y;                        ///< screen Y coordinate of sprite
};

struct ChildScreenSpriteToDraw {
	SpriteID image;
	PaletteID pal;
	const SubSprite *sub;           ///< only draw a rectangular part of the sprite
	int32 x;
	int32 y;
	int next;                       ///< next child to draw (-1 at the end)
	bool relative;
};

/**
 * Mode of "sprite combining"
 * @see StartSpriteCombine
 */
enum SpriteCombineMode {
	SPRITE_COMBINE_NONE,     ///< Every #AddSortableSpriteToDraw start its own bounding box
	SPRITE_COMBINE_PENDING,  ///< %Sprite combining will start with the next unclipped sprite.
	SPRITE_COMBINE_ACTIVE,   ///< %Sprite combining is active. #AddSortableSpriteToDraw outputs child sprites.
};

typedef std::vector<TileSpriteToDraw> TileSpriteToDrawVector;
typedef std::vector<StringSpriteToDraw> StringSpriteToDrawVector;
typedef std::vector<ParentSpriteToDraw> ParentSpriteToDrawVector;
typedef std::vector<ChildScreenSpriteToDraw> ChildScreenSpriteToDrawVector;

typedef std::vector<std::pair<int, OrderType> > RankOrderTypeList;
typedef std::map<TileIndex, RankOrderTypeList> RouteStepsMap;

const uint max_rank_order_type_count = 10;

enum RailSnapMode {
	RSM_NO_SNAP,
	RSM_SNAP_TO_TILE,
	RSM_SNAP_TO_RAIL,
};

/**
 * Snapping point for a track.
 *
 * Point where a track (rail/road/other) can be snapped to while selecting tracks with polyline
 * tool (HT_POLY). Besides of x/y coordinates expressed in tile "units" it contains a set of
 * allowed line directions.
 */
struct LineSnapPoint : Point {
	uint8 dirs; ///< Allowed line directions, set of #Direction bits.
};

typedef std::vector<LineSnapPoint> LineSnapPoints; ///< Set of snapping points

/** Coordinates of a polyline track made of 2 connected line segments. */
struct PolylineInfo {
	Point start;           ///< The point where the first segment starts (as given in LineSnapPoint).
	Direction first_dir;   ///< Direction of the first line segment.
	uint first_len;        ///< size of the first segment - number of track pieces.
	Direction second_dir;  ///< Direction of the second line segment.
	uint second_len;       ///< size of the second segment - number of track pieces.
};

struct TunnelToMap {
	TunnelBridgeToMap tb;
	int y_intercept;
	uint8 tunnel_z;
};
struct TunnelToMapStorage {
	std::vector<TunnelToMap> tunnels;
};

struct BridgeSetXComparator {
	bool operator() (const TileIndex a, const TileIndex b) const
	{
		return std::make_tuple(TileX(a), TileY(a)) < std::make_tuple(TileX(b), TileY(b));
	}
};

struct BridgeSetYComparator {
	bool operator() (const TileIndex a, const TileIndex b) const
	{
		return a < b;
	}
};

/** Data structure storing rendering information */
struct ViewportDrawer {
	DrawPixelInfo dpi;

	StringSpriteToDrawVector string_sprites_to_draw;
	TileSpriteToDrawVector tile_sprites_to_draw;
	ParentSpriteToDrawVector parent_sprites_to_draw;
	ParentSpriteToSortVector parent_sprites_to_sort; ///< Parent sprite pointer array used for sorting
	ChildScreenSpriteToDrawVector child_screen_sprites_to_draw;
	TunnelToMapStorage tunnel_to_map_x;
	TunnelToMapStorage tunnel_to_map_y;
	btree::btree_map<TileIndex, TileIndex, BridgeSetXComparator> bridge_to_map_x;
	btree::btree_map<TileIndex, TileIndex, BridgeSetYComparator> bridge_to_map_y;

	int *last_child;

	SpriteCombineMode combine_sprites;               ///< Current mode of "sprite combining". @see StartSpriteCombine
	uint combine_psd_index;
	int combine_left;
	int combine_right;
	int combine_top;
	int combine_bottom;

	int foundation[FOUNDATION_PART_END];             ///< Foundation sprites (index into parent_sprites_to_draw).
	FoundationPart foundation_part;                  ///< Currently active foundation for ground sprite drawing.
	int *last_foundation_child[FOUNDATION_PART_END]; ///< Tail of ChildSprite list of the foundations. (index into child_screen_sprites_to_draw)
	Point foundation_offset[FOUNDATION_PART_END];    ///< Pixel offset for ground sprites on the foundations.
};

static void MarkViewportDirty(ViewPort * const vp, int left, int top, int right, int bottom);
static void MarkRouteStepDirty(RouteStepsMap::const_iterator cit);
static void MarkRouteStepDirty(const TileIndex tile, uint order_nr);

static DrawPixelInfo _dpi_for_text;
static ViewportDrawer _vd;

static std::vector<ViewPort *> _viewport_window_cache;

RouteStepsMap _vp_route_steps;
RouteStepsMap _vp_route_steps_last_mark_dirty;
uint _vp_route_step_width = 0;
uint _vp_route_step_height_top = 0;
uint _vp_route_step_height_middle = 0;
uint _vp_route_step_height_bottom = 0;
SubSprite _vp_route_step_subsprite;

struct DrawnPathRouteTileLine {
	TileIndex from_tile;
	TileIndex to_tile;
	bool order_match;

	bool operator==(const DrawnPathRouteTileLine &other) const
	{
		return std::tie(this->from_tile, this->to_tile, this->order_match) == std::tie(other.from_tile, other.to_tile, other.order_match);
	}

	bool operator!=(const DrawnPathRouteTileLine &other) const
	{
		return !(*this == other);
	}

	bool operator<(const DrawnPathRouteTileLine &other) const
	{
		return std::tie(this->from_tile, this->to_tile, this->order_match) < std::tie(other.from_tile, other.to_tile, other.order_match);
	}
};

std::vector<DrawnPathRouteTileLine> _vp_route_paths;
std::vector<DrawnPathRouteTileLine> _vp_route_paths_last_mark_dirty;

static void MarkRoutePathsDirty(const std::vector<DrawnPathRouteTileLine> &lines);

TileHighlightData _thd;
static TileInfo *_cur_ti;
bool _draw_bounding_boxes = false;
bool _draw_dirty_blocks = false;
uint _dirty_block_colour = 0;
static VpSpriteSorter _vp_sprite_sorter = nullptr;

const byte *_pal2trsp_remap_ptr = nullptr;

static RailSnapMode _rail_snap_mode = RSM_NO_SNAP; ///< Type of rail track snapping (polyline tool).
static LineSnapPoints _tile_snap_points; ///< Tile to which a rail track will be snapped to (polyline tool).
static LineSnapPoints _rail_snap_points; ///< Set of points where a rail track will be snapped to (polyline tool).
static LineSnapPoint _current_snap_lock; ///< Start point and direction at which selected track is locked on currently (while dragging in polyline mode).

static RailSnapMode GetRailSnapMode();
static void SetRailSnapMode(RailSnapMode mode);
static TileIndex GetRailSnapTile();
static void SetRailSnapTile(TileIndex tile);

enum ViewportDebugFlags {
	VDF_DIRTY_BLOCK_PER_DRAW,
	VDF_DIRTY_WHOLE_VIEWPORT,
	VDF_DIRTY_BLOCK_PER_SPLIT,
	VDF_DISABLE_DRAW_SPLIT,
};
uint32 _viewport_debug_flags;

static Point MapXYZToViewport(const ViewPort *vp, int x, int y, int z)
{
	Point p = RemapCoords(x, y, z);
	p.x -= vp->virtual_width / 2;
	p.y -= vp->virtual_height / 2;
	return p;
}

void ClearViewPortCache(ViewPort *vp)
{
	if (vp->zoom >= ZOOM_LVL_DRAW_MAP) {
		memset(vp->map_draw_vehicles_cache.done_hash_bits, 0, sizeof(vp->map_draw_vehicles_cache.done_hash_bits));
		vp->map_draw_vehicles_cache.vehicle_pixels.assign(vp->map_draw_vehicles_cache.vehicle_pixels.size(), false);
	}
}

void ClearViewPortCaches()
{
	for (ViewPort *vp : _viewport_window_cache) {
		ClearViewPortCache(vp);
	}
}

void DeleteWindowViewport(Window *w)
{
	if (w->viewport == nullptr) return;

	container_unordered_remove(_viewport_window_cache, w->viewport);
	delete w->viewport->overlay;
	delete w->viewport;
	w->viewport = nullptr;
}

/**
 * Initialize viewport of the window for use.
 * @param w Window to use/display the viewport in
 * @param x Offset of left edge of viewport with respect to left edge window \a w
 * @param y Offset of top edge of viewport with respect to top edge window \a w
 * @param width Width of the viewport
 * @param height Height of the viewport
 * @param follow_flags Flags controlling the viewport.
 *        - If bit 31 is set, the lower 20 bits are the vehicle that the viewport should follow.
 *        - If bit 31 is clear, it is a #TileIndex.
 * @param zoom Zoomlevel to display
 */
void InitializeWindowViewport(Window *w, int x, int y,
	int width, int height, uint32 follow_flags, ZoomLevel zoom)
{
	assert(w->viewport == nullptr);

	ViewportData *vp = new ViewportData();

	vp->overlay = nullptr;
	vp->left = x + w->left;
	vp->top = y + w->top;
	vp->width = width;
	vp->height = height;

	vp->zoom = static_cast<ZoomLevel>(Clamp(zoom, _settings_client.gui.zoom_min, _settings_client.gui.zoom_max));

	vp->virtual_left = 0;
	vp->virtual_top = 0;
	vp->virtual_width = ScaleByZoom(width, vp->zoom);
	vp->virtual_height = ScaleByZoom(height, vp->zoom);

	vp->map_type = VPMT_BEGIN;

	UpdateViewportSizeZoom(vp);

	Point pt;

	if (follow_flags & 0x80000000) {
		const Vehicle *veh;

		vp->follow_vehicle = (VehicleID)(follow_flags & 0xFFFFF);
		veh = Vehicle::Get(vp->follow_vehicle);
		pt = MapXYZToViewport(vp, veh->x_pos, veh->y_pos, veh->z_pos);
		MarkAllRoutePathsDirty(veh);
		MarkAllRouteStepsDirty(veh);
	} else {
		uint x = TileX(follow_flags) * TILE_SIZE;
		uint y = TileY(follow_flags) * TILE_SIZE;

		vp->follow_vehicle = INVALID_VEHICLE;
		pt = MapXYZToViewport(vp, x, y, GetSlopePixelZ(x, y));
	}

	vp->scrollpos_x = pt.x;
	vp->scrollpos_y = pt.y;
	vp->dest_scrollpos_x = pt.x;
	vp->dest_scrollpos_y = pt.y;

	w->viewport = vp;
	_viewport_window_cache.push_back(vp);
}

static Point _vp_move_offs;

struct ViewportRedrawRegion {
	Rect coords;
};

static std::vector<ViewportRedrawRegion> _vp_redraw_regions;

static void DoViewportRedrawRegions(const Window *w, int left, int top, int width, int height)
{
	if (width <= 0 || height <= 0) return;

	FOR_ALL_WINDOWS_FROM_BACK_FROM(w, w) {
		if (left + width > w->left &&
				w->left + w->width > left &&
				top + height > w->top &&
				w->top + w->height > top) {

			if (left < w->left) {
				DoViewportRedrawRegions(w, left, top, w->left - left, height);
				DoViewportRedrawRegions(w, left + (w->left - left), top, width - (w->left - left), height);
				return;
			}

			if (left + width > w->left + w->width) {
				DoViewportRedrawRegions(w, left, top, (w->left + w->width - left), height);
				DoViewportRedrawRegions(w, left + (w->left + w->width - left), top, width - (w->left + w->width - left), height);
				return;
			}

			if (top < w->top) {
				DoViewportRedrawRegions(w, left, top, width, (w->top - top));
				DoViewportRedrawRegions(w, left, top + (w->top - top), width, height - (w->top - top));
				return;
			}

			if (top + height > w->top + w->height) {
				DoViewportRedrawRegions(w, left, top, width, (w->top + w->height - top));
				DoViewportRedrawRegions(w, left, top + (w->top + w->height - top), width, height - (w->top + w->height - top));
				return;
			}

			return;
		}
	}

	_vp_redraw_regions.push_back({ { left, top, left + width, top + height } });
}

static void DoSetViewportPositionFillRegion(int left, int top, int width, int height, int xo, int yo) {
	int src_left = left - xo;
	int src_top = top - yo;
	int src_right = src_left + width;
	int src_bottom = src_top + height;
	for (const auto &region : _vp_redraw_regions) {
		if (region.coords.left < src_right &&
				region.coords.right > src_left &&
				region.coords.top < src_bottom &&
				region.coords.bottom > src_top) {
			/* can use this region as a source */
			if (src_left < region.coords.left) {
				DoSetViewportPositionFillRegion(src_left + xo, src_top + yo, region.coords.left - src_left, height, xo, yo);
				src_left = region.coords.left;
				width = src_right - src_left;
			}
			if (src_top < region.coords.top) {
				DoSetViewportPositionFillRegion(src_left + xo, src_top + yo, width, region.coords.top - src_top, xo, yo);
				src_top = region.coords.top;
				height = src_bottom - src_top;
			}
			if (src_right > region.coords.right) {
				DoSetViewportPositionFillRegion(region.coords.right + xo, src_top + yo, src_right - region.coords.right, height, xo, yo);
				src_right = region.coords.right;
				width = src_right - src_left;
			}
			if (src_bottom > region.coords.bottom) {
				DoSetViewportPositionFillRegion(src_left + xo, region.coords.bottom + yo, width, src_bottom - region.coords.bottom, xo, yo);
				src_bottom = region.coords.bottom;
				height = src_bottom - src_top;
			}

			if (xo >= 0) {
				/* scrolling left, moving pixels right */
				width += xo;
			} else {
				/* scrolling right, moving pixels left */
				src_left += xo;
				width -= xo;
			}
			if (yo >= 0) {
				/* scrolling down, moving pixels up */
				height += yo;
			} else {
				/* scrolling up, moving pixels down */
				src_top += yo;
				height -= yo;
			}
			BlitterFactory::GetCurrentBlitter()->ScrollBuffer(_screen.dst_ptr, src_left, src_top, width, height, xo, yo);

			return;
		}
	}
	DrawOverlappedWindowForAll(left, top, left + width, top + height);
};

static void DoSetViewportPosition(Window *w, const int left, const int top, const int width, const int height)
{
	const int xo = _vp_move_offs.x;
	const int yo = _vp_move_offs.y;


	IncrementWindowUpdateNumber();

	_vp_redraw_regions.clear();
	DoViewportRedrawRegions(w, left, top, width, height);

	if (abs(xo) >= width || abs(yo) >= height) {
		/* fully outside */
		for (ViewportRedrawRegion &vrr : _vp_redraw_regions) {
			RedrawScreenRect(vrr.coords.left, vrr.coords.top, vrr.coords.right, vrr.coords.bottom);
		}
		return;
	}

	Blitter *blitter = BlitterFactory::GetCurrentBlitter();

	if (_cursor.visible) UndrawMouseCursor();

	if (_networking) NetworkUndrawChatMessage();

	if (xo != 0) {
		std::sort(_vp_redraw_regions.begin(), _vp_redraw_regions.end(), [&](const ViewportRedrawRegion &a, const ViewportRedrawRegion &b) {
			if (a.coords.right <= b.coords.left && xo > 0) return true;
			if (a.coords.left >= b.coords.right && xo < 0) return true;
			return false;
		});
		if (yo != 0) {
			std::stable_sort(_vp_redraw_regions.begin(), _vp_redraw_regions.end(), [&](const ViewportRedrawRegion &a, const ViewportRedrawRegion &b) {
				if (a.coords.bottom <= b.coords.top && yo > 0) return true;
				if (a.coords.top >= b.coords.bottom && yo < 0) return true;
				return false;
			});
		}
	} else {
		std::sort(_vp_redraw_regions.begin(), _vp_redraw_regions.end(), [&](const ViewportRedrawRegion &a, const ViewportRedrawRegion &b) {
			if (a.coords.bottom <= b.coords.top && yo > 0) return true;
			if (a.coords.top >= b.coords.bottom && yo < 0) return true;
			return false;
		});
	}

	while (!_vp_redraw_regions.empty()) {
		const Rect &rect = _vp_redraw_regions.back().coords;
		int left = rect.left;
		int top = rect.top;
		int width = rect.right - rect.left;
		int height = rect.bottom - rect.top;
		_vp_redraw_regions.pop_back();
		VideoDriver::GetInstance()->MakeDirty(left, top, width, height);
		int fill_width = abs(xo);
		int fill_height = abs(yo);
		if (fill_width < width && fill_height < height) {
			blitter->ScrollBuffer(_screen.dst_ptr, left, top, width, height, xo, yo);
		} else {
			if (width < fill_width) fill_width = width;
			if (height < fill_height) fill_height = height;
		}
		if (xo < 0) {
			/* scrolling right, moving pixels left, fill in on right */
			width -= fill_width;
			DoSetViewportPositionFillRegion(left + width, top, fill_width, height, xo, yo);
		} else if (xo > 0) {
			/* scrolling left, moving pixels right, fill in on left */
			DoSetViewportPositionFillRegion(left, top, fill_width, height, xo, yo);
			width -= fill_width;
			left += fill_width;
		}
		if (yo < 0 && width > 0) {
			/* scrolling down, moving pixels up, fill in at bottom */
			height -= fill_height;
			DoSetViewportPositionFillRegion(left, top + height, width, fill_height, xo, yo);
		} else if (yo > 0 && width > 0) {
			/* scrolling up, moving pixels down, fill in at top */
			DoSetViewportPositionFillRegion(left, top, width, fill_height, xo, yo);
		}
	}
}

inline void UpdateViewportDirtyBlockLeftMargin(ViewPort *vp)
{
	if (vp->zoom >= ZOOM_LVL_DRAW_MAP) {
		vp->dirty_block_left_margin = 0;
	} else {
		vp->dirty_block_left_margin = UnScaleByZoomLower((-vp->virtual_left) & 127, vp->zoom);
	}
}

static void SetViewportPosition(Window *w, int x, int y, bool force_update_overlay)
{
	if (unlikely(HasBit(_viewport_debug_flags, VDF_DIRTY_WHOLE_VIEWPORT))) {
		w->flags |= WF_DIRTY;
	}

	ViewPort *vp = w->viewport;
	int old_left = vp->virtual_left;
	int old_top = vp->virtual_top;
	int i;
	int left, top, width, height;

	vp->virtual_left = x;
	vp->virtual_top = y;
	UpdateViewportDirtyBlockLeftMargin(vp);

	if (force_update_overlay || IsViewportOverlayOutsideCachedRegion(w)) RebuildViewportOverlay(w, true);

	/* Viewport is bound to its left top corner, so it must be rounded down (UnScaleByZoomLower)
	 * else glitch described in FS#1412 will happen (offset by 1 pixel with zoom level > NORMAL)
	 */
	old_left = UnScaleByZoomLower(old_left, vp->zoom);
	old_top = UnScaleByZoomLower(old_top, vp->zoom);
	x = UnScaleByZoomLower(x, vp->zoom);
	y = UnScaleByZoomLower(y, vp->zoom);

	old_left -= x;
	old_top -= y;

	if (old_top == 0 && old_left == 0) return;

	_vp_move_offs.x = old_left;
	_vp_move_offs.y = old_top;

	left = vp->left;
	top = vp->top;
	width = vp->width;
	height = vp->height;

	if (left < 0) {
		width += left;
		left = 0;
	}

	i = left + width - _screen.width;
	if (i >= 0) width -= i;

	if (width > 0) {
		if (top < 0) {
			height += top;
			top = 0;
		}

		i = top + height - _screen.height;
		if (i >= 0) height -= i;

		if (height > 0 && (_vp_move_offs.x != 0 || _vp_move_offs.y != 0)) {
			SCOPE_INFO_FMT([&], "DoSetViewportPosition: %d, %d, %d, %d, %d, %d, %s", left, top, width, height, _vp_move_offs.x, _vp_move_offs.y, scope_dumper().WindowInfo(w));
			DoSetViewportPosition((Window *) w->z_front, left, top, width, height);
			ClearViewPortCache(w->viewport);
		}
	}
}

/**
 * Is a xy position inside the viewport of the window?
 * @param w Window to examine its viewport
 * @param x X coordinate of the xy position
 * @param y Y coordinate of the xy position
 * @return Pointer to the viewport if the xy position is in the viewport of the window,
 *         otherwise \c nullptr is returned.
 */
ViewPort *IsPtInWindowViewport(const Window *w, int x, int y)
{
	ViewPort *vp = w->viewport;

	if (vp != nullptr &&
			IsInsideMM(x, vp->left, vp->left + vp->width) &&
			IsInsideMM(y, vp->top, vp->top + vp->height))
		return vp;

	return nullptr;
}

/**
 * Translate screen coordinate in a viewport to underlying tile coordinate.
 *
 * Returns exact point of the map that is visible in the given place
 * of the viewport (3D perspective), height of tiles and foundations matter.
 *
 * @param vp  Viewport that contains the (\a x, \a y) screen coordinate
 * @param x   Screen x coordinate, distance in pixels from the left edge of viewport frame
 * @param y   Screen y coordinate, distance in pixels from the top edge of viewport frame
 * @param clamp_to_map Clamp the coordinate outside of the map to the closest, non-void tile within the map
 * @return Tile coordinate or (-1, -1) if given x or y is not within viewport frame
 */
Point TranslateXYToTileCoord(const ViewPort *vp, int x, int y, bool clamp_to_map)
{
	if (!IsInsideBS(x, vp->left, vp->width) || !IsInsideBS(y, vp->top, vp->height)) {
		Point pt = { -1, -1 };
		return pt;
	}

	return InverseRemapCoords2(
			ScaleByZoom(x - vp->left, vp->zoom) + vp->virtual_left,
			ScaleByZoom(y - vp->top, vp->zoom) + vp->virtual_top, clamp_to_map);
}

/* When used for zooming, check area below current coordinates (x,y)
 * and return the tile of the zoomed out/in position (zoom_x, zoom_y)
 * when you just want the tile, make x = zoom_x and y = zoom_y */
static Point GetTileFromScreenXY(int x, int y, int zoom_x, int zoom_y)
{
	Window *w;
	ViewPort *vp;
	Point pt;

	if ( (w = FindWindowFromPt(x, y)) != nullptr &&
			 (vp = IsPtInWindowViewport(w, x, y)) != nullptr)
				return TranslateXYToTileCoord(vp, zoom_x, zoom_y);

	pt.y = pt.x = -1;
	return pt;
}

Point GetTileBelowCursor()
{
	return GetTileFromScreenXY(_cursor.pos.x, _cursor.pos.y, _cursor.pos.x, _cursor.pos.y);
}


Point GetTileZoomCenterWindow(bool in, Window * w)
{
	int x, y;
	ViewPort *vp = w->viewport;

	if (in) {
		x = ((_cursor.pos.x - vp->left) >> 1) + (vp->width >> 2);
		y = ((_cursor.pos.y - vp->top) >> 1) + (vp->height >> 2);
	} else {
		x = vp->width - (_cursor.pos.x - vp->left);
		y = vp->height - (_cursor.pos.y - vp->top);
	}
	/* Get the tile below the cursor and center on the zoomed-out center */
	return GetTileFromScreenXY(_cursor.pos.x, _cursor.pos.y, x + vp->left, y + vp->top);
}

/**
 * Update the status of the zoom-buttons according to the zoom-level
 * of the viewport. This will update their status and invalidate accordingly
 * @param w Window pointer to the window that has the zoom buttons
 * @param vp pointer to the viewport whose zoom-level the buttons represent
 * @param widget_zoom_in widget index for window with zoom-in button
 * @param widget_zoom_out widget index for window with zoom-out button
 */
void HandleZoomMessage(Window *w, const ViewPort *vp, byte widget_zoom_in, byte widget_zoom_out)
{
	w->SetWidgetDisabledState(widget_zoom_in, vp->zoom <= _settings_client.gui.zoom_min);
	w->SetWidgetDirty(widget_zoom_in);

	w->SetWidgetDisabledState(widget_zoom_out, vp->zoom >= _settings_client.gui.zoom_max);
	w->SetWidgetDirty(widget_zoom_out);
}

/**
 * Schedules a tile sprite for drawing.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param x position x (world coordinates) of the sprite.
 * @param y position y (world coordinates) of the sprite.
 * @param z position z (world coordinates) of the sprite.
 * @param sub Only draw a part of the sprite.
 * @param extra_offs_x Pixel X offset for the sprite position.
 * @param extra_offs_y Pixel Y offset for the sprite position.
 */
static void AddTileSpriteToDraw(SpriteID image, PaletteID pal, int32 x, int32 y, int z, const SubSprite *sub = nullptr, int extra_offs_x = 0, int extra_offs_y = 0)
{
	assert((image & SPRITE_MASK) < MAX_SPRITES);

	/*C++17: TileSpriteToDraw &ts = */ _vd.tile_sprites_to_draw.emplace_back();
	TileSpriteToDraw &ts = _vd.tile_sprites_to_draw.back();
	ts.image = image;
	ts.pal = pal;
	ts.sub = sub;
	Point pt = RemapCoords(x, y, z);
	ts.x = pt.x + extra_offs_x;
	ts.y = pt.y + extra_offs_y;
}

/**
 * Adds a child sprite to the active foundation.
 *
 * The pixel offset of the sprite relative to the ParentSprite is the sum of the offset passed to OffsetGroundSprite() and extra_offs_?.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param sub Only draw a part of the sprite.
 * @param foundation_part Foundation part.
 * @param extra_offs_x Pixel X offset for the sprite position.
 * @param extra_offs_y Pixel Y offset for the sprite position.
 */
static void AddChildSpriteToFoundation(SpriteID image, PaletteID pal, const SubSprite *sub, FoundationPart foundation_part, int extra_offs_x, int extra_offs_y)
{
	assert(IsInsideMM(foundation_part, 0, FOUNDATION_PART_END));
	assert(_vd.foundation[foundation_part] != -1);
	Point offs = _vd.foundation_offset[foundation_part];

	/* Change the active ChildSprite list to the one of the foundation */
	int *old_child = _vd.last_child;
	_vd.last_child = _vd.last_foundation_child[foundation_part];

	AddChildSpriteScreen(image, pal, offs.x + extra_offs_x, offs.y + extra_offs_y, false, sub, false);

	/* Switch back to last ChildSprite list */
	_vd.last_child = old_child;
}

/**
 * Draws a ground sprite at a specific world-coordinate relative to the current tile.
 * If the current tile is drawn on top of a foundation the sprite is added as child sprite to the "foundation"-ParentSprite.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param x position x (world coordinates) of the sprite relative to current tile.
 * @param y position y (world coordinates) of the sprite relative to current tile.
 * @param z position z (world coordinates) of the sprite relative to current tile.
 * @param sub Only draw a part of the sprite.
 * @param extra_offs_x Pixel X offset for the sprite position.
 * @param extra_offs_y Pixel Y offset for the sprite position.
 */
void DrawGroundSpriteAt(SpriteID image, PaletteID pal, int32 x, int32 y, int z, const SubSprite *sub, int extra_offs_x, int extra_offs_y)
{
	/* Switch to first foundation part, if no foundation was drawn */
	if (_vd.foundation_part == FOUNDATION_PART_NONE) _vd.foundation_part = FOUNDATION_PART_NORMAL;

	if (_vd.foundation[_vd.foundation_part] != -1) {
		Point pt = RemapCoords(x, y, z);
		AddChildSpriteToFoundation(image, pal, sub, _vd.foundation_part, pt.x + extra_offs_x * ZOOM_LVL_BASE, pt.y + extra_offs_y * ZOOM_LVL_BASE);
	} else {
		AddTileSpriteToDraw(image, pal, _cur_ti->x + x, _cur_ti->y + y, _cur_ti->z + z, sub, extra_offs_x * ZOOM_LVL_BASE, extra_offs_y * ZOOM_LVL_BASE);
	}
}

/**
 * Draws a ground sprite for the current tile.
 * If the current tile is drawn on top of a foundation the sprite is added as child sprite to the "foundation"-ParentSprite.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param sub Only draw a part of the sprite.
 * @param extra_offs_x Pixel X offset for the sprite position.
 * @param extra_offs_y Pixel Y offset for the sprite position.
 */
void DrawGroundSprite(SpriteID image, PaletteID pal, const SubSprite *sub, int extra_offs_x, int extra_offs_y)
{
	DrawGroundSpriteAt(image, pal, 0, 0, 0, sub, extra_offs_x, extra_offs_y);
}

/**
 * Called when a foundation has been drawn for the current tile.
 * Successive ground sprites for the current tile will be drawn as child sprites of the "foundation"-ParentSprite, not as TileSprites.
 *
 * @param x sprite x-offset (screen coordinates) of ground sprites relative to the "foundation"-ParentSprite.
 * @param y sprite y-offset (screen coordinates) of ground sprites relative to the "foundation"-ParentSprite.
 */
void OffsetGroundSprite(int x, int y)
{
	/* Switch to next foundation part */
	switch (_vd.foundation_part) {
		case FOUNDATION_PART_NONE:
			_vd.foundation_part = FOUNDATION_PART_NORMAL;
			break;
		case FOUNDATION_PART_NORMAL:
			_vd.foundation_part = FOUNDATION_PART_HALFTILE;
			break;
		default: NOT_REACHED();
	}

	/* _vd.last_child == nullptr if foundation sprite was clipped by the viewport bounds */
	if (_vd.last_child != nullptr) _vd.foundation[_vd.foundation_part] = (uint)_vd.parent_sprites_to_draw.size() - 1;

	_vd.foundation_offset[_vd.foundation_part].x = x * ZOOM_LVL_BASE;
	_vd.foundation_offset[_vd.foundation_part].y = y * ZOOM_LVL_BASE;
	_vd.last_foundation_child[_vd.foundation_part] = _vd.last_child;
}

/**
 * Adds a child sprite to a parent sprite.
 * In contrast to "AddChildSpriteScreen()" the sprite position is in world coordinates
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param x position x of the sprite.
 * @param y position y of the sprite.
 * @param z position z of the sprite.
 * @param sub Only draw a part of the sprite.
 */
static void AddCombinedSprite(SpriteID image, PaletteID pal, int x, int y, int z, const SubSprite *sub)
{
	Point pt = RemapCoords(x, y, z);
	const Sprite *spr = GetSprite(image & SPRITE_MASK, ST_NORMAL);

	int left = pt.x + spr->x_offs;
	int right = pt.x + spr->x_offs + spr->width;
	int top = pt.y + spr->y_offs;
	int bottom = pt.y + spr->y_offs + spr->height;
	if (left >= _vd.dpi.left + _vd.dpi.width ||
			right <= _vd.dpi.left ||
			top >= _vd.dpi.top + _vd.dpi.height ||
			bottom <= _vd.dpi.top)
		return;

	AddChildSpriteScreen(image, pal, pt.x, pt.y, false, sub, false, false);
	if (left < _vd.combine_left) _vd.combine_left = left;
	if (right > _vd.combine_right) _vd.combine_right = right;
	if (top < _vd.combine_top) _vd.combine_top = top;
	if (bottom > _vd.combine_bottom) _vd.combine_bottom = bottom;
}

/**
 * Draw a (transparent) sprite at given coordinates with a given bounding box.
 * The bounding box extends from (x + bb_offset_x, y + bb_offset_y, z + bb_offset_z) to (x + w - 1, y + h - 1, z + dz - 1), both corners included.
 * Bounding boxes with bb_offset_x == w or bb_offset_y == h or bb_offset_z == dz are allowed and produce thin slices.
 *
 * @note Bounding boxes are normally specified with bb_offset_x = bb_offset_y = bb_offset_z = 0. The extent of the bounding box in negative direction is
 *       defined by the sprite offset in the grf file.
 *       However if modifying the sprite offsets is not suitable (e.g. when using existing graphics), the bounding box can be tuned by bb_offset.
 *
 * @pre w >= bb_offset_x, h >= bb_offset_y, dz >= bb_offset_z. Else w, h or dz are ignored.
 *
 * @param image the image to combine and draw,
 * @param pal the provided palette,
 * @param x position X (world) of the sprite,
 * @param y position Y (world) of the sprite,
 * @param w bounding box extent towards positive X (world),
 * @param h bounding box extent towards positive Y (world),
 * @param dz bounding box extent towards positive Z (world),
 * @param z position Z (world) of the sprite,
 * @param transparent if true, switch the palette between the provided palette and the transparent palette,
 * @param bb_offset_x bounding box extent towards negative X (world),
 * @param bb_offset_y bounding box extent towards negative Y (world),
 * @param bb_offset_z bounding box extent towards negative Z (world)
 * @param sub Only draw a part of the sprite.
 */
void AddSortableSpriteToDraw(SpriteID image, PaletteID pal, int x, int y, int w, int h, int dz, int z, bool transparent, int bb_offset_x, int bb_offset_y, int bb_offset_z, const SubSprite *sub)
{
	int32 left, right, top, bottom;

	assert((image & SPRITE_MASK) < MAX_SPRITES);

	/* make the sprites transparent with the right palette */
	if (transparent) {
		SetBit(image, PALETTE_MODIFIER_TRANSPARENT);
		pal = PALETTE_TO_TRANSPARENT;
	}

	if (_vd.combine_sprites == SPRITE_COMBINE_ACTIVE) {
		AddCombinedSprite(image, pal, x, y, z, sub);
		return;
	}

	_vd.last_child = nullptr;

	Point pt = RemapCoords(x, y, z);
	int tmp_left, tmp_top, tmp_x = pt.x, tmp_y = pt.y;
	uint16 tmp_width, tmp_height;

	/* Compute screen extents of sprite */
	if (unlikely(image == SPR_EMPTY_BOUNDING_BOX)) {
		left = tmp_left = RemapCoords(x + w          , y + bb_offset_y, z + bb_offset_z).x;
		right           = RemapCoords(x + bb_offset_x, y + h          , z + bb_offset_z).x + 1;
		top  = tmp_top  = RemapCoords(x + bb_offset_x, y + bb_offset_y, z + dz         ).y;
		bottom          = RemapCoords(x + w          , y + h          , z + bb_offset_z).y + 1;
		tmp_width = right - left;
		tmp_height = bottom - top;
	} else {
		const Sprite *spr = GetSprite(image & SPRITE_MASK, ST_NORMAL);
		left = tmp_left = (pt.x += spr->x_offs);
		right           = (pt.x +  spr->width );
		top  = tmp_top  = (pt.y += spr->y_offs);
		bottom          = (pt.y +  spr->height);
		tmp_width = spr->width;
		tmp_height = spr->height;
	}

	if (unlikely(_draw_bounding_boxes && (image != SPR_EMPTY_BOUNDING_BOX))) {
		/* Compute maximal extents of sprite and its bounding box */
		left   = min(left  , RemapCoords(x + w          , y + bb_offset_y, z + bb_offset_z).x);
		right  = max(right , RemapCoords(x + bb_offset_x, y + h          , z + bb_offset_z).x + 1);
		top    = min(top   , RemapCoords(x + bb_offset_x, y + bb_offset_y, z + dz         ).y);
		bottom = max(bottom, RemapCoords(x + w          , y + h          , z + bb_offset_z).y + 1);
	}

	/* Do not add the sprite to the viewport, if it is outside */
	if (left   >= _vd.dpi.left + _vd.dpi.width ||
	    right  <= _vd.dpi.left                 ||
	    top    >= _vd.dpi.top + _vd.dpi.height ||
	    bottom <= _vd.dpi.top) {
		return;
	}

	/*C++17: ParentSpriteToDraw &ps = */ _vd.parent_sprites_to_draw.emplace_back();
	ParentSpriteToDraw &ps = _vd.parent_sprites_to_draw.back();
	ps.x = tmp_x;
	ps.y = tmp_y;

	ps.left = tmp_left;
	ps.top  = tmp_top;

	ps.image = image;
	ps.pal = pal;
	ps.sub = sub;
	ps.xmin = x + bb_offset_x;
	ps.xmax = x + max(bb_offset_x, w) - 1;

	ps.ymin = y + bb_offset_y;
	ps.ymax = y + max(bb_offset_y, h) - 1;

	ps.zmin = z + bb_offset_z;
	ps.zmax = z + max(bb_offset_z, dz) - 1;

	ps.first_child = -1;
	ps.width = tmp_width;
	ps.height = tmp_height;

	/* bit 15 of ps.height */
	// ps.comparison_done = false;

	_vd.last_child = &ps.first_child;

	if (_vd.combine_sprites == SPRITE_COMBINE_PENDING) {
		_vd.combine_sprites = SPRITE_COMBINE_ACTIVE;
		_vd.combine_psd_index = _vd.parent_sprites_to_draw.size() - 1;
		_vd.combine_left = tmp_left;
		_vd.combine_right = right;
		_vd.combine_top = tmp_top;
		_vd.combine_bottom = bottom;
	}
}

/**
 * Starts a block of sprites, which are "combined" into a single bounding box.
 *
 * Subsequent calls to #AddSortableSpriteToDraw will be drawn into the same bounding box.
 * That is: The first sprite that is not clipped by the viewport defines the bounding box, and
 * the following sprites will be child sprites to that one.
 *
 * That implies:
 *  - The drawing order is definite. No other sprites will be sorted between those of the block.
 *  - You have to provide a valid bounding box for all sprites,
 *    as you won't know which one is the first non-clipped one.
 *    Preferable you use the same bounding box for all.
 *  - You cannot use #AddChildSpriteScreen inside the block, as its result will be indefinite.
 *
 * The block is terminated by #EndSpriteCombine.
 *
 * You cannot nest "combined" blocks.
 */
void StartSpriteCombine()
{
	assert(_vd.combine_sprites == SPRITE_COMBINE_NONE);
	_vd.combine_sprites = SPRITE_COMBINE_PENDING;
}

/**
 * Terminates a block of sprites started by #StartSpriteCombine.
 * Take a look there for details.
 */
void EndSpriteCombine()
{
	assert(_vd.combine_sprites != SPRITE_COMBINE_NONE);
	if (_vd.combine_sprites == SPRITE_COMBINE_ACTIVE) {
		ParentSpriteToDraw &ps = _vd.parent_sprites_to_draw[_vd.combine_psd_index];
		ps.left = _vd.combine_left;
		ps.top = _vd.combine_top;
		ps.width = _vd.combine_right - _vd.combine_left;
		ps.height = _vd.combine_bottom - _vd.combine_top;
	}
	_vd.combine_sprites = SPRITE_COMBINE_NONE;
}

/**
 * Check if the parameter "check" is inside the interval between
 * begin and end, including both begin and end.
 * @note Whether \c begin or \c end is the biggest does not matter.
 *       This method will account for that.
 * @param begin The begin of the interval.
 * @param end   The end of the interval.
 * @param check The value to check.
 */
static bool IsInRangeInclusive(int begin, int end, int check)
{
	if (begin > end) Swap(begin, end);
	return begin <= check && check <= end;
}

/**
 * Checks whether a point is inside the selected rectangle given by _thd.size, _thd.pos and _thd.diagonal
 * @param x The x coordinate of the point to be checked.
 * @param y The y coordinate of the point to be checked.
 * @return True if the point is inside the rectangle, else false.
 */
static bool IsInsideSelectedRectangle(int x, int y)
{
	if (!_thd.diagonal) {
		return IsInsideBS(x, _thd.pos.x, _thd.size.x) && IsInsideBS(y, _thd.pos.y, _thd.size.y);
	}

	int dist_a = (_thd.size.x + _thd.size.y);      // Rotated coordinate system for selected rectangle.
	int dist_b = (_thd.size.x - _thd.size.y);      // We don't have to divide by 2. It's all relative!
	int a = ((x - _thd.pos.x) + (y - _thd.pos.y)); // Rotated coordinate system for the point under scrutiny.
	int b = ((x - _thd.pos.x) - (y - _thd.pos.y));

	/* Check if a and b are between 0 and dist_a or dist_b respectively. */
	return IsInRangeInclusive(dist_a, 0, a) && IsInRangeInclusive(dist_b, 0, b);
}

/**
 * Add a child sprite to a parent sprite.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param x sprite x-offset (screen coordinates), optionally relative to parent sprite.
 * @param y sprite y-offset (screen coordinates), optionally relative to parent sprite.
 * @param transparent if true, switch the palette between the provided palette and the transparent palette,
 * @param sub Only draw a part of the sprite.
 * @param relative Whether coordinates are relative.
 */
void AddChildSpriteScreen(SpriteID image, PaletteID pal, int x, int y, bool transparent, const SubSprite *sub, bool scale, bool relative)
{
	assert((image & SPRITE_MASK) < MAX_SPRITES);

	/* If the ParentSprite was clipped by the viewport bounds, do not draw the ChildSprites either */
	if (_vd.last_child == nullptr) return;

	/* make the sprites transparent with the right palette */
	if (transparent) {
		SetBit(image, PALETTE_MODIFIER_TRANSPARENT);
		pal = PALETTE_TO_TRANSPARENT;
	}

	*_vd.last_child = (uint)_vd.child_screen_sprites_to_draw.size();

	/*C++17: ChildScreenSpriteToDraw &cs = */ _vd.child_screen_sprites_to_draw.emplace_back();
	ChildScreenSpriteToDraw &cs = _vd.child_screen_sprites_to_draw.back();
	cs.image = image;
	cs.pal = pal;
	cs.sub = sub;
	cs.x = scale ? x * ZOOM_LVL_BASE : x;
	cs.y = scale ? y * ZOOM_LVL_BASE : y;
	cs.next = -1;
	cs.relative = relative;

	/* Append the sprite to the active ChildSprite list.
	 * If the active ParentSprite is a foundation, update last_foundation_child as well.
	 * Note: ChildSprites of foundations are NOT sequential in the vector, as selection sprites are added at last. */
	if (_vd.last_foundation_child[0] == _vd.last_child) _vd.last_foundation_child[0] = &cs.next;
	if (_vd.last_foundation_child[1] == _vd.last_child) _vd.last_foundation_child[1] = &cs.next;
	_vd.last_child = &cs.next;
}

static void AddStringToDraw(int x, int y, StringID string, uint64 params_1, uint64 params_2, Colours colour, uint16 width)
{
	assert(width != 0);
	/*C++17: StringSpriteToDraw &ss = */ _vd.string_sprites_to_draw.emplace_back();
	StringSpriteToDraw &ss = _vd.string_sprites_to_draw.back();
	ss.string = string;
	ss.x = x;
	ss.y = y;
	ss.params[0] = params_1;
	ss.params[1] = params_2;
	ss.width = width;
	ss.colour = colour;
}


/**
 * Draws sprites between ground sprite and everything above.
 *
 * The sprite is either drawn as TileSprite or as ChildSprite of the active foundation.
 *
 * @param image the image to draw.
 * @param pal the provided palette.
 * @param ti TileInfo Tile that is being drawn
 * @param z_offset Z offset relative to the groundsprite. Only used for the sprite position, not for sprite sorting.
 * @param foundation_part Foundation part the sprite belongs to.
 * @param sub Sub-section of sprite to draw.
 */
void DrawSelectionSprite(SpriteID image, PaletteID pal, const TileInfo *ti, int z_offset, FoundationPart foundation_part, const SubSprite *sub)
{
	/* FIXME: This is not totally valid for some autorail highlights that extend over the edges of the tile. */
	if (_vd.foundation[foundation_part] == -1) {
		/* draw on real ground */
		AddTileSpriteToDraw(image, pal, ti->x, ti->y, ti->z + z_offset, sub);
	} else {
		/* draw on top of foundation */
		AddChildSpriteToFoundation(image, pal, sub, foundation_part, 0, -z_offset * ZOOM_LVL_BASE);
	}
}

/**
 * Draws a selection rectangle on a tile.
 *
 * @param ti TileInfo Tile that is being drawn
 * @param pal Palette to apply.
 */
void DrawTileSelectionRect(const TileInfo *ti, PaletteID pal)
{
	if (!IsValidTile(ti->tile)) return;

	SpriteID sel;
	if (IsHalftileSlope(ti->tileh)) {
		Corner halftile_corner = GetHalftileSlopeCorner(ti->tileh);
		SpriteID sel2 = SPR_HALFTILE_SELECTION_FLAT + halftile_corner;
		DrawSelectionSprite(sel2, pal, ti, 7 + TILE_HEIGHT, FOUNDATION_PART_HALFTILE);

		Corner opposite_corner = OppositeCorner(halftile_corner);
		if (IsSteepSlope(ti->tileh)) {
			sel = SPR_HALFTILE_SELECTION_DOWN;
		} else {
			sel = ((ti->tileh & SlopeWithOneCornerRaised(opposite_corner)) != 0 ? SPR_HALFTILE_SELECTION_UP : SPR_HALFTILE_SELECTION_FLAT);
		}
		sel += opposite_corner;
	} else {
		sel = SPR_SELECT_TILE + SlopeToSpriteOffset(ti->tileh);
	}
	DrawSelectionSprite(sel, pal, ti, 7, FOUNDATION_PART_NORMAL);
}

static HighLightStyle GetPartOfAutoLine(int px, int py, const Point &selstart, const Point &selend, HighLightStyle dir)
{
	if (!IsInRangeInclusive(selstart.x & ~TILE_UNIT_MASK, selend.x & ~TILE_UNIT_MASK, px)) return HT_DIR_END;
	if (!IsInRangeInclusive(selstart.y & ~TILE_UNIT_MASK, selend.y & ~TILE_UNIT_MASK, py)) return HT_DIR_END;

	px -= selstart.x & ~TILE_UNIT_MASK;
	py -= selstart.y & ~TILE_UNIT_MASK;

	switch (dir) {
		case HT_DIR_X: return (py == 0) ? HT_DIR_X : HT_DIR_END;
		case HT_DIR_Y: return (px == 0) ? HT_DIR_Y : HT_DIR_END;
		case HT_DIR_HU: return (px == -py) ? HT_DIR_HU : (px == -py - (int)TILE_SIZE) ? HT_DIR_HL : HT_DIR_END;
		case HT_DIR_HL: return (px == -py) ? HT_DIR_HL : (px == -py + (int)TILE_SIZE) ? HT_DIR_HU : HT_DIR_END;
		case HT_DIR_VL: return (px ==  py) ? HT_DIR_VL : (px ==  py + (int)TILE_SIZE) ? HT_DIR_VR : HT_DIR_END;
		case HT_DIR_VR: return (px ==  py) ? HT_DIR_VR : (px ==  py - (int)TILE_SIZE) ? HT_DIR_VL : HT_DIR_END;
		default: NOT_REACHED(); break;
	}

	return HT_DIR_END;
}

#include "table/autorail.h"

/**
 * Draws autorail highlights.
 *
 * @param *ti TileInfo Tile that is being drawn
 * @param autorail_type \c HT_DIR_XXX, offset into _AutorailTilehSprite[][]
 * @param pal Palette to use, -1 to autodetect
 */
static void DrawAutorailSelection(const TileInfo *ti, HighLightStyle autorail_type, PaletteID pal = -1)
{
	SpriteID image;
	FoundationPart foundation_part = FOUNDATION_PART_NORMAL;
	int offset;
	bool bridge_head_mode = false;

	if (IsFlatRailBridgeHeadTile(ti->tile)) {
		extern bool IsValidFlatRailBridgeHeadTrackBits(Slope normalised_slope, DiagDirection bridge_direction, TrackBits tracks);

		offset = _AutorailTilehSprite[SLOPE_FLAT][autorail_type];
		const Slope real_tileh = GetTileSlope(ti->tile, nullptr);
		const Slope normalised_tileh = IsSteepSlope(real_tileh) ? SlopeWithOneCornerRaised(GetHighestSlopeCorner(real_tileh)) : real_tileh;
		if (!IsValidFlatRailBridgeHeadTrackBits(normalised_tileh, GetTunnelBridgeDirection(ti->tile), TrackToTrackBits((Track) autorail_type))) {
			offset = -offset;
		}
		if (!IsRailCustomBridgeHead(ti->tile)) {
			bridge_head_mode = true;
		}
	} else {
		Slope autorail_tileh = RemoveHalftileSlope(ti->tileh);
		if (IsHalftileSlope(ti->tileh)) {
			static const HighLightStyle _lower_rail[CORNER_END] = { HT_DIR_VR, HT_DIR_HU, HT_DIR_VL, HT_DIR_HL }; // CORNER_W, CORNER_S, CORNER_E, CORNER_N
			Corner halftile_corner = GetHalftileSlopeCorner(ti->tileh);
			if (autorail_type != _lower_rail[halftile_corner]) {
				foundation_part = FOUNDATION_PART_HALFTILE;
				/* Here we draw the highlights of the "three-corners-raised"-slope. That looks ok to me. */
				autorail_tileh = SlopeWithThreeCornersRaised(OppositeCorner(halftile_corner));
			}
		}
		assert(autorail_type < HT_DIR_END);
		offset = _AutorailTilehSprite[autorail_tileh][autorail_type];
	}

	if (offset >= 0) {
		image = SPR_AUTORAIL_BASE + offset;
		if (pal == (PaletteID)-1) pal = _thd.make_square_red ? PALETTE_SEL_TILE_RED : PAL_NONE;
	} else {
		image = SPR_AUTORAIL_BASE - offset;
		if (pal == (PaletteID)-1) pal = PALETTE_SEL_TILE_RED;
	}

	if (bridge_head_mode) {
		AddSortableSpriteToDraw(image, pal, ti->x, ti->y, 16, 16, 0, ti->z + 15);
	} else {
		DrawSelectionSprite(image, pal, ti, 7, foundation_part);
	}
}

enum TileHighlightType {
	THT_NONE,
	THT_WHITE,
	THT_BLUE,
	THT_RED,
	THT_LIGHT_BLUE,
};

const Station *_viewport_highlight_station; ///< Currently selected station for coverage area highlight
const Town *_viewport_highlight_town;       ///< Currently selected town for coverage area highlight

/**
 * Get tile highlight type of coverage area for a given tile.
 * @param t Tile that is being drawn
 * @return Tile highlight type to draw
 */
static TileHighlightType GetTileHighlightType(TileIndex t)
{
	if (_viewport_highlight_station != nullptr) {
		if (IsTileType(t, MP_STATION) && GetStationIndex(t) == _viewport_highlight_station->index) return THT_LIGHT_BLUE;
		if (_viewport_highlight_station->TileIsInCatchment(t)) return THT_BLUE;
	}

	if (_viewport_highlight_town != nullptr) {
		if (IsTileType(t, MP_HOUSE)) {
			if (GetTownIndex(t) == _viewport_highlight_town->index) {
				TileHighlightType type = THT_RED;
				for (const Station *st : _viewport_highlight_town->stations_near) {
					if (st->owner != _current_company) continue;
					if (st->TileIsInCatchment(t)) return THT_BLUE;
				}
				return type;
			}
		} else if (IsTileType(t, MP_STATION)) {
			for (const Station *st : _viewport_highlight_town->stations_near) {
				if (st->owner != _current_company) continue;
				if (GetStationIndex(t) == st->index) return THT_WHITE;
			}
		}
	}

	return THT_NONE;
}

/**
 * Draw tile highlight for coverage area highlight.
 * @param *ti TileInfo Tile that is being drawn
 * @param tht Highlight type to draw.
 */
static void DrawTileHighlightType(const TileInfo *ti, TileHighlightType tht)
{
	switch (tht) {
		default:
		case THT_NONE: break;
		case THT_WHITE: DrawTileSelectionRect(ti, PAL_NONE); break;
		case THT_BLUE:  DrawTileSelectionRect(ti, PALETTE_SEL_TILE_BLUE); break;
		case THT_RED:   DrawTileSelectionRect(ti, PALETTE_TILE_RED_PULSATING); break;
		case THT_LIGHT_BLUE: DrawTileSelectionRect(ti, SPR_ZONING_INNER_HIGHLIGHT_LIGHT_BLUE); break;
	}
}

/**
 * Highlights tiles insede local authority of selected towns.
 * @param *ti TileInfo Tile that is being drawn
 */
static void HighlightTownLocalAuthorityTiles(const TileInfo *ti)
{
	/* Going through cases in order of computational time. */

	if (_town_local_authority_kdtree.Count() == 0) return;

	/* Tile belongs to town regardless of distance from town. */
	if (GetTileType(ti->tile) == MP_HOUSE) {
		if (!Town::GetByTile(ti->tile)->show_zone) return;

		DrawTileSelectionRect(ti, PALETTE_CRASH);
		return;
	}

	/* If the closest town in the highlighted list is far, we can stop searching. */
	TownID tid = _town_local_authority_kdtree.FindNearest(TileX(ti->tile), TileY(ti->tile));
	Town *closest_highlighted_town = Town::Get(tid);

	if (DistanceManhattan(ti->tile, closest_highlighted_town->xy) >= _settings_game.economy.dist_local_authority) return;

	/* Tile is inside of the local autrhority distance of a highlighted town,
	   but it is possible that a non-highlighted town is even closer. */
	Town *closest_town = ClosestTownFromTile(ti->tile, _settings_game.economy.dist_local_authority);

	if (closest_town->show_zone) {
		DrawTileSelectionRect(ti, PALETTE_CRASH);
	}

}

/**
 * Checks if the specified tile is selected and if so draws selection using correct selectionstyle.
 * @param *ti TileInfo Tile that is being drawn
 */
static void DrawTileSelection(const TileInfo *ti)
{
	/* Highlight tiles insede local authority of selected towns. */
	HighlightTownLocalAuthorityTiles(ti);

	/* Draw a red error square? */
	bool is_redsq = _thd.redsq == ti->tile;
	if (is_redsq) DrawTileSelectionRect(ti, PALETTE_TILE_RED_PULSATING);

	TileHighlightType tht = GetTileHighlightType(ti->tile);
	DrawTileHighlightType(ti, tht);

	switch (_thd.drawstyle & HT_DRAG_MASK) {
		default: break; // No tile selection active?

		case HT_RECT:
			if (!is_redsq) {
				if (IsInsideSelectedRectangle(ti->x, ti->y)) {
					DrawTileSelectionRect(ti, _thd.make_square_red ? PALETTE_SEL_TILE_RED : PAL_NONE);
				} else if (_thd.outersize.x > 0 && (tht == THT_NONE || tht == THT_RED) &&
						/* Check if it's inside the outer area? */
						IsInsideBS(ti->x, _thd.pos.x + _thd.offs.x, _thd.size.x + _thd.outersize.x) &&
						IsInsideBS(ti->y, _thd.pos.y + _thd.offs.y, _thd.size.y + _thd.outersize.y)) {
					/* Draw a blue rect. */
					DrawTileSelectionRect(ti, PALETTE_SEL_TILE_BLUE);
				}
			}
			break;

		case HT_POINT:
			if (IsInsideSelectedRectangle(ti->x, ti->y)) {
				/* Figure out the Z coordinate for the single dot. */
				int z = 0;
				FoundationPart foundation_part = FOUNDATION_PART_NORMAL;
				if (ti->tileh & SLOPE_N) {
					z += TILE_HEIGHT;
					if (RemoveHalftileSlope(ti->tileh) == SLOPE_STEEP_N) z += TILE_HEIGHT;
				}
				if (IsHalftileSlope(ti->tileh)) {
					Corner halftile_corner = GetHalftileSlopeCorner(ti->tileh);
					if ((halftile_corner == CORNER_W) || (halftile_corner == CORNER_E)) z += TILE_HEIGHT;
					if (halftile_corner != CORNER_S) {
						foundation_part = FOUNDATION_PART_HALFTILE;
						if (IsSteepSlope(ti->tileh)) z -= TILE_HEIGHT;
					}
				}
				DrawSelectionSprite(_cur_dpi->zoom <= ZOOM_LVL_DETAIL ? SPR_DOT : SPR_DOT_SMALL, PAL_NONE, ti, z, foundation_part);
			}
			break;

		case HT_RAIL:
			if (ti->tile == TileVirtXY(_thd.pos.x, _thd.pos.y)) {
				assert((_thd.drawstyle & HT_DIR_MASK) < HT_DIR_END);
				DrawAutorailSelection(ti, _thd.drawstyle & HT_DIR_MASK);
			}
			break;

		case HT_LINE: {
			HighLightStyle type = GetPartOfAutoLine(ti->x, ti->y, _thd.selstart, _thd.selend, _thd.drawstyle & HT_DIR_MASK);
			if (type < HT_DIR_END) {
				DrawAutorailSelection(ti, type);
			} else if (_thd.dir2 < HT_DIR_END) {
				type = GetPartOfAutoLine(ti->x, ti->y, _thd.selstart2, _thd.selend2, _thd.dir2);
				if (type < HT_DIR_END) DrawAutorailSelection(ti, type, PALETTE_SEL_TILE_BLUE);
			}
			break;
		}
	}
}

/**
 * Returns the y coordinate in the viewport coordinate system where the given
 * tile is painted.
 * @param tile Any tile.
 * @return The viewport y coordinate where the tile is painted.
 */
static int GetViewportY(Point tile)
{
	/* Each increment in X or Y direction moves down by half a tile, i.e. TILE_PIXELS / 2. */
	return (tile.y * (int)(TILE_PIXELS / 2) + tile.x * (int)(TILE_PIXELS / 2) - TilePixelHeightOutsideMap(tile.x, tile.y)) << ZOOM_LVL_SHIFT;
}

/**
 * Add the landscape to the viewport, i.e. all ground tiles and buildings.
 */
static void ViewportAddLandscape()
{
	assert(_vd.dpi.top <= _vd.dpi.top + _vd.dpi.height);
	assert(_vd.dpi.left <= _vd.dpi.left + _vd.dpi.width);

	Point upper_left = InverseRemapCoords(_vd.dpi.left, _vd.dpi.top);
	Point upper_right = InverseRemapCoords(_vd.dpi.left + _vd.dpi.width, _vd.dpi.top);

	/* Transformations between tile coordinates and viewport rows/columns: See vp_column_row
	 *   column = y - x
	 *   row    = x + y
	 *   x      = (row - column) / 2
	 *   y      = (row + column) / 2
	 * Note: (row, columns) pairs are only valid, if they are both even or both odd.
	 */

	/* Columns overlap with neighbouring columns by a half tile.
	 *  - Left column is column of upper_left (rounded down) and one column to the left.
	 *  - Right column is column of upper_right (rounded up) and one column to the right.
	 * Note: Integer-division does not round down for negative numbers, so ensure rounding with another increment/decrement.
	 */
	int left_column = DivTowardsNegativeInf(upper_left.y - upper_left.x, (int)TILE_SIZE) - 1;
	int right_column = DivTowardsPositiveInf(upper_right.y - upper_right.x, (int)TILE_SIZE) + 1;

	int potential_bridge_height = ZOOM_LVL_BASE * TILE_HEIGHT * _settings_game.construction.max_bridge_height;

	/* Rows overlap with neighbouring rows by a half tile.
	 * The first row that could possibly be visible is the row above upper_left (if it is at height 0).
	 * Due to integer-division not rounding down for negative numbers, we need another decrement.
	 */
	int row = DivTowardsNegativeInf(upper_left.y + upper_left.x, (int)TILE_SIZE) - 1;
	bool last_row = false;
	for (; !last_row; row++) {
		last_row = true;
		for (int column = left_column; column <= right_column; column++) {
			/* Valid row/column? */
			if ((row + column) % 2 != 0) continue;

			Point tilecoord;
			tilecoord.x = (row - column) / 2;
			tilecoord.y = (row + column) / 2;
			assert(column == tilecoord.y - tilecoord.x);
			assert(row == tilecoord.y + tilecoord.x);

			TileType tile_type;
			TileInfo tile_info;
			_cur_ti = &tile_info;
			tile_info.x = tilecoord.x * TILE_SIZE; // FIXME tile_info should use signed integers
			tile_info.y = tilecoord.y * TILE_SIZE;

			if (IsInsideBS(tilecoord.x, 0, MapSizeX()) && IsInsideBS(tilecoord.y, 0, MapSizeY())) {
				/* This includes the south border at MapMaxX / MapMaxY. When terraforming we still draw tile selections there. */
				tile_info.tile = TileXY(tilecoord.x, tilecoord.y);
				tile_type = GetTileType(tile_info.tile);
			} else {
				tile_info.tile = INVALID_TILE;
				tile_type = MP_VOID;
			}

			if (tile_type != MP_VOID) {
				/* We are inside the map => paint landscape. */
				tile_info.tileh = GetTilePixelSlope(tile_info.tile, &tile_info.z);
			} else {
				/* We are outside the map => paint black. */
				tile_info.tileh = GetTilePixelSlopeOutsideMap(tilecoord.x, tilecoord.y, &tile_info.z);
			}

			int viewport_y = GetViewportY(tilecoord);

			if (viewport_y + MAX_TILE_EXTENT_BOTTOM < _vd.dpi.top) {
				/* The tile in this column is not visible yet.
				 * Tiles in other columns may be visible, but we need more rows in any case. */
				last_row = false;
				continue;
			}

			int min_visible_height = viewport_y - (_vd.dpi.top + _vd.dpi.height);
			bool tile_visible = min_visible_height <= 0;

			if (tile_type != MP_VOID) {
				/* Is tile with buildings visible? */
				if (min_visible_height < MAX_TILE_EXTENT_TOP) tile_visible = true;

				if (IsBridgeAbove(tile_info.tile)) {
					/* Is the bridge visible? */
					TileIndex bridge_tile = GetNorthernBridgeEnd(tile_info.tile);
					int bridge_height = ZOOM_LVL_BASE * (GetBridgePixelHeight(bridge_tile) - TilePixelHeight(tile_info.tile));
					if (min_visible_height < bridge_height + MAX_TILE_EXTENT_TOP) tile_visible = true;
				}

				/* Would a higher bridge on a more southern tile be visible?
				 * If yes, we need to loop over more rows to possibly find one. */
				if (min_visible_height < potential_bridge_height + MAX_TILE_EXTENT_TOP) last_row = false;
			} else {
				/* Outside of map. If we are on the north border of the map, there may still be a bridge visible,
				 * so we need to loop over more rows to possibly find one. */
				if ((tilecoord.x <= 0 || tilecoord.y <= 0) && min_visible_height < potential_bridge_height + MAX_TILE_EXTENT_TOP) last_row = false;
			}

			if (tile_visible) {
				last_row = false;
				_vd.foundation_part = FOUNDATION_PART_NONE;
				_vd.foundation[0] = -1;
				_vd.foundation[1] = -1;
				_vd.last_foundation_child[0] = nullptr;
				_vd.last_foundation_child[1] = nullptr;

				bool no_ground_tiles = min_visible_height > 0;
				_tile_type_procs[tile_type]->draw_tile_proc(&tile_info, { min_visible_height, no_ground_tiles });
				if (tile_info.tile != INVALID_TILE && min_visible_height <= 0) {
					DrawTileSelection(&tile_info);
					DrawTileZoning(&tile_info);
				}
			}
		}
	}
}

/**
 * Add a string to draw in the viewport
 * @param dpi current viewport area
 * @param small_from Zoomlevel from when the small font should be used
 * @param sign sign position and dimension
 * @param string_normal String for normal and 2x zoom level
 * @param string_small String for 4x and 8x zoom level
 * @param string_small_shadow Shadow string for 4x and 8x zoom level; or #STR_NULL if no shadow
 * @param colour colour of the sign background; or INVALID_COLOUR if transparent
 */
void ViewportAddString(const DrawPixelInfo *dpi, ZoomLevel small_from, const ViewportSign *sign, StringID string_normal, StringID string_small, StringID string_small_shadow, uint64 params_1, uint64 params_2, Colours colour)
{
	bool small = dpi->zoom >= small_from;

	int left   = dpi->left;
	int top    = dpi->top;
	int right  = left + dpi->width;
	int bottom = top + dpi->height;

	int sign_height     = ScaleByZoom(VPSM_TOP + FONT_HEIGHT_NORMAL + VPSM_BOTTOM, dpi->zoom);
	int sign_half_width = ScaleByZoom((small ? sign->width_small : sign->width_normal) / 2, dpi->zoom);

	if (bottom < sign->top ||
			top   > sign->top + sign_height ||
			right < sign->center - sign_half_width ||
			left  > sign->center + sign_half_width) {
		return;
	}

	if (!small) {
		AddStringToDraw(sign->center - sign_half_width, sign->top, string_normal, params_1, params_2, colour, sign->width_normal);
	} else {
		int shadow_offset = 0;
		if (string_small_shadow != STR_NULL) {
			shadow_offset = 4;
			AddStringToDraw(sign->center - sign_half_width + shadow_offset, sign->top, string_small_shadow, params_1, params_2, INVALID_COLOUR, sign->width_small);
		}
		AddStringToDraw(sign->center - sign_half_width, sign->top - shadow_offset, string_small, params_1, params_2,
				colour, sign->width_small | 0x8000);
	}
}

static Rect ExpandRectWithViewportSignMargins(Rect r, ZoomLevel zoom)
{
	/* Pessimistically always use normal font, but also assume small font is never larger in either dimension */
	const int fh = FONT_HEIGHT_NORMAL;
	const int max_tw = _viewport_sign_maxwidth / 2 + 1;
	const int expand_y = ScaleByZoom(VPSM_TOP + fh + VPSM_BOTTOM, zoom);
	const int expand_x = ScaleByZoom(VPSM_LEFT + max_tw + VPSM_RIGHT, zoom);

	r.left -= expand_x;
	r.right += expand_x;
	r.top -= expand_y;
	r.bottom += expand_y;

	return r;
}

static void ViewportAddKdtreeSigns(DrawPixelInfo *dpi, bool towns_only)
{
	Rect search_rect{ dpi->left, dpi->top, dpi->left + dpi->width, dpi->top + dpi->height };
	search_rect = ExpandRectWithViewportSignMargins(search_rect, dpi->zoom);

	bool show_stations = HasBit(_display_opt, DO_SHOW_STATION_NAMES) && _game_mode != GM_MENU && !towns_only;
	bool show_waypoints = HasBit(_display_opt, DO_SHOW_WAYPOINT_NAMES) && _game_mode != GM_MENU && !towns_only;
	bool show_towns = HasBit(_display_opt, DO_SHOW_TOWN_NAMES) && _game_mode != GM_MENU;
	bool show_signs = HasBit(_display_opt, DO_SHOW_SIGNS) && !IsInvisibilitySet(TO_SIGNS) && !towns_only;
	bool show_competitors = HasBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS) && !towns_only;

	const BaseStation *st;
	const Sign *si;

	/* Collect all the items first and draw afterwards, to ensure layering */
	std::vector<const BaseStation *> stations;
	std::vector<const Town *> towns;
	std::vector<const Sign *> signs;

	_viewport_sign_kdtree.FindContained(search_rect.left, search_rect.top, search_rect.right, search_rect.bottom, [&](const ViewportSignKdtreeItem & item) {
		switch (item.type) {
			case ViewportSignKdtreeItem::VKI_STATION:
				if (!show_stations) break;
				st = BaseStation::Get(item.id.station);

				/* Don't draw if station is owned by another company and competitor station names are hidden. Stations owned by none are never ignored. */
				if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) break;

				stations.push_back(st);
				break;

			case ViewportSignKdtreeItem::VKI_WAYPOINT:
				if (!show_waypoints) break;
				st = BaseStation::Get(item.id.station);

				/* Don't draw if station is owned by another company and competitor station names are hidden. Stations owned by none are never ignored. */
				if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) break;

				stations.push_back(st);
				break;

			case ViewportSignKdtreeItem::VKI_TOWN:
				if (!show_towns) break;
				towns.push_back(Town::Get(item.id.town));
				break;

			case ViewportSignKdtreeItem::VKI_SIGN:
				if (!show_signs) break;
				si = Sign::Get(item.id.sign);

				/* Don't draw if sign is owned by another company and competitor signs should be hidden.
				* Note: It is intentional that also signs owned by OWNER_NONE are hidden. Bankrupt
				* companies can leave OWNER_NONE signs after them. */
				if (!show_competitors && si->IsCompetitorOwned()) break;

				signs.push_back(si);
				break;

			default:
				NOT_REACHED();
		}
	});

	/* Layering order (bottom to top): Town names, signs, stations */

	for (const auto *t : towns) {
		ViewportAddString(dpi, ZOOM_LVL_OUT_16X, &t->cache.sign,
			t->Label(), t->SmallLabel(), STR_VIEWPORT_TOWN_TINY_BLACK,
			t->index, t->cache.population);
	}

	for (const auto *si : signs) {
		ViewportAddString(dpi, ZOOM_LVL_OUT_16X, &si->sign,
			STR_WHITE_SIGN,
			(IsTransparencySet(TO_SIGNS) || si->owner == OWNER_DEITY) ? STR_VIEWPORT_SIGN_SMALL_WHITE : STR_VIEWPORT_SIGN_SMALL_BLACK, STR_NULL,
			si->index, 0, (si->owner == OWNER_NONE) ? COLOUR_GREY : (si->owner == OWNER_DEITY ? INVALID_COLOUR : _company_colours[si->owner]));
	}

	for (const auto *st : stations) {
		if (Station::IsExpected(st)) {
			/* Station */
			ViewportAddString(dpi, ZOOM_LVL_OUT_16X, &st->sign,
				STR_VIEWPORT_STATION, STR_VIEWPORT_STATION + 1, STR_NULL,
				st->index, st->facilities, (st->owner == OWNER_NONE || !st->IsInUse()) ? COLOUR_GREY : _company_colours[st->owner]);
		} else {
			/* Waypoint */
			ViewportAddString(dpi, ZOOM_LVL_OUT_16X, &st->sign,
				STR_VIEWPORT_WAYPOINT, STR_VIEWPORT_WAYPOINT + 1, STR_NULL,
				st->index, st->facilities, (st->owner == OWNER_NONE || !st->IsInUse()) ? COLOUR_GREY : _company_colours[st->owner]);
		}
	}
}


/**
 * Update the position of the viewport sign.
 * @param center the (preferred) center of the viewport sign
 * @param top    the new top of the sign
 * @param str    the string to show in the sign
 * @param str_small the string to show when zoomed out. STR_NULL means same as \a str
 */
void ViewportSign::UpdatePosition(ZoomLevel maxzoom, int center, int top, StringID str, StringID str_small)
{
	if (this->width_normal != 0) this->MarkDirty(maxzoom);

	this->top = top;

	char buffer[DRAW_STRING_BUFFER];

	GetString(buffer, str, lastof(buffer));
	this->width_normal = VPSM_LEFT + Align(GetStringBoundingBox(buffer).width, 2) + VPSM_RIGHT;
	this->center = center;

	/* zoomed out version */
	if (str_small != STR_NULL) {
		GetString(buffer, str_small, lastof(buffer));
	}
	this->width_small = VPSM_LEFT + Align(GetStringBoundingBox(buffer, FS_SMALL).width, 2) + VPSM_RIGHT;

	this->MarkDirty(maxzoom);
}

/**
 * Mark the sign dirty in all viewports.
 * @param maxzoom Maximum %ZoomLevel at which the text is visible.
 *
 * @ingroup dirty
 */
void ViewportSign::MarkDirty(ZoomLevel maxzoom) const
{
	if (maxzoom == ZOOM_LVL_END) return;

	Rect zoomlevels[ZOOM_LVL_COUNT];

	for (ZoomLevel zoom = ZOOM_LVL_BEGIN; zoom != ZOOM_LVL_END; zoom++) {
		const ZoomLevel small_from = (maxzoom == ZOOM_LVL_OUT_8X) ? ZOOM_LVL_OUT_8X : ZOOM_LVL_OUT_16X;
		const int width = (zoom >= small_from) ? this->width_small : this->width_normal ;
		zoomlevels[zoom].left   = this->center - ScaleByZoom(width / 2 + 1, zoom);
		zoomlevels[zoom].top    = this->top    - ScaleByZoom(1, zoom);
		zoomlevels[zoom].right  = this->center + ScaleByZoom(width / 2 + 1, zoom);
		zoomlevels[zoom].bottom = this->top    + ScaleByZoom(VPSM_TOP + FONT_HEIGHT_NORMAL + VPSM_BOTTOM + 1, zoom);
	}

	for (ViewPort *vp : _viewport_window_cache) {
		if (vp->zoom <= maxzoom) {
			Rect &zl = zoomlevels[vp->zoom];
			MarkViewportDirty(vp, zl.left, zl.top, zl.right, zl.bottom);
		}
	}
}

static void ViewportDrawTileSprites(const TileSpriteToDrawVector *tstdv)
{
	for (const TileSpriteToDraw &ts : *tstdv) {
		DrawSpriteViewport(ts.image, ts.pal, ts.x, ts.y, ts.sub);
	}
}

/** This fallback sprite checker always exists. */
static bool ViewportSortParentSpritesChecker()
{
	return true;
}

/** Sort parent sprites pointer array */
static void ViewportSortParentSprites(ParentSpriteToSortVector *psdv)
{
	auto psdvend = psdv->end();
	auto psd = psdv->begin();
	while (psd != psdvend) {
		ParentSpriteToDraw *ps = *psd;

		if (ps->IsComparisonDone()) {
			psd++;
			continue;
		}

		ps->SetComparisonDone(true);

		for (auto psd2 = psd + 1; psd2 != psdvend; psd2++) {
			ParentSpriteToDraw *ps2 = *psd2;

			if (ps2->IsComparisonDone()) continue;

			/* Decide which comparator to use, based on whether the bounding
			 * boxes overlap
			 */
			if (ps->xmax >= ps2->xmin && ps->xmin <= ps2->xmax && // overlap in X?
					ps->ymax >= ps2->ymin && ps->ymin <= ps2->ymax && // overlap in Y?
					ps->zmax >= ps2->zmin && ps->zmin <= ps2->zmax) { // overlap in Z?
				/* Use X+Y+Z as the sorting order, so sprites closer to the bottom of
				 * the screen and with higher Z elevation, are drawn in front.
				 * Here X,Y,Z are the coordinates of the "center of mass" of the sprite,
				 * i.e. X=(left+right)/2, etc.
				 * However, since we only care about order, don't actually divide / 2
				 */
				if (ps->xmin + ps->xmax + ps->ymin + ps->ymax + ps->zmin + ps->zmax <=
						ps2->xmin + ps2->xmax + ps2->ymin + ps2->ymax + ps2->zmin + ps2->zmax) {
					continue;
				}
			} else {
				/* We only change the order, if it is definite.
				 * I.e. every single order of X, Y, Z says ps2 is behind ps or they overlap.
				 * That is: If one partial order says ps behind ps2, do not change the order.
				 */
				if (ps->xmax < ps2->xmin ||
						ps->ymax < ps2->ymin ||
						ps->zmax < ps2->zmin) {
					continue;
				}
			}

			/* Move ps2 in front of ps */
			ParentSpriteToDraw *temp = ps2;
			for (auto psd3 = psd2; psd3 > psd; psd3--) {
				*psd3 = *(psd3 - 1);
			}
			*psd = temp;
		}
	}
}

static void ViewportDrawParentSprites(const ParentSpriteToSortVector *psd, const ChildScreenSpriteToDrawVector *csstdv)
{
	for (const ParentSpriteToDraw *ps : *psd) {
		if (ps->image != SPR_EMPTY_BOUNDING_BOX) DrawSpriteViewport(ps->image, ps->pal, ps->x, ps->y, ps->sub);

		int child_idx = ps->first_child;
		while (child_idx >= 0) {
			const ChildScreenSpriteToDraw *cs = csstdv->data() + child_idx;
			child_idx = cs->next;
			int x = cs->x;
			int y = cs->y;
			if (cs->relative) {
				x += ps->left;
				y += ps->top;
			}
			DrawSpriteViewport(cs->image, cs->pal, x, y, cs->sub);
		}
	}
}

/**
 * Draws the bounding boxes of all ParentSprites
 * @param psd Array of ParentSprites
 */
static void ViewportDrawBoundingBoxes(const ParentSpriteToSortVector *psd)
{
	for (const ParentSpriteToDraw *ps : *psd) {
		Point pt1 = RemapCoords(ps->xmax + 1, ps->ymax + 1, ps->zmax + 1); // top front corner
		Point pt2 = RemapCoords(ps->xmin    , ps->ymax + 1, ps->zmax + 1); // top left corner
		Point pt3 = RemapCoords(ps->xmax + 1, ps->ymin    , ps->zmax + 1); // top right corner
		Point pt4 = RemapCoords(ps->xmax + 1, ps->ymax + 1, ps->zmin    ); // bottom front corner

		DrawBox(        pt1.x,         pt1.y,
		        pt2.x - pt1.x, pt2.y - pt1.y,
		        pt3.x - pt1.x, pt3.y - pt1.y,
		        pt4.x - pt1.x, pt4.y - pt1.y);
	}
}

static void ViewportMapStoreBridge(const ViewPort * const vp, const TileIndex tile)
{
	extern LegendAndColour _legend_land_owners[NUM_NO_COMPANY_ENTRIES + MAX_COMPANIES + 1];
	extern uint _company_to_list_pos[MAX_COMPANIES];

	/* No need to bother for hidden things */
	if (!_settings_client.gui.show_bridges_on_map) return;
	const Owner o = GetTileOwner(tile);
	if (o < MAX_COMPANIES && !_legend_land_owners[_company_to_list_pos[o]].show_on_map) return;

	switch (GetTunnelBridgeDirection(tile)) {
		case DIAGDIR_NE: {
			/* X axis: tile at higher coordinate, facing towards lower coordinate */
			auto iter = _vd.bridge_to_map_x.lower_bound(tile);
			if (iter != _vd.bridge_to_map_x.begin()) {
				auto prev = iter;
				--prev;
				if (prev->second == tile) return;
			}
			_vd.bridge_to_map_x.insert(iter, std::make_pair(GetOtherTunnelBridgeEnd(tile), tile));
			break;
		}

		case DIAGDIR_NW: {
			/* Y axis: tile at higher coordinate, facing towards lower coordinate */
			auto iter = _vd.bridge_to_map_y.lower_bound(tile);
			if (iter != _vd.bridge_to_map_y.begin()) {
				auto prev = iter;
				--prev;
				if (prev->second == tile) return;
			}
			_vd.bridge_to_map_y.insert(iter, std::make_pair(GetOtherTunnelBridgeEnd(tile), tile));
			break;
		}

		case DIAGDIR_SW: {
			/* X axis: tile at lower coordinate, facing towards higher coordinate */
			auto iter = _vd.bridge_to_map_x.lower_bound(tile);
			if (iter != _vd.bridge_to_map_x.end() && iter->first == tile) return;
			_vd.bridge_to_map_x.insert(iter, std::make_pair(tile, GetOtherTunnelBridgeEnd(tile)));
			break;
		}

		case DIAGDIR_SE: {
			/* Y axis: tile at lower coordinate, facing towards higher coordinate */
			auto iter = _vd.bridge_to_map_y.lower_bound(tile);
			if (iter != _vd.bridge_to_map_y.end() && iter->first == tile) return;
			_vd.bridge_to_map_y.insert(iter, std::make_pair(tile, GetOtherTunnelBridgeEnd(tile)));
			break;
		}

		default:
			NOT_REACHED();
	}
}

void ViewportMapStoreTunnel(const TileIndex tile, const TileIndex tile_south, const int tunnel_z, const bool insert_sorted)
{
	extern LegendAndColour _legend_land_owners[NUM_NO_COMPANY_ENTRIES + MAX_COMPANIES + 1];
	extern uint _company_to_list_pos[MAX_COMPANIES];

	/* No need to bother for hidden things */
	if (!_settings_client.gui.show_tunnels_on_map) return;
	const Owner o = GetTileOwner(tile);
	if (o < MAX_COMPANIES && !_legend_land_owners[_company_to_list_pos[o]].show_on_map) return;

	const Axis axis = (TileX(tile) == TileX(tile_south)) ? AXIS_Y : AXIS_X;
	const Point viewport_pt = RemapCoords(TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE, tunnel_z);
	int y_intercept;
	if (axis == AXIS_X) {
		/* NE to SW */
		y_intercept = viewport_pt.y + (viewport_pt.x / 2);
	} else {
		/* NW to SE */
		y_intercept = viewport_pt.y - (viewport_pt.x / 2);
	}
	TunnelToMapStorage &storage = (axis == AXIS_X) ? _vd.tunnel_to_map_x : _vd.tunnel_to_map_y;
	TunnelToMap *tbtm;
	if (insert_sorted) {
		auto iter = std::upper_bound(storage.tunnels.begin(), storage.tunnels.end(), y_intercept, [](int a, const TunnelToMap &b) -> bool {
			return a < b.y_intercept;
		});
		tbtm = &(*(storage.tunnels.emplace(iter)));
	} else {
		storage.tunnels.emplace_back();
		tbtm = &(storage.tunnels.back());
	}

	/* ensure deterministic ordering, to avoid render flicker */
	tbtm->tb.from_tile = tile;
	tbtm->tb.to_tile = tile_south;
	tbtm->y_intercept = y_intercept;
	tbtm->tunnel_z = tunnel_z;
}

void ViewportMapClearTunnelCache()
{
	_vd.tunnel_to_map_x.tunnels.clear();
	_vd.tunnel_to_map_y.tunnels.clear();
}

void ViewportMapInvalidateTunnelCacheByTile(const TileIndex tile, const Axis axis)
{
	if (!_settings_client.gui.show_tunnels_on_map) return;
	std::vector<TunnelToMap> &tbtmv = (axis == AXIS_X) ? _vd.tunnel_to_map_x.tunnels : _vd.tunnel_to_map_y.tunnels;
	for (auto tbtm = tbtmv.begin(); tbtm != tbtmv.end(); tbtm++) {
		if (tbtm->tb.from_tile == tile) {
			tbtmv.erase(tbtm);
			return;
		}
	}
}

void ViewportMapBuildTunnelCache()
{
	ViewportMapClearTunnelCache();
	if (_settings_client.gui.show_tunnels_on_map) {
		for (Tunnel *tunnel : Tunnel::Iterate()) {
			ViewportMapStoreTunnel(tunnel->tile_n, tunnel->tile_s, tunnel->height, false);
		}
		auto sorter = [](const TunnelToMap &a, const TunnelToMap &b) -> bool {
			return a.y_intercept < b.y_intercept;
		};
		std::sort(_vd.tunnel_to_map_x.tunnels.begin(), _vd.tunnel_to_map_x.tunnels.end(), sorter);
		std::sort(_vd.tunnel_to_map_y.tunnels.begin(), _vd.tunnel_to_map_y.tunnels.end(), sorter);
	}
}

/**
 * Draw/colour the blocks that have been redrawn.
 */
static void ViewportDrawDirtyBlocks()
{
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	const DrawPixelInfo *dpi = _cur_dpi;
	void *dst;
	int right =  UnScaleByZoom(dpi->width,  dpi->zoom);
	int bottom = UnScaleByZoom(dpi->height, dpi->zoom);

	int colour = _string_colourmap[_dirty_block_colour & 0xF];

	dst = dpi->dst_ptr;

	byte bo = UnScaleByZoom(dpi->left + dpi->top, dpi->zoom) & 1;
	do {
		for (int i = (bo ^= 1); i < right; i += 2) blitter->SetPixel(dst, i, 0, (uint8)colour);
		dst = blitter->MoveTo(dst, 0, 1);
	} while (--bottom > 0);
}

static void ViewportDrawStrings(ZoomLevel zoom, const StringSpriteToDrawVector *sstdv)
{
	for (const StringSpriteToDraw &ss : *sstdv) {
		TextColour colour = TC_BLACK;
		bool small = HasBit(ss.width, 15);
		int w = GB(ss.width, 0, 15);
		int x = UnScaleByZoom(ss.x, zoom);
		int y = UnScaleByZoom(ss.y, zoom);
		int h = VPSM_TOP + (small ? FONT_HEIGHT_SMALL : FONT_HEIGHT_NORMAL) + VPSM_BOTTOM;

		SetDParam(0, ss.params[0]);
		SetDParam(1, ss.params[1]);

		if (ss.colour != INVALID_COLOUR) {
			/* Do not draw signs nor station names if they are set invisible */
			if (IsInvisibilitySet(TO_SIGNS) && ss.string != STR_WHITE_SIGN) continue;

			if (IsTransparencySet(TO_SIGNS) && ss.string != STR_WHITE_SIGN) {
				/* Don't draw the rectangle.
				 * Real colours need the TC_IS_PALETTE_COLOUR flag.
				 * Otherwise colours from _string_colourmap are assumed. */
				colour = (TextColour)_colour_gradient[ss.colour][6] | TC_IS_PALETTE_COLOUR;
			} else {
				/* Draw the rectangle if 'transparent station signs' is off,
				 * or if we are drawing a general text sign (STR_WHITE_SIGN). */
				DrawFrameRect(
					x, y, x + w, y + h, ss.colour,
					IsTransparencySet(TO_SIGNS) ? FR_TRANSPARENT : FR_NONE
				);
			}
		}

		DrawString(x + VPSM_LEFT, x + w - 1 - VPSM_RIGHT, y + VPSM_TOP, ss.string, colour, SA_HOR_CENTER);
	}
}

static inline Vehicle *GetVehicleFromWindow(Window *w)
{
	if (w) {
		WindowClass wc = w->window_class;
		WindowNumber wn = w->window_number;

		if (wc == WC_DROPDOWN_MENU) GetParentWindowInfo(w, wc, wn);

		switch (wc) {
			case WC_VEHICLE_VIEW:
			case WC_VEHICLE_ORDERS:
			case WC_VEHICLE_TIMETABLE:
			case WC_VEHICLE_DETAILS:
			case WC_VEHICLE_REFIT:
			case WC_VEHICLE_CARGO_TYPE_LOAD_ORDERS:
			case WC_VEHICLE_CARGO_TYPE_UNLOAD_ORDERS:
				if (wn != INVALID_VEHICLE) return Vehicle::Get(wn);
				break;
			default:
				break;
		}
	}
	return nullptr;
}

static inline TileIndex GetLastValidOrderLocation(const Vehicle *veh)
{
	Order *order;
	TileIndex tmp, result = INVALID_TILE;
	FOR_VEHICLE_ORDERS(veh, order) {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
			case OT_GOTO_WAYPOINT:
			case OT_IMPLICIT:
			case OT_GOTO_DEPOT:
				tmp = order->GetLocation(veh, veh->type == VEH_AIRCRAFT);
				if (tmp != INVALID_TILE) result = tmp;
				break;
			default:
				break;
		}
	}
	return result;
}

static inline Order *GetFinalOrder(const Vehicle *veh, Order *order)
{
	// Use Floyd's cycle-finding algorithm to prevent endless loop
	// due to a cycle formed by confitional orders.
	auto cycle_check = order;

	while (order->IsType(OT_CONDITIONAL)) {
		order = veh->GetOrder(order->GetConditionSkipToOrder());

		if (cycle_check->IsType(OT_CONDITIONAL)) {
			cycle_check = veh->GetOrder(cycle_check->GetConditionSkipToOrder());

			if (cycle_check->IsType(OT_CONDITIONAL)) {
				cycle_check = veh->GetOrder(cycle_check->GetConditionSkipToOrder());
			}
		}

		bool cycle_detected = (order->IsType(OT_CONDITIONAL) && (order == cycle_check));

		if (cycle_detected) return nullptr;
	}

	return order;
}

static bool ViewportMapPrepareVehicleRoute(const Vehicle * const veh)
{
	if (!veh) return false;

	if (_vp_route_paths.size() == 0) {
		TileIndex from_tile = GetLastValidOrderLocation(veh);
		if (from_tile == INVALID_TILE) return false;

		Order *order;
		FOR_VEHICLE_ORDERS(veh, order) {
			Order *final_order = GetFinalOrder(veh, order);
			if (final_order == nullptr) continue;
			const TileIndex to_tile = final_order->GetLocation(veh, veh->type == VEH_AIRCRAFT);
			if (to_tile == INVALID_TILE) continue;

			DrawnPathRouteTileLine path = { from_tile, to_tile, (final_order == order) };
			if (path.from_tile > path.to_tile) std::swap(path.from_tile, path.to_tile);
			_vp_route_paths.push_back(path);

			const OrderType ot = order->GetType();
			if (ot == OT_GOTO_STATION || ot == OT_GOTO_DEPOT || ot == OT_GOTO_WAYPOINT || ot == OT_IMPLICIT) from_tile = to_tile;
		}
		// remove duplicate lines
		std::sort(_vp_route_paths.begin(), _vp_route_paths.end());
		_vp_route_paths.erase(std::unique(_vp_route_paths.begin(), _vp_route_paths.end()), _vp_route_paths.end());
	}
	return true;
}

/** Draw the route of a vehicle. */
static void ViewportMapDrawVehicleRoute(const ViewPort *vp)
{
	switch (_settings_client.gui.show_vehicle_route) {
		/* case 0: return; // No */
		case 1: { // Simple
			DrawPixelInfo *old_dpi = _cur_dpi;
			_cur_dpi = &_dpi_for_text;

			for (const auto &iter : _vp_route_paths) {
				const int from_tile_x = TileX(iter.from_tile) * TILE_SIZE + TILE_SIZE / 2;
				const int from_tile_y = TileY(iter.from_tile) * TILE_SIZE + TILE_SIZE / 2;
				Point from_pt = RemapCoords(from_tile_x, from_tile_y, 0);
				const int from_x = UnScaleByZoom(from_pt.x, vp->zoom);

				const int to_tile_x = TileX(iter.to_tile) * TILE_SIZE + TILE_SIZE / 2;
				const int to_tile_y = TileY(iter.to_tile) * TILE_SIZE + TILE_SIZE / 2;
				Point to_pt = RemapCoords(to_tile_x, to_tile_y, 0);
				const int to_x = UnScaleByZoom(to_pt.x, vp->zoom);

				if (from_x < _cur_dpi->left - 1 && to_x < _cur_dpi->left - 1) continue;
				if (from_x > _cur_dpi->left + _cur_dpi->width + 1 && to_x > _cur_dpi->left + _cur_dpi->width + 1) continue;

				from_pt.y -= GetSlopePixelZ(from_tile_x, from_tile_y) * ZOOM_LVL_BASE;
				to_pt.y -= GetSlopePixelZ(to_tile_x, to_tile_y) * ZOOM_LVL_BASE;
				const int from_y = UnScaleByZoom(from_pt.y, vp->zoom);
				const int to_y = UnScaleByZoom(to_pt.y, vp->zoom);

				int line_width = 3;
				if (_settings_client.gui.dash_level_of_route_lines == 0) {
					GfxDrawLine(from_x, from_y, to_x, to_y, PC_BLACK, 3, _settings_client.gui.dash_level_of_route_lines);
					line_width = 1;
				}
				GfxDrawLine(from_x, from_y, to_x, to_y, iter.order_match ? PC_WHITE : PC_YELLOW, line_width, _settings_client.gui.dash_level_of_route_lines);
			}

			_cur_dpi = old_dpi;
			break;
		}
	}
}

static inline void DrawRouteStep(const ViewPort * const vp, const TileIndex tile, const RankOrderTypeList list)
{
	if (tile == INVALID_TILE) return;
	const uint step_count = list.size() > max_rank_order_type_count ? 1 : list.size();
	const int x_pos = TileX(tile) * TILE_SIZE + TILE_SIZE / 2;
	const int y_pos = TileY(tile) * TILE_SIZE + TILE_SIZE / 2;
	Point pt = RemapCoords(x_pos, y_pos, 0);
	const int x = UnScaleByZoomLower(pt.x - _vd.dpi.left, _vd.dpi.zoom) - (_vp_route_step_width / 2);
	if (x >= _cur_dpi->width || (x + _vp_route_step_width) <= 0) return;
	pt.y -= GetSlopePixelZ(x_pos, y_pos) * ZOOM_LVL_BASE;
	const int char_height = GetCharacterHeight(FS_SMALL) + 1;
	const int rsth = _vp_route_step_height_top + (int) step_count * char_height + _vp_route_step_height_bottom;
	const int y = UnScaleByZoomLower(pt.y - _vd.dpi.top,  _vd.dpi.zoom) - rsth;
	if (y >= _cur_dpi->height || (y + rsth) <= 0) return;

	/* Draw the background. */
	DrawSprite(SPR_ROUTE_STEP_TOP, PAL_NONE, _cur_dpi->left + x, _cur_dpi->top + y);
	uint y2 = y + _vp_route_step_height_top;

	for (uint r = step_count; r != 0; r--, y2 += char_height) {
		DrawSprite(SPR_ROUTE_STEP_MIDDLE, PAL_NONE, _cur_dpi->left + x, _cur_dpi->top + y2, &_vp_route_step_subsprite);
	}

	DrawSprite(SPR_ROUTE_STEP_BOTTOM, PAL_NONE, _cur_dpi->left + x, _cur_dpi->top + y2);
	SpriteID s = SPR_ROUTE_STEP_BOTTOM_SHADOW;
	DrawSprite(SetBit(s, PALETTE_MODIFIER_TRANSPARENT), PALETTE_TO_TRANSPARENT, _cur_dpi->left + x, _cur_dpi->top + y2);

	/* Fill with the data. */
	DrawPixelInfo *old_dpi = _cur_dpi;
	y2 = y + _vp_route_step_height_top;
	_cur_dpi = &_dpi_for_text;

	if (list.size() > max_rank_order_type_count) {
		/* Write order overflow item */
		SetDParam(0, list.size());
		DrawString(_dpi_for_text.left + x, _dpi_for_text.left + x + _vp_route_step_width - 1, _dpi_for_text.top + y2,
				STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP_OVERFLOW, TC_FROMSTRING, SA_CENTER, false, FS_SMALL);
	} else {
		for (RankOrderTypeList::const_iterator cit = list.begin(); cit != list.end(); cit++, y2 += char_height) {
			bool ok = true;
			switch (cit->second) {
				case OT_GOTO_STATION:
					SetDParam(1, STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP_STATION);
					break;
				case OT_GOTO_DEPOT:
					SetDParam(1, STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP_DEPOT);
					break;
				case OT_GOTO_WAYPOINT:
					SetDParam(1, STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP_WAYPOINT);
					break;
				case OT_IMPLICIT:
					SetDParam(1, STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP_IMPLICIT);
					break;
				default: // OT_NOTHING OT_LOADING OT_LEAVESTATION OT_DUMMY OT_CONDITIONAL
					ok = false;
					break;
			}
			if (ok) {
				/* Write order's info */
				SetDParam(0, cit->first);
				DrawString(_dpi_for_text.left + x, _dpi_for_text.left + x + _vp_route_step_width - 1, _dpi_for_text.top + y2,
						STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP, TC_FROMSTRING, SA_CENTER, false, FS_SMALL);
			}
		}
	}
	_cur_dpi = old_dpi;
}

static bool ViewportPrepareVehicleRouteSteps(const Vehicle * const veh)
{
	if (!veh) return false;

	if (_vp_route_steps.size() == 0) {
		/* Prepare data. */
		Order *order;
		int order_rank = 0;
		FOR_VEHICLE_ORDERS(veh, order) {
			const TileIndex tile = order->GetLocation(veh, veh->type == VEH_AIRCRAFT);
			order_rank++;
			if (tile == INVALID_TILE) continue;
			_vp_route_steps[tile].push_back(std::pair<int, OrderType>(order_rank, order->GetType()));
		}
	}

	return true;
}

void ViewportPrepareVehicleRoute()
{
	if (!_settings_client.gui.show_vehicle_route_steps && !_settings_client.gui.show_vehicle_route) return;
	const Vehicle * const veh = GetVehicleFromWindow(_focused_window);
	if (_settings_client.gui.show_vehicle_route_steps && veh && ViewportPrepareVehicleRouteSteps(veh)) {
		if (_vp_route_steps != _vp_route_steps_last_mark_dirty) {
			for (RouteStepsMap::const_iterator cit = _vp_route_steps.begin(); cit != _vp_route_steps.end(); cit++) {
				MarkRouteStepDirty(cit);
			}
			_vp_route_steps_last_mark_dirty = _vp_route_steps;
		}
	}
	if (_settings_client.gui.show_vehicle_route) {
		if (!veh) {
			if (!_vp_route_paths.empty()) {
				// make sure we remove any leftover paths
				MarkRoutePathsDirty(_vp_route_paths);
				_vp_route_paths.clear();
				_vp_route_paths_last_mark_dirty.clear();
			}
			return;
		} else {
			if (ViewportMapPrepareVehicleRoute(veh)) {
				if (_vp_route_paths_last_mark_dirty != _vp_route_paths) {
					// make sure we're not drawing a partial path
					MarkRoutePathsDirty(_vp_route_paths);
					_vp_route_paths_last_mark_dirty = _vp_route_paths;
				}
			} else {
				if (!_vp_route_paths.empty()) {
					// make sure we remove any leftover paths
					MarkRoutePathsDirty(_vp_route_paths);
					_vp_route_paths.clear();
					_vp_route_paths_last_mark_dirty.clear();
				}
				return;
			}
		}
	}
}

/** Draw the route steps of a vehicle. */
static void ViewportDrawVehicleRouteSteps(const ViewPort * const vp)
{
	for (RouteStepsMap::const_iterator cit = _vp_route_steps.begin(); cit != _vp_route_steps.end(); cit++) {
		DrawRouteStep(vp, cit->first, cit->second);
	}
}

void ViewportDrawPlans(const ViewPort *vp)
{
	DrawPixelInfo *old_dpi = _cur_dpi;
	_cur_dpi = &_dpi_for_text;

	for (Plan *p : Plan::Iterate()) {
		if (!p->IsVisible()) continue;
		for (PlanLineVector::iterator it = p->lines.begin(); it != p->lines.end(); it++) {
			PlanLine *pl = *it;
			if (!pl->visible) continue;
			for (uint i = 1; i < pl->tiles.size(); i++) {
				const TileIndex from_tile = pl->tiles[i-1];
				const Point from_pt = RemapCoords2(TileX(from_tile) * TILE_SIZE + TILE_SIZE / 2, TileY(from_tile) * TILE_SIZE + TILE_SIZE / 2);
				const int from_x = UnScaleByZoom(from_pt.x, vp->zoom);
				const int from_y = UnScaleByZoom(from_pt.y, vp->zoom);

				const TileIndex to_tile = pl->tiles[i];
				const Point to_pt = RemapCoords2(TileX(to_tile) * TILE_SIZE + TILE_SIZE / 2, TileY(to_tile) * TILE_SIZE + TILE_SIZE / 2);
				const int to_x = UnScaleByZoom(to_pt.x, vp->zoom);
				const int to_y = UnScaleByZoom(to_pt.y, vp->zoom);

				GfxDrawLine(from_x, from_y, to_x, to_y, PC_BLACK, 3);
				if (pl->focused) {
					GfxDrawLine(from_x, from_y, to_x, to_y, PC_RED, 1);
				} else {
					GfxDrawLine(from_x, from_y, to_x, to_y, PC_WHITE, 1);
				}
			}
		}
	}

	if (_current_plan && _current_plan->temp_line->tiles.size() > 1) {
		for (uint i = 1; i < _current_plan->temp_line->tiles.size(); i++) {
			const TileIndex from_tile = _current_plan->temp_line->tiles[i-1];
			const Point from_pt = RemapCoords2(TileX(from_tile) * TILE_SIZE + TILE_SIZE / 2, TileY(from_tile) * TILE_SIZE + TILE_SIZE / 2);
			const int from_x = UnScaleByZoom(from_pt.x, vp->zoom);
			const int from_y = UnScaleByZoom(from_pt.y, vp->zoom);

			const TileIndex to_tile = _current_plan->temp_line->tiles[i];
			const Point to_pt = RemapCoords2(TileX(to_tile) * TILE_SIZE + TILE_SIZE / 2, TileY(to_tile) * TILE_SIZE + TILE_SIZE / 2);
			const int to_x = UnScaleByZoom(to_pt.x, vp->zoom);
			const int to_y = UnScaleByZoom(to_pt.y, vp->zoom);

			GfxDrawLine(from_x, from_y, to_x, to_y, PC_WHITE, 3, 1);
		}
	}

	_cur_dpi = old_dpi;
}

#define SLOPIFY_COLOUR(tile, height, vF, vW, vS, vE, vN, action) { \
	if (show_slope) { \
		const Slope slope = GetTileSlope((tile), (height)); \
		switch (slope) { \
			case SLOPE_FLAT: \
			case SLOPE_ELEVATED: \
				action (vF); break; \
			default: { \
				switch (slope & SLOPE_EW) { \
					case SLOPE_W: action (vW); break; \
					case SLOPE_E: action (vE); break; \
					default:      action (slope & SLOPE_S) ? (vS) : (vN); break; \
				} \
				break; \
			} \
		} \
	} else { \
		action (vF); \
	} \
}
#define RETURN_SLOPIFIED_COLOUR(tile, height, colour, colour_light, colour_dark) SLOPIFY_COLOUR(tile, height, colour, colour_light, colour_dark, colour_dark, colour_light, return)
#define ASSIGN_SLOPIFIED_COLOUR(tile, height, colour, colour_light, colour_dark, to_var) SLOPIFY_COLOUR(tile, height, colour, colour_light, colour_dark, colour_dark, colour_light, to_var =)
#define GET_SLOPE_INDEX(slope_index) SLOPIFY_COLOUR(tile, nullptr, 0, 1, 2, 3, 4, slope_index =)

#define COL8TO32(x) _cur_palette.palette[x].data
#define COLOUR_FROM_INDEX(x) ((const uint8 *)&(x))[colour_index]
#define IS32(x) (is_32bpp ? COL8TO32(x) : (x))

/* Variables containing Colour if 32bpp or palette index if 8bpp. */
uint32 _vp_map_vegetation_clear_colours[16][6][8]; ///< [Slope][ClearGround][Multi (see LoadClearGroundMainColours())]
uint32 _vp_map_vegetation_tree_colours[5][MAX_TREE_COUNT_BY_LANDSCAPE]; ///< [TreeGround][max of _tree_count_by_landscape]
uint32 _vp_map_water_colour[5]; ///< [Slope]

static inline uint ViewportMapGetColourIndexMulti(const TileIndex tile, const ClearGround cg)
{
	switch (cg) {
		case CLEAR_GRASS:
		case CLEAR_SNOW:
		case CLEAR_DESERT:
			return GetClearDensity(tile);
		case CLEAR_ROUGH:
			return GB(TileX(tile) ^ TileY(tile), 4, 3);
		case CLEAR_ROCKS:
			return TileHash(TileX(tile), TileY(tile)) & 1;
		case CLEAR_FIELDS:
			return GetFieldType(tile) & 7;
		default: NOT_REACHED();
	}
}

static const ClearGround _treeground_to_clearground[5] = {
	CLEAR_GRASS, // TREE_GROUND_GRASS
	CLEAR_ROUGH, // TREE_GROUND_ROUGH
	CLEAR_SNOW,  // TREE_GROUND_SNOW_DESERT, make it +1 if _settings_game.game_creation.landscape == LT_TROPIC
	CLEAR_GRASS, // TREE_GROUND_SHORE
	CLEAR_SNOW,  // TREE_GROUND_ROUGH_SNOW, make it +1 if _settings_game.game_creation.landscape == LT_TROPIC
};

template <bool is_32bpp, bool show_slope>
static inline uint32 ViewportMapGetColourVegetation(const TileIndex tile, TileType t, const uint colour_index)
{
	uint32 colour;
	switch (t) {
		case MP_CLEAR: {
			Slope slope = show_slope ? (Slope) (GetTileSlope(tile, nullptr) & 15) : SLOPE_FLAT;
			uint multi;
			ClearGround cg = GetClearGround(tile);
			if (cg == CLEAR_FIELDS && colour_index & 1) {
				cg = CLEAR_GRASS;
				multi = 1;
			} else multi = ViewportMapGetColourIndexMulti(tile, cg);
			return _vp_map_vegetation_clear_colours[slope][cg][multi];
		}

		case MP_INDUSTRY:
			colour = IsTileForestIndustry(tile) ? (colour_index & 1 ? PC_GREEN : 0x7B) : GREY_SCALE(3);
			break;

		case MP_TREES: {
			const TreeGround tg = GetTreeGround(tile);
			const uint td = GetTreeDensity(tile);
			if (IsTransparencySet(TO_TREES)) {
				ClearGround cg = _treeground_to_clearground[tg];
				if (cg == CLEAR_SNOW && _settings_game.game_creation.landscape == LT_TROPIC) cg = CLEAR_DESERT;
				Slope slope = show_slope ? (Slope) (GetTileSlope(tile, nullptr) & 15) : SLOPE_FLAT;
				uint32 ground_colour = _vp_map_vegetation_clear_colours[slope][cg][td];

				if (IsInvisibilitySet(TO_TREES)) {
					/* Like ground. */
					return ground_colour;
				}

				/* Take ground and make it darker. */
				if (is_32bpp) {
					return Blitter_32bppBase::MakeTransparent(ground_colour, 192, 256).data;
				} else {
					/* 8bpp transparent snow trees give blue. Definitely don't want that. Prefer grey. */
					if (cg == CLEAR_SNOW && td > 1) return GREY_SCALE(13 - GetTreeCount(tile));
					return _pal2trsp_remap_ptr[ground_colour];
				}
			} else {
				if (tg == TREE_GROUND_SNOW_DESERT || tg == TREE_GROUND_ROUGH_SNOW) {
					return _vp_map_vegetation_clear_colours[colour_index][_settings_game.game_creation.landscape == LT_TROPIC ? CLEAR_DESERT : CLEAR_SNOW][td];
				} else {
					const uint rnd = min(GetTreeCount(tile) ^ (((tile & 3) ^ (TileY(tile) & 3)) * td), MAX_TREE_COUNT_BY_LANDSCAPE - 1);
					return _vp_map_vegetation_tree_colours[tg][rnd];
				}
			}
		}

		case MP_WATER:
			if (is_32bpp) {
				uint slope_index = 0;
				if (IsTileType(tile, MP_WATER) && GetWaterTileType(tile) != WATER_TILE_COAST) GET_SLOPE_INDEX(slope_index);
				return _vp_map_water_colour[slope_index];
			}
			/* FALL THROUGH */

		default:
			colour = ApplyMask(MKCOLOUR_XXXX(GREY_SCALE(3)), &_smallmap_vehicles_andor[t]);
			colour = COLOUR_FROM_INDEX(colour);
			break;
	}

	if (is_32bpp) {
		return COL8TO32(colour);
	} else {
		if (show_slope) ASSIGN_SLOPIFIED_COLOUR(tile, nullptr, colour, _lighten_colour[colour], _darken_colour[colour], colour);
		return colour;
	}
}

template <bool is_32bpp, bool show_slope>
static inline uint32 ViewportMapGetColourIndustries(const TileIndex tile, const TileType t, const uint colour_index)
{
	extern LegendAndColour _legend_from_industries[NUM_INDUSTRYTYPES + 1];
	extern uint _industry_to_list_pos[NUM_INDUSTRYTYPES];
	extern bool _smallmap_show_heightmap;

	TileType t2 = t;
	if (t == MP_INDUSTRY) {
		/* If industry is allowed to be seen, use its colour on the map. */
		const IndustryType it = Industry::GetByTile(tile)->type;
		if (_legend_from_industries[_industry_to_list_pos[it]].show_on_map)
			return IS32(GetIndustrySpec(it)->map_colour);
		/* Otherwise, return the colour which will make it disappear. */
		t2 = IsTileOnWater(tile) ? MP_WATER : MP_CLEAR;
	}

	if (is_32bpp && t2 == MP_WATER) {
		uint slope_index = 0;
		if (t != MP_INDUSTRY && IsTileType(tile, MP_WATER) && GetWaterTileType(tile) != WATER_TILE_COAST) GET_SLOPE_INDEX(slope_index); ///< Ignore industry on water not shown on map.
		return _vp_map_water_colour[slope_index];
	}

	const int h = TileHeight(tile);
	const SmallMapColourScheme * const cs = &_heightmap_schemes[_settings_client.gui.smallmap_land_colour];
	const uint32 colours = ApplyMask(_smallmap_show_heightmap ? cs->height_colours[h] : cs->default_colour, &_smallmap_vehicles_andor[t2]);
	uint32 colour = COLOUR_FROM_INDEX(colours);

	if (show_slope) ASSIGN_SLOPIFIED_COLOUR(tile, nullptr, colour, _lighten_colour[colour], _darken_colour[colour], colour);

	return IS32(colour);
}

template <bool is_32bpp, bool show_slope>
static inline uint32 ViewportMapGetColourOwner(const TileIndex tile, TileType t, const uint colour_index)
{
	extern LegendAndColour _legend_land_owners[NUM_NO_COMPANY_ENTRIES + MAX_COMPANIES + 1];
	extern uint _company_to_list_pos[MAX_COMPANIES];

	switch (t) {
		case MP_INDUSTRY: return IS32(PC_DARK_GREY);
		case MP_HOUSE:    return IS32(colour_index & 1 ? PC_DARK_RED : GREY_SCALE(3));
		default:          break;
	}

	const Owner o = GetTileOwner(tile);
	if ((o < MAX_COMPANIES && !_legend_land_owners[_company_to_list_pos[o]].show_on_map) || o == OWNER_NONE || o == OWNER_WATER) {
		if (t == MP_WATER) {
			if (is_32bpp) {
				uint slope_index = 0;
				if (IsTileType(tile, MP_WATER) && GetWaterTileType(tile) != WATER_TILE_COAST) GET_SLOPE_INDEX(slope_index);
				return _vp_map_water_colour[slope_index];
			} else {
				return PC_WATER;
			}
		}

		const int h = TileHeight(tile);
		uint32 colour = COLOUR_FROM_INDEX(_heightmap_schemes[_settings_client.gui.smallmap_land_colour].height_colours[h]);
		if (show_slope) ASSIGN_SLOPIFIED_COLOUR(tile, nullptr, colour, _lighten_colour[colour], _darken_colour[colour], colour);
		return IS32(colour);

	} else if (o == OWNER_TOWN) {
		return IS32(t == MP_ROAD ? (colour_index & 1 ? PC_BLACK : GREY_SCALE(3)) : PC_DARK_RED);
	}

	/* Train stations are sometimes hard to spot.
	 * So we give the player a hint by mixing his colour with black. */
	uint32 colour = _legend_land_owners[_company_to_list_pos[o]].colour;
	if (t != MP_STATION) {
		if (show_slope) ASSIGN_SLOPIFIED_COLOUR(tile, nullptr, colour, _lighten_colour[colour], _darken_colour[colour], colour);
	} else {
		if (GetStationType(tile) == STATION_RAIL) colour = colour_index & 1 ? colour : PC_BLACK;
	}
	if (is_32bpp) return COL8TO32(colour);
	return colour;
}

static inline void ViewportMapStoreBridgeAboveTile(const ViewPort * const vp, const TileIndex tile)
{
	/* No need to bother for hidden things */
	if (!_settings_client.gui.show_bridges_on_map) return;

	if (GetBridgeAxis(tile) == AXIS_X) {
		auto iter = _vd.bridge_to_map_x.lower_bound(tile);
		if (iter != _vd.bridge_to_map_x.end() && iter->first < tile && iter->second > tile) return; /* already covered */
		_vd.bridge_to_map_x.insert(iter, std::make_pair(GetNorthernBridgeEnd(tile), GetSouthernBridgeEnd(tile)));
	} else {
		auto iter = _vd.bridge_to_map_y.lower_bound(tile);
		if (iter != _vd.bridge_to_map_y.end() && iter->first < tile && iter->second > tile) return; /* already covered */
		_vd.bridge_to_map_y.insert(iter, std::make_pair(GetNorthernBridgeEnd(tile), GetSouthernBridgeEnd(tile)));
	}
}

static inline TileIndex ViewportMapGetMostSignificantTileType(const ViewPort * const vp, const TileIndex from_tile, TileType * const tile_type)
{
	if (vp->zoom <= ZOOM_LVL_OUT_128X || !_settings_client.gui.viewport_map_scan_surroundings) {
		const TileType ttype = GetTileType(from_tile);
		/* Store bridges and tunnels. */
		if (ttype != MP_TUNNELBRIDGE) {
			*tile_type = ttype;
			if (IsBridgeAbove(from_tile)) ViewportMapStoreBridgeAboveTile(vp, from_tile);
		} else {
			if (IsBridge(from_tile)) {
				ViewportMapStoreBridge(vp, from_tile);
			}
			switch (GetTunnelBridgeTransportType(from_tile)) {
				case TRANSPORT_RAIL:  *tile_type = MP_RAILWAY; break;
				case TRANSPORT_ROAD:  *tile_type = MP_ROAD;    break;
				case TRANSPORT_WATER: *tile_type = MP_WATER;   break;
				default:              NOT_REACHED();           break;
			}
		}
		return from_tile;
	}

	const uint8 length = (vp->zoom - ZOOM_LVL_OUT_128X) * 2;
	TileArea tile_area = TileArea(from_tile, length, length);
	tile_area.ClampToMap();

	/* Find the most important tile of the area. */
	TileIndex result = from_tile;
	uint importance = 0;
	TILE_AREA_LOOP_WITH_PREFETCH(tile, tile_area) {
		const TileType ttype = GetTileType(tile);
		const uint tile_importance = _tiletype_importance[ttype];
		if (tile_importance > importance) {
			importance = tile_importance;
			result = tile;
		}
		if (ttype != MP_TUNNELBRIDGE && IsBridgeAbove(tile)) {
			ViewportMapStoreBridgeAboveTile(vp, tile);
		}
	}

	/* Store bridges and tunnels. */
	*tile_type = GetTileType(result);
	if (*tile_type == MP_TUNNELBRIDGE) {
		if (IsBridge(result)) {
			ViewportMapStoreBridge(vp, result);
		}
		switch (GetTunnelBridgeTransportType(result)) {
			case TRANSPORT_RAIL: *tile_type = MP_RAILWAY; break;
			case TRANSPORT_ROAD: *tile_type = MP_ROAD;    break;
			default:             *tile_type = MP_WATER;   break;
		}
	}

	return result;
}

/** Get the colour of a tile, can be 32bpp RGB or 8bpp palette index. */
template <bool is_32bpp, bool show_slope>
uint32 ViewportMapGetColour(const ViewPort * const vp, uint x, uint y, const uint colour_index)
{
	if (!(IsInsideMM(x, TILE_SIZE, MapMaxX() * TILE_SIZE - 1) &&
		  IsInsideMM(y, TILE_SIZE, MapMaxY() * TILE_SIZE - 1)))
		return 0;

	/* Very approximative but fast way to get the tile when taking Z into account. */
	const TileIndex tile_tmp = TileVirtXY(x, y);
	const uint z = TileHeight(tile_tmp) * 4;
	TileIndex tile = TileVirtXY(x + z, y + z);
	if (tile >= MapSize()) return 0;
	if (_settings_game.construction.freeform_edges) {
		/* tile_tmp and tile must be from the same side,
		 * otherwise it's an approximation erroneous case
		 * that leads to a graphic glitch below south west border.
		 */
		if (TileX(tile_tmp) > (MapSizeX() - (MapSizeX() / 8)))
			if ((TileX(tile_tmp) < (MapSizeX() / 2)) != (TileX(tile) < (MapSizeX() / 2)))
				return 0;
	}
	TileType tile_type = MP_VOID;
	tile = ViewportMapGetMostSignificantTileType(vp, tile, &tile_type);
	if (tile_type == MP_VOID) return 0;

	/* Return the colours. */
	switch (vp->map_type) {
		default:              return ViewportMapGetColourOwner<is_32bpp, show_slope>(tile, tile_type, colour_index);
		case VPMT_INDUSTRY:   return ViewportMapGetColourIndustries<is_32bpp, show_slope>(tile, tile_type, colour_index);
		case VPMT_VEGETATION: return ViewportMapGetColourVegetation<is_32bpp, show_slope>(tile, tile_type, colour_index);
	}
}

/* Taken from http://stereopsis.com/doubleblend.html, PixelBlend() is faster than ComposeColourRGBANoCheck() */
static inline void PixelBlend(uint32 * const d, const uint32 s)
{
	const uint32 a     = (s >> 24) + 1;
	const uint32 dstrb = *d & 0xFF00FF;
	const uint32 dstg  = *d & 0xFF00;
	const uint32 srcrb = s & 0xFF00FF;
	const uint32 srcg  = s & 0xFF00;
	uint32 drb = srcrb - dstrb;
	uint32 dg  =  srcg - dstg;
	drb *= a;
	dg  *= a;
	drb >>= 8;
	dg  >>= 8;
	uint32 rb = (drb + dstrb) & 0xFF00FF;
	uint32 g  = (dg  + dstg) & 0xFF00;
	*d = rb | g;
}

/** Draw the bounding boxes of the scrolling viewport (right-clicked and dragged) */
static void ViewportMapDrawScrollingViewportBox(const ViewPort * const vp)
{
	if (_scrolling_viewport && _scrolling_viewport->viewport) {
		const ViewPort * const vp_scrolling = _scrolling_viewport->viewport;
		if (vp_scrolling->zoom < ZOOM_LVL_DRAW_MAP) {
			/* Check intersection of dpi and vp_scrolling */
			const int mask = ScaleByZoom(-1, vp->zoom);
			const int vp_scrolling_virtual_top_mask = vp_scrolling->virtual_top & mask;
			const int vp_scrolling_virtual_bottom_mask = (vp_scrolling->virtual_top + vp_scrolling->virtual_height) & mask;
			const int t_inter = max(vp_scrolling_virtual_top_mask, _vd.dpi.top);
			const int b_inter = min(vp_scrolling_virtual_bottom_mask, _vd.dpi.top + _vd.dpi.height);
			if (t_inter < b_inter) {
				const int vp_scrolling_virtual_left_mask = vp_scrolling->virtual_left & mask;
				const int vp_scrolling_virtual_right_mask = (vp_scrolling->virtual_left + vp_scrolling->virtual_width) & mask;
				const int l_inter = max(vp_scrolling_virtual_left_mask, _vd.dpi.left);
				const int r_inter = min(vp_scrolling_virtual_right_mask, _vd.dpi.left + _vd.dpi.width);
				if (l_inter < r_inter) {
					/* OK, so we can draw something that tells where the scrolling viewport is */
					Blitter * const blitter = BlitterFactory::GetCurrentBlitter();
					const int w_inter = UnScaleByZoom(r_inter - l_inter, vp->zoom);
					const int h_inter = UnScaleByZoom(b_inter - t_inter, vp->zoom);
					const int x = UnScaleByZoom(l_inter - _vd.dpi.left, vp->zoom);
					const int y = UnScaleByZoom(t_inter - _vd.dpi.top, vp->zoom);

					/* If asked, with 32bpp we can do some blending */
					if (_settings_client.gui.show_scrolling_viewport_on_map >= 2 && blitter->GetScreenDepth() == 32)
						for (int j = y; j < y + h_inter; j++)
							for (int i = x; i < x + w_inter; i++)
								PixelBlend((uint32*) blitter->MoveTo(_vd.dpi.dst_ptr, i, j), 0x40FCFCFC);

					/* Draw area contour */
					if (_settings_client.gui.show_scrolling_viewport_on_map != 2) {
						if (t_inter == vp_scrolling_virtual_top_mask)
							for (int i = x; i < x + w_inter; i += 2)
								blitter->SetPixel(_vd.dpi.dst_ptr, i, y, PC_WHITE);
						if (b_inter == vp_scrolling_virtual_bottom_mask)
							for (int i = x; i < x + w_inter; i += 2)
								blitter->SetPixel(_vd.dpi.dst_ptr, i, y + h_inter, PC_WHITE);
						if (l_inter == vp_scrolling_virtual_left_mask)
							for (int j = y; j < y + h_inter; j += 2)
								blitter->SetPixel(_vd.dpi.dst_ptr, x, j, PC_WHITE);
						if (r_inter == vp_scrolling_virtual_right_mask)
							for (int j = y; j < y + h_inter; j += 2)
								blitter->SetPixel(_vd.dpi.dst_ptr, x + w_inter, j, PC_WHITE);
					}
				}
			}
		}
	}
}

uint32 *_vp_map_line; ///< Buffer for drawing the map of a viewport.

static void ViewportMapDrawBridgeTunnel(const ViewPort * const vp, const TunnelBridgeToMap * const tbtm, const int z,
		const bool is_tunnel, const int w, const int h, Blitter * const blitter)
{
	extern LegendAndColour _legend_land_owners[NUM_NO_COMPANY_ENTRIES + MAX_COMPANIES + 1];
	extern uint _company_to_list_pos[MAX_COMPANIES];

	TileIndex tile = tbtm->from_tile;
	const Owner o = GetTileOwner(tile);
	if (o < MAX_COMPANIES && !_legend_land_owners[_company_to_list_pos[o]].show_on_map) return;

	uint8 colour;
	if (vp->map_type == VPMT_OWNER && _settings_client.gui.use_owner_colour_for_tunnelbridge && o < MAX_COMPANIES) {
		colour = _legend_land_owners[_company_to_list_pos[o]].colour;
		colour = is_tunnel ? _darken_colour[colour] : _lighten_colour[colour];
	} else {
		colour = is_tunnel ? PC_BLACK : PC_VERY_LIGHT_YELLOW;
	}

	TileIndexDiff delta = TileOffsByDiagDir(GetTunnelBridgeDirection(tile));
	for (; tile != tbtm->to_tile; tile += delta) { // For each tile
		const Point pt = RemapCoords(TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE, z);
		const int x = UnScaleByZoomLower(pt.x - _vd.dpi.left, _vd.dpi.zoom);
		if (IsInsideMM(x, 0, w)) {
			const int y = UnScaleByZoomLower(pt.y - _vd.dpi.top, _vd.dpi.zoom);
			if (IsInsideMM(y, 0, h)) blitter->SetPixel(_vd.dpi.dst_ptr, x, y, colour);
		}
	}
}

/** Draw the map on a viewport. */
template <bool is_32bpp, bool show_slope>
void ViewportMapDraw(const ViewPort * const vp)
{
	assert(vp != nullptr);
	Blitter * const blitter = BlitterFactory::GetCurrentBlitter();

	SmallMapWindow::RebuildColourIndexIfNecessary();

	/* Index of colour: _green_map_heights[] contains blocks of 4 colours, say ABCD
	 * For a XXXY colour block to render nicely, follow the model:
	 *   line 1: ABCDABCDABCD
	 *   line 2: CDABCDABCDAB
	 *   line 3: ABCDABCDABCD
	 * => colour_index_base's second bit is changed every new line.
	 */
	const  int sx = UnScaleByZoomLower(_vd.dpi.left, _vd.dpi.zoom);
	const  int sy = UnScaleByZoomLower(_vd.dpi.top, _vd.dpi.zoom);
	const uint line_padding = 2 * (sy & 1);
	uint       colour_index_base = (sx + line_padding) & 3;

	const  int incr_a = (1 << (vp->zoom - 2)) / ZOOM_LVL_BASE;
	const  int incr_b = (1 << (vp->zoom - 1)) / ZOOM_LVL_BASE;
	const  int a = (_vd.dpi.left >> 2) / ZOOM_LVL_BASE;
	int        b = (_vd.dpi.top >> 1) / ZOOM_LVL_BASE;
	const  int w = UnScaleByZoom(_vd.dpi.width, vp->zoom);
	const  int h = UnScaleByZoom(_vd.dpi.height, vp->zoom);
	int        j = 0;

	/* Render base map. */
	do { // For each line
		int i = w;
		uint colour_index = colour_index_base;
		colour_index_base ^= 2;
		uint32 *vp_map_line_ptr32 = _vp_map_line;
		uint8 *vp_map_line_ptr8 = (uint8*) _vp_map_line;
		int c = b - a;
		int d = b + a;
		do { // For each pixel of a line
			if (is_32bpp) {
				*vp_map_line_ptr32 = ViewportMapGetColour<is_32bpp, show_slope>(vp, c, d, colour_index);
				vp_map_line_ptr32++;
			} else {
				*vp_map_line_ptr8 = (uint8) ViewportMapGetColour<is_32bpp, show_slope>(vp, c, d, colour_index);
				vp_map_line_ptr8++;
			}
			colour_index = (colour_index + 1) & 3;
			c -= incr_a;
			d += incr_a;
		} while (--i);
		if (is_32bpp) {
			blitter->SetLine32(_vd.dpi.dst_ptr, 0, j, _vp_map_line, w);
		} else {
			blitter->SetLine(_vd.dpi.dst_ptr, 0, j, (uint8*) _vp_map_line, w);
		}
		b += incr_b;
	} while (++j < h);

	auto draw_tunnels = [&](const int y_intercept_min, const int y_intercept_max, const TunnelToMapStorage &storage) {
		auto iter = std::lower_bound(storage.tunnels.begin(), storage.tunnels.end(), y_intercept_min, [](const TunnelToMap &a, int b) -> bool {
			return a.y_intercept < b;
		});
		for (; iter != storage.tunnels.end() && iter->y_intercept <= y_intercept_max; ++iter) {
			const TunnelToMap &ttm = *iter;
			const int tunnel_z = ttm.tunnel_z * TILE_HEIGHT;
			const Point pt_from = RemapCoords(TileX(ttm.tb.from_tile) * TILE_SIZE, TileY(ttm.tb.from_tile) * TILE_SIZE, tunnel_z);
			const Point pt_to = RemapCoords(TileX(ttm.tb.to_tile) * TILE_SIZE, TileY(ttm.tb.to_tile) * TILE_SIZE, tunnel_z);

			/* check if tunnel is wholly outside redrawing area */
			const int x_from = UnScaleByZoomLower(pt_from.x - _vd.dpi.left, _vd.dpi.zoom);
			const int x_to = UnScaleByZoomLower(pt_to.x - _vd.dpi.left, _vd.dpi.zoom);
			if ((x_from < 0 && x_to < 0) || (x_from > w && x_to > w)) continue;
			const int y_from = UnScaleByZoomLower(pt_from.y - _vd.dpi.top, _vd.dpi.zoom);
			const int y_to = UnScaleByZoomLower(pt_to.y - _vd.dpi.top, _vd.dpi.zoom);
			if ((y_from < 0 && y_to < 0) || (y_from > h && y_to > h)) continue;

			ViewportMapDrawBridgeTunnel(vp, &ttm.tb, tunnel_z, true, w, h, blitter);
		}
	};

	/* Render tunnels */
	if (_settings_client.gui.show_tunnels_on_map && _vd.tunnel_to_map_x.tunnels.size() != 0) {
		const int y_intercept_min = _vd.dpi.top + (_vd.dpi.left / 2);
		const int y_intercept_max = _vd.dpi.top + _vd.dpi.height + ((_vd.dpi.left + _vd.dpi.width) / 2);
		draw_tunnels(y_intercept_min, y_intercept_max, _vd.tunnel_to_map_x);
	}
	if (_settings_client.gui.show_tunnels_on_map && _vd.tunnel_to_map_y.tunnels.size() != 0) {
		const int y_intercept_min = _vd.dpi.top - ((_vd.dpi.left + _vd.dpi.width) / 2);
		const int y_intercept_max = _vd.dpi.top + _vd.dpi.height - (_vd.dpi.left / 2);
		draw_tunnels(y_intercept_min, y_intercept_max, _vd.tunnel_to_map_y);
	}

	/* Render bridges */
	if (_settings_client.gui.show_bridges_on_map && _vd.bridge_to_map_x.size() != 0) {
		for (const auto &it : _vd.bridge_to_map_x) { // For each bridge
			TunnelBridgeToMap tbtm { it.first, it.second };
			ViewportMapDrawBridgeTunnel(vp, &tbtm, (GetBridgeHeight(tbtm.from_tile) - 1) * TILE_HEIGHT, false, w, h, blitter);
		}
	}
	if (_settings_client.gui.show_bridges_on_map && _vd.bridge_to_map_y.size() != 0) {
		for (const auto &it : _vd.bridge_to_map_y) { // For each bridge
			TunnelBridgeToMap tbtm { it.first, it.second };
			ViewportMapDrawBridgeTunnel(vp, &tbtm, (GetBridgeHeight(tbtm.from_tile) - 1) * TILE_HEIGHT, false, w, h, blitter);
		}
	}
}

static void ViewportProcessParentSprites()
{
	if (_vd.parent_sprites_to_sort.size() > 60 && (_cur_dpi->width >= 256 || _cur_dpi->height >= 256) && !_draw_bounding_boxes && !HasBit(_viewport_debug_flags, VDF_DISABLE_DRAW_SPLIT)) {
		/* split drawing region */
		ParentSpriteToSortVector all_sprites = std::move(_vd.parent_sprites_to_sort);
		_vd.parent_sprites_to_sort.clear();
		void *saved_dst_ptr = _cur_dpi->dst_ptr;
		if (_cur_dpi->height > _cur_dpi->width) {
			/* vertical split: upper half */
			const int orig_height = _cur_dpi->height;
			const int orig_top = _cur_dpi->top;
			_cur_dpi->height = (orig_height / 2) & ScaleByZoom(-1, _cur_dpi->zoom);
			int split = _cur_dpi->top + _cur_dpi->height;
			for (ParentSpriteToDraw *psd : all_sprites) {
				if (psd->top < split) _vd.parent_sprites_to_sort.push_back(psd);
			}
			ViewportProcessParentSprites();
			_vd.parent_sprites_to_sort.clear();

			/* vertical split: lower half */
			_cur_dpi->dst_ptr = BlitterFactory::GetCurrentBlitter()->MoveTo(_cur_dpi->dst_ptr, 0, UnScaleByZoom(_cur_dpi->height, _cur_dpi->zoom));
			_cur_dpi->top = split;
			_cur_dpi->height = orig_height - _cur_dpi->height;

			for (ParentSpriteToDraw *psd : all_sprites) {
				psd->SetComparisonDone(false);
				if (psd->top + psd->height > _cur_dpi->top) {
					_vd.parent_sprites_to_sort.push_back(psd);
				}
			}
			ViewportProcessParentSprites();

			/* restore _cur_dpi */
			_cur_dpi->height = orig_height;
			_cur_dpi->top = orig_top;
		} else {
			/* horizontal split: left half */
			const int orig_width = _cur_dpi->width;
			const int orig_left = _cur_dpi->left;
			_cur_dpi->width = (orig_width / 2) & ScaleByZoom(-1, _cur_dpi->zoom);
			const int margin = UnScaleByZoom(128, _cur_dpi->zoom); // Half tile (1 column) margin either side of split
			const int split = _cur_dpi->left + _cur_dpi->width;
			for (ParentSpriteToDraw *psd : all_sprites) {
				if (psd->left < split + margin) _vd.parent_sprites_to_sort.push_back(psd);
			}
			ViewportProcessParentSprites();
			_vd.parent_sprites_to_sort.clear();

			/* horizontal split: right half */
			_cur_dpi->dst_ptr = BlitterFactory::GetCurrentBlitter()->MoveTo(_cur_dpi->dst_ptr, UnScaleByZoom(_cur_dpi->width, _cur_dpi->zoom), 0);
			_cur_dpi->left = split;
			_cur_dpi->width = orig_width - _cur_dpi->width;

			for (ParentSpriteToDraw *psd : all_sprites) {
				psd->SetComparisonDone(false);
				if (psd->left + psd->width > _cur_dpi->left - margin) {
					_vd.parent_sprites_to_sort.push_back(psd);
				}
			}
			ViewportProcessParentSprites();

			/* restore _cur_dpi */
			_cur_dpi->width = orig_width;
			_cur_dpi->left = orig_left;
		}
		_cur_dpi->dst_ptr = saved_dst_ptr;
	} else {
		_vp_sprite_sorter(&_vd.parent_sprites_to_sort);
		ViewportDrawParentSprites(&_vd.parent_sprites_to_sort, &_vd.child_screen_sprites_to_draw);

		if (_draw_dirty_blocks && HasBit(_viewport_debug_flags, VDF_DIRTY_BLOCK_PER_SPLIT)) {
			ViewportDrawDirtyBlocks();
			++_dirty_block_colour;
		}
	}
}

void ViewportDoDraw(ViewPort *vp, int left, int top, int right, int bottom)
{
	DrawPixelInfo *old_dpi = _cur_dpi;
	_cur_dpi = &_vd.dpi;

	_vd.dpi.zoom = vp->zoom;
	int mask = ScaleByZoom(-1, vp->zoom);

	_vd.combine_sprites = SPRITE_COMBINE_NONE;

	_vd.dpi.width = (right - left) & mask;
	_vd.dpi.height = (bottom - top) & mask;
	_vd.dpi.left = left & mask;
	_vd.dpi.top = top & mask;
	_vd.dpi.pitch = old_dpi->pitch;
	_vd.last_child = nullptr;

	int x = UnScaleByZoomLower(_vd.dpi.left - (vp->virtual_left & mask), vp->zoom) + vp->left;
	int y = UnScaleByZoomLower(_vd.dpi.top - (vp->virtual_top & mask), vp->zoom) + vp->top;

	_vd.dpi.dst_ptr = BlitterFactory::GetCurrentBlitter()->MoveTo(old_dpi->dst_ptr, x - old_dpi->left, y - old_dpi->top);

	_dpi_for_text        = _vd.dpi;
	_dpi_for_text.left   = UnScaleByZoom(_dpi_for_text.left,   _dpi_for_text.zoom);
	_dpi_for_text.top    = UnScaleByZoom(_dpi_for_text.top,    _dpi_for_text.zoom);
	_dpi_for_text.width  = UnScaleByZoom(_dpi_for_text.width,  _dpi_for_text.zoom);
	_dpi_for_text.height = UnScaleByZoom(_dpi_for_text.height, _dpi_for_text.zoom);
	_dpi_for_text.zoom   = ZOOM_LVL_NORMAL;

	if (vp->zoom >= ZOOM_LVL_DRAW_MAP) {
		/* Here the rendering is like smallmap. */
		if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 32) {
			if (_settings_client.gui.show_slopes_on_viewport_map) ViewportMapDraw<true, true>(vp);
			else ViewportMapDraw<true, false>(vp);
		} else {
			_pal2trsp_remap_ptr = IsTransparencySet(TO_TREES) ? GetNonSprite(GB(PALETTE_TO_TRANSPARENT, 0, PALETTE_WIDTH), ST_RECOLOUR) + 1 : nullptr;
			if (_settings_client.gui.show_slopes_on_viewport_map) ViewportMapDraw<false, true>(vp);
			else ViewportMapDraw<false, false>(vp);
		}
		ViewportMapDrawVehicles(&_vd.dpi, vp);
		if (_scrolling_viewport && _settings_client.gui.show_scrolling_viewport_on_map) ViewportMapDrawScrollingViewportBox(vp);
		if (vp->zoom < ZOOM_LVL_OUT_256X) ViewportAddKdtreeSigns(&_vd.dpi, true);
	} else {
		/* Classic rendering. */
		ViewportAddLandscape();
		ViewportAddVehicles(&_vd.dpi);

		ViewportAddKdtreeSigns(&_vd.dpi, false);

		DrawTextEffects(&_vd.dpi);

		if (_vd.tile_sprites_to_draw.size() != 0) ViewportDrawTileSprites(&_vd.tile_sprites_to_draw);

		for (auto &psd : _vd.parent_sprites_to_draw) {
			_vd.parent_sprites_to_sort.push_back(&psd);
		}

		ViewportProcessParentSprites();

		if (_draw_bounding_boxes) ViewportDrawBoundingBoxes(&_vd.parent_sprites_to_sort);
	}
	if (_draw_dirty_blocks && !(HasBit(_viewport_debug_flags, VDF_DIRTY_BLOCK_PER_SPLIT) && vp->zoom < ZOOM_LVL_DRAW_MAP)) {
		ViewportDrawDirtyBlocks();
		if (HasBit(_viewport_debug_flags, VDF_DIRTY_BLOCK_PER_DRAW)) ++_dirty_block_colour;
	}

	DrawPixelInfo dp = _vd.dpi;
	ZoomLevel zoom = _vd.dpi.zoom;
	dp.zoom = ZOOM_LVL_NORMAL;
	dp.width = UnScaleByZoom(dp.width, zoom);
	dp.height = UnScaleByZoom(dp.height, zoom);
	_cur_dpi = &dp;

	if (vp->overlay != nullptr && vp->overlay->GetCargoMask() != 0 && vp->overlay->GetCompanyMask() != 0) {
		/* translate to window coordinates */
		dp.left = x;
		dp.top = y;
		vp->overlay->Draw(&dp);
	}

	if (_settings_client.gui.show_vehicle_route) ViewportMapDrawVehicleRoute(vp);
	if (_vd.string_sprites_to_draw.size() != 0) {
		/* translate to world coordinates */
		dp.left = UnScaleByZoom(_vd.dpi.left, zoom);
		dp.top = UnScaleByZoom(_vd.dpi.top, zoom);
		ViewportDrawStrings(zoom, &_vd.string_sprites_to_draw);
	}
	if (_settings_client.gui.show_vehicle_route_steps) ViewportDrawVehicleRouteSteps(vp);
	ViewportDrawPlans(vp);

	_cur_dpi = old_dpi;

	_vd.bridge_to_map_x.clear();
	_vd.bridge_to_map_y.clear();
	_vd.string_sprites_to_draw.clear();
	_vd.tile_sprites_to_draw.clear();
	_vd.parent_sprites_to_draw.clear();
	_vd.parent_sprites_to_sort.clear();
	_vd.child_screen_sprites_to_draw.clear();
}

/**
 * Make sure we don't draw a too big area at a time.
 * If we do, the sprite sorter will run into major performance problems and the sprite memory may overflow.
 */
void ViewportDrawChk(ViewPort *vp, int left, int top, int right, int bottom)
{
	if ((vp->zoom < ZOOM_LVL_DRAW_MAP) && ((int64)ScaleByZoom(bottom - top, vp->zoom) * (int64)ScaleByZoom(right - left, vp->zoom) > (int64)(1000000 * ZOOM_LVL_BASE * ZOOM_LVL_BASE))) {
		if ((bottom - top) > (right - left)) {
			int t = (top + bottom) >> 1;
			ViewportDrawChk(vp, left, top, right, t);
			ViewportDrawChk(vp, left, t, right, bottom);
		} else {
			int t = (left + right) >> 1;
			ViewportDrawChk(vp, left, top, t, bottom);
			ViewportDrawChk(vp, t, top, right, bottom);
		}
	} else {
		ViewportDoDraw(vp,
			ScaleByZoom(left - vp->left, vp->zoom) + vp->virtual_left,
			ScaleByZoom(top - vp->top, vp->zoom) + vp->virtual_top,
			ScaleByZoom(right - vp->left, vp->zoom) + vp->virtual_left,
			ScaleByZoom(bottom - vp->top, vp->zoom) + vp->virtual_top
		);
	}
}

static inline void ViewportDraw(ViewPort *vp, int left, int top, int right, int bottom)
{
	if (right <= vp->left || bottom <= vp->top) return;

	if (left >= vp->left + vp->width) return;

	if (left < vp->left) left = vp->left;
	if (right > vp->left + vp->width) right = vp->left + vp->width;

	if (top >= vp->top + vp->height) return;

	if (top < vp->top) top = vp->top;
	if (bottom > vp->top + vp->height) bottom = vp->top + vp->height;

	vp->is_drawn = true;

	ViewportDrawChk(vp, left, top, right, bottom);
}

/**
 * Draw the viewport of this window.
 */
void Window::DrawViewport() const
{
	PerformanceAccumulator framerate(PFE_DRAWWORLD);

	DrawPixelInfo *dpi = _cur_dpi;

	dpi->left += this->left;
	dpi->top += this->top;

	ViewportDraw(this->viewport, dpi->left, dpi->top, dpi->left + dpi->width, dpi->top + dpi->height);

	dpi->left -= this->left;
	dpi->top -= this->top;
}

/**
 * Ensure that a given viewport has a valid scroll position.
 *
 * There must be a visible piece of the map in the center of the viewport.
 * If there isn't, the viewport will be scrolled to nearest such location.
 *
 * @param vp The viewport.
 * @param[in,out] scroll_x Viewport X scroll.
 * @param[in,out] scroll_y Viewport Y scroll.
 */
static inline void ClampViewportToMap(const ViewPort *vp, int *scroll_x, int *scroll_y)
{
	/* Centre of the viewport is hot spot. */
	Point pt = {
		*scroll_x + vp->virtual_width / 2,
		*scroll_y + vp->virtual_height / 2
	};

	/* Find nearest tile that is within borders of the map. */
	bool clamped;
	pt = InverseRemapCoords2(pt.x, pt.y, true, &clamped);

	if (clamped) {
		/* Convert back to viewport coordinates and remove centering. */
		pt = RemapCoords2(pt.x, pt.y);
		*scroll_x = pt.x - vp->virtual_width / 2;
		*scroll_y = pt.y - vp->virtual_height / 2;
	}
}

/**
 * Update the viewport position being displayed.
 * @param w %Window owning the viewport.
 */
void UpdateViewportPosition(Window *w)
{
	const ViewPort *vp = w->viewport;

	if (w->viewport->follow_vehicle != INVALID_VEHICLE) {
		const Vehicle *veh = Vehicle::Get(w->viewport->follow_vehicle);
		Point pt = MapXYZToViewport(vp, veh->x_pos, veh->y_pos, veh->z_pos);

		w->viewport->scrollpos_x = pt.x;
		w->viewport->scrollpos_y = pt.y;
		SetViewportPosition(w, pt.x, pt.y, false);
	} else {
		/* Ensure the destination location is within the map */
		ClampViewportToMap(vp, &w->viewport->dest_scrollpos_x, &w->viewport->dest_scrollpos_y);

		int delta_x = w->viewport->dest_scrollpos_x - w->viewport->scrollpos_x;
		int delta_y = w->viewport->dest_scrollpos_y - w->viewport->scrollpos_y;

		bool update_overlay = false;
		if (delta_x != 0 || delta_y != 0) {
			if (_settings_client.gui.smooth_scroll) {
				int max_scroll = ScaleByMapSize1D(512 * ZOOM_LVL_BASE);
				/* Not at our desired position yet... */
				w->viewport->scrollpos_x += Clamp(DivAwayFromZero(delta_x, 4), -max_scroll, max_scroll);
				w->viewport->scrollpos_y += Clamp(DivAwayFromZero(delta_y, 4), -max_scroll, max_scroll);
			} else {
				w->viewport->scrollpos_x = w->viewport->dest_scrollpos_x;
				w->viewport->scrollpos_y = w->viewport->dest_scrollpos_y;
			}
			update_overlay = (w->viewport->scrollpos_x == w->viewport->dest_scrollpos_x &&
								w->viewport->scrollpos_y == w->viewport->dest_scrollpos_y);
		}

		ClampViewportToMap(vp, &w->viewport->scrollpos_x, &w->viewport->scrollpos_y);

		if (_scrolling_viewport == w) UpdateActiveScrollingViewport(w);

		SetViewportPosition(w, w->viewport->scrollpos_x, w->viewport->scrollpos_y, update_overlay);
	}
}

void UpdateViewportSizeZoom(ViewPort *vp)
{
	vp->dirty_blocks_per_column = CeilDiv(vp->height, vp->GetDirtyBlockHeight());
	vp->dirty_blocks_per_row = CeilDiv(vp->width, vp->GetDirtyBlockWidth());
	uint size = vp->dirty_blocks_per_row * vp->dirty_blocks_per_column;
	vp->dirty_blocks.assign(size, false);
	UpdateViewportDirtyBlockLeftMargin(vp);
	if (vp->zoom >= ZOOM_LVL_DRAW_MAP) {
		memset(vp->map_draw_vehicles_cache.done_hash_bits, 0, sizeof(vp->map_draw_vehicles_cache.done_hash_bits));
		vp->map_draw_vehicles_cache.vehicle_pixels.assign(vp->width * vp->height, false);
	} else {
		vp->map_draw_vehicles_cache.vehicle_pixels.clear();
	}
}

void UpdateActiveScrollingViewport(Window *w)
{
	if (w && (!_settings_client.gui.show_scrolling_viewport_on_map || w->viewport->zoom >= ZOOM_LVL_DRAW_MAP)) w = nullptr;

	const bool bound_valid = (_scrolling_viewport_bound.left != _scrolling_viewport_bound.right);

	if (!w && !bound_valid) return;

	const int gap = ScaleByZoom(1, ZOOM_LVL_MAX);

	auto get_bounds = [&gap](const ViewportData *vp) -> Rect {
		int lr_low = vp->virtual_left;
		int lr_hi = vp->dest_scrollpos_x;
		if (lr_low > lr_hi) Swap(lr_low, lr_hi);
		int right = lr_hi + vp->virtual_width + gap;

		int tb_low = vp->virtual_top;
		int tb_hi = vp->scrollpos_y;
		if (tb_low > tb_hi) Swap(tb_low, tb_hi);
		int bottom = tb_hi + vp->virtual_height + gap;

		return { lr_low, tb_low, right, bottom };
	};

	if (w && !bound_valid) {
		const Rect bounds = get_bounds(w->viewport);
		MarkAllViewportMapsDirty(bounds.left, bounds.top, bounds.right, bounds.bottom);
		_scrolling_viewport_bound = bounds;
	} else if (!w && bound_valid) {
		const Rect &bounds = _scrolling_viewport_bound;
		MarkAllViewportMapsDirty(bounds.left, bounds.top, bounds.right, bounds.bottom);
		_scrolling_viewport_bound = { 0, 0, 0, 0 };
	} else {
		/* Calculate symmetric difference of two rectangles */
		const Rect a = get_bounds(w->viewport);
		const Rect &b = _scrolling_viewport_bound;
		if (a.left != b.left) MarkAllViewportMapsDirty(min(a.left, b.left) - gap, min(a.top, b.top) - gap, max(a.left, b.left) + gap, max(a.bottom, b.bottom) + gap);
		if (a.top != b.top) MarkAllViewportMapsDirty(min(a.left, b.left) - gap, min(a.top, b.top) - gap, max(a.right, b.right) + gap, max(a.top, b.top) + gap);
		if (a.right != b.right) MarkAllViewportMapsDirty(min(a.right, b.right) - (2 * gap), min(a.top, b.top) - gap, max(a.right, b.right) + gap, max(a.bottom, b.bottom) + gap);
		if (a.bottom != b.bottom) MarkAllViewportMapsDirty(min(a.left, b.left) - gap, min(a.bottom, b.bottom) - (2 * gap), max(a.right, b.right) + gap, max(a.bottom, b.bottom) + gap);
		_scrolling_viewport_bound = a;
	}
}

/**
 * Marks a viewport as dirty for repaint if it displays (a part of) the area the needs to be repainted.
 * @param vp     The viewport to mark as dirty
 * @param left   Left edge of area to repaint
 * @param top    Top edge of area to repaint
 * @param right  Right edge of area to repaint
 * @param bottom Bottom edge of area to repaint
 * @ingroup dirty
 */
static void MarkViewportDirty(ViewPort * const vp, int left, int top, int right, int bottom)
{
	/* Rounding wrt. zoom-out level */
	right  += (1 << vp->zoom) - 1;
	bottom += (1 << vp->zoom) - 1;

	right -= vp->virtual_left;
	if (right <= 0) return;
	right = min(right, vp->virtual_width);

	bottom -= vp->virtual_top;
	if (bottom <= 0) return;
	bottom = min(bottom, vp->virtual_height);

	left = max(0, left - vp->virtual_left);

	if (left >= vp->virtual_width) return;

	top = max(0, top - vp->virtual_top);

	if (top >= vp->virtual_height) return;

	uint x = max<int>(0, UnScaleByZoomLower(left, vp->zoom) - vp->dirty_block_left_margin) >> vp->GetDirtyBlockWidthShift();
	uint y = UnScaleByZoomLower(top, vp->zoom) >> vp->GetDirtyBlockHeightShift();
	uint w = (max<int>(0, UnScaleByZoomLower(right, vp->zoom) - 1 - vp->dirty_block_left_margin) >> vp->GetDirtyBlockWidthShift()) + 1 - x;
	uint h = ((UnScaleByZoom(bottom, vp->zoom) - 1) >> vp->GetDirtyBlockHeightShift()) + 1 - y;

	uint column_skip = vp->dirty_blocks_per_column - h;
	uint pos = (x * vp->dirty_blocks_per_column) + y;
	for (uint i = 0; i < w; i++) {
		for (uint j = 0; j < h; j++) {
			vp->dirty_blocks[pos] = true;
			pos++;
		}
		pos += column_skip;
	}
	vp->is_dirty = true;
}

/**
 * Mark all viewports that display an area as dirty (in need of repaint).
 * @param left   Left   edge of area to repaint. (viewport coordinates, that is wrt. #ZOOM_LVL_NORMAL)
 * @param top    Top    edge of area to repaint. (viewport coordinates, that is wrt. #ZOOM_LVL_NORMAL)
 * @param right  Right  edge of area to repaint. (viewport coordinates, that is wrt. #ZOOM_LVL_NORMAL)
 * @param bottom Bottom edge of area to repaint. (viewport coordinates, that is wrt. #ZOOM_LVL_NORMAL)
 * @param mark_dirty_if_zoomlevel_is_below To tell if an update is relevant or not (for example, animations in map mode are not)
 * @ingroup dirty
 */
void MarkAllViewportsDirty(int left, int top, int right, int bottom, const ZoomLevel mark_dirty_if_zoomlevel_is_below)
{
	for (ViewPort * const vp : _viewport_window_cache) {
		if (vp->zoom >= mark_dirty_if_zoomlevel_is_below) continue;
		MarkViewportDirty(vp, left, top, right, bottom);
	}
}

static void MarkRouteStepDirty(RouteStepsMap::const_iterator cit)
{
	const uint size = cit->second.size() > max_rank_order_type_count ? 1 : cit->second.size();
	MarkRouteStepDirty(cit->first, size);
}

static void MarkRouteStepDirty(const TileIndex tile, uint order_nr)
{
	assert(tile != INVALID_TILE);
	const Point pt = RemapCoords2(TileX(tile) * TILE_SIZE + TILE_SIZE / 2, TileY(tile) * TILE_SIZE + TILE_SIZE / 2);
	const int char_height = GetCharacterHeight(FS_SMALL) + 1;
	for (ViewPort * const vp : _viewport_window_cache) {
		const int half_width = ScaleByZoom((_vp_route_step_width / 2) + 1, vp->zoom);
		const int height = ScaleByZoom(_vp_route_step_height_top + char_height * order_nr + _vp_route_step_height_bottom, vp->zoom);
		MarkViewportDirty(vp, pt.x - half_width, pt.y - height, pt.x + half_width, pt.y);
	}
}

void MarkAllRouteStepsDirty(const Vehicle *veh)
{
	ViewportPrepareVehicleRouteSteps(veh);
	for (RouteStepsMap::const_iterator cit = _vp_route_steps.begin(); cit != _vp_route_steps.end(); cit++) {
		MarkRouteStepDirty(cit);
	}
	_vp_route_steps_last_mark_dirty.swap(_vp_route_steps);
	_vp_route_steps.clear();
}

/**
 * Mark all viewports in map mode that display an area as dirty (in need of repaint).
 * @param left   Left edge of area to repaint
 * @param top    Top edge of area to repaint
 * @param right  Right edge of area to repaint
 * @param bottom Bottom edge of area to repaint
 * @ingroup dirty
 */
void MarkAllViewportMapsDirty(int left, int top, int right, int bottom)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		ViewPort *vp = w->viewport;
		if (vp != nullptr && vp->zoom >= ZOOM_LVL_DRAW_MAP) {
			assert(vp->width != 0);
			MarkViewportDirty(vp, left, top, right, bottom);
		}
	}
}

void ConstrainAllViewportsZoom()
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (w->viewport == nullptr) continue;

		ZoomLevel zoom = static_cast<ZoomLevel>(Clamp(w->viewport->zoom, _settings_client.gui.zoom_min, _settings_client.gui.zoom_max));
		if (zoom != w->viewport->zoom) {
			while (w->viewport->zoom < zoom) DoZoomInOutWindow(ZOOM_OUT, w);
			while (w->viewport->zoom > zoom) DoZoomInOutWindow(ZOOM_IN, w);
		}
	}
}

/**
 * Mark a tile given by its index dirty for repaint.
 * @param tile The tile to mark dirty.
 * @param mark_dirty_if_zoomlevel_is_below To tell if an update is relevant or not (for example, animations in map mode are not).
 * @param bridge_level_offset Height of bridge on tile to also mark dirty. (Height level relative to north corner.)
 * @param tile_height_override Height of the tile (#TileHeight).
 * @ingroup dirty
 */
void MarkTileDirtyByTile(TileIndex tile, const ZoomLevel mark_dirty_if_zoomlevel_is_below, int bridge_level_offset, int tile_height_override)
{
	Point pt = RemapCoords(TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE, tile_height_override * TILE_HEIGHT);
	MarkAllViewportsDirty(
			pt.x - 31  * ZOOM_LVL_BASE,
			pt.y - 122 * ZOOM_LVL_BASE - ZOOM_LVL_BASE * TILE_HEIGHT * bridge_level_offset,
			pt.x - 31  * ZOOM_LVL_BASE + 67  * ZOOM_LVL_BASE,
			pt.y - 122 * ZOOM_LVL_BASE + 154 * ZOOM_LVL_BASE,
			mark_dirty_if_zoomlevel_is_below
	);
}

void MarkTileGroundDirtyByTile(TileIndex tile, const ZoomLevel mark_dirty_if_zoomlevel_is_below)
{
	int x = TileX(tile) * TILE_SIZE;
	int y = TileY(tile) * TILE_SIZE;
	Point top = RemapCoords(x, y, GetTileMaxPixelZ(tile));
	Point bot = RemapCoords(x + TILE_SIZE, y + TILE_SIZE, GetTilePixelZ(tile));
	MarkAllViewportsDirty(top.x - TILE_PIXELS * ZOOM_LVL_BASE, top.y - TILE_HEIGHT * ZOOM_LVL_BASE, top.x + TILE_PIXELS * ZOOM_LVL_BASE, bot.y);
}

void MarkTileLineDirty(const TileIndex from_tile, const TileIndex to_tile)
{
	assert(from_tile != INVALID_TILE);
	assert(to_tile != INVALID_TILE);

	const Point from_pt = RemapCoords2(TileX(from_tile) * TILE_SIZE + TILE_SIZE / 2, TileY(from_tile) * TILE_SIZE + TILE_SIZE / 2);
	const Point to_pt = RemapCoords2(TileX(to_tile) * TILE_SIZE + TILE_SIZE / 2, TileY(to_tile) * TILE_SIZE + TILE_SIZE / 2);

	const int block_radius = 20;

	int x1 = from_pt.x / block_radius;
	int y1 = from_pt.y / block_radius;
	const int x2 = to_pt.x / block_radius;
	const int y2 = to_pt.y / block_radius;

	/* http://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm#Simplification */
	const int dx = abs(x2 - x1);
	const int dy = abs(y2 - y1);
	const int sx = (x1 < x2) ? 1 : -1;
	const int sy = (y1 < y2) ? 1 : -1;
	int err = dx - dy;
	for (;;) {
		MarkAllViewportsDirty(
				(x1 - 1) * block_radius,
				(y1 - 1) * block_radius,
				(x1 + 1) * block_radius,
				(y1 + 1) * block_radius,
				ZOOM_LVL_END
		);
		if (x1 == x2 && y1 == y2) break;
		const int e2 = 2 * err;
		if (e2 > -dy) {
			err -= dy;
			x1 += sx;
		}
		if (e2 < dx) {
			err += dx;
			y1 += sy;
		}
	}
}

static void MarkRoutePathsDirty(const std::vector<DrawnPathRouteTileLine> &lines)
{
	for (std::vector<DrawnPathRouteTileLine>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
		MarkTileLineDirty(it->from_tile, it->to_tile);
	}
}

void MarkAllRoutePathsDirty(const Vehicle *veh)
{
	switch (_settings_client.gui.show_vehicle_route) {
		case 0: // No
			return;

		case 1: // Simple
			ViewportMapPrepareVehicleRoute(veh);
			break;
	}
	for (const auto &iter : _vp_route_paths) {
		MarkTileLineDirty(iter.from_tile, iter.to_tile);
	}
	_vp_route_paths_last_mark_dirty.swap(_vp_route_paths);
	_vp_route_paths.clear();
}

void CheckMarkDirtyFocusedRoutePaths(const Vehicle *veh)
{
	const Vehicle *focused_veh = GetVehicleFromWindow(_focused_window);
	if (focused_veh && veh == focused_veh) {
		MarkAllRoutePathsDirty(veh);
		MarkAllRouteStepsDirty(veh);
	}
}

/**
 * Marks the selected tiles as dirty.
 *
 * This function marks the selected tiles as dirty for repaint
 *
 * @ingroup dirty
 */
static void SetSelectionTilesDirty()
{
	int x_size = _thd.size.x;
	int y_size = _thd.size.y;

	if (!_thd.diagonal) { // Selecting in a straight rectangle (or a single square)
		int x_start = _thd.pos.x;
		int y_start = _thd.pos.y;

		if (_thd.outersize.x != 0 || _thd.outersize.y != 0) {
			x_size  += _thd.outersize.x;
			x_start += _thd.offs.x;
			y_size  += _thd.outersize.y;
			y_start += _thd.offs.y;
		}

		x_size -= TILE_SIZE;
		y_size -= TILE_SIZE;

		assert(x_size >= 0);
		assert(y_size >= 0);

		int x_end = Clamp(x_start + x_size, 0, MapSizeX() * TILE_SIZE - TILE_SIZE);
		int y_end = Clamp(y_start + y_size, 0, MapSizeY() * TILE_SIZE - TILE_SIZE);

		x_start = Clamp(x_start, 0, MapSizeX() * TILE_SIZE - TILE_SIZE);
		y_start = Clamp(y_start, 0, MapSizeY() * TILE_SIZE - TILE_SIZE);

		/* make sure everything is multiple of TILE_SIZE */
		assert((x_end | y_end | x_start | y_start) % TILE_SIZE == 0);

		/* How it works:
		 * Suppose we have to mark dirty rectangle of 3x4 tiles:
		 *   x
		 *  xxx
		 * xxxxx
		 *  xxxxx
		 *   xxx
		 *    x
		 * This algorithm marks dirty columns of tiles, so it is done in 3+4-1 steps:
		 * 1)  x     2)  x
		 *    xxx       Oxx
		 *   Oxxxx     xOxxx
		 *    xxxxx     Oxxxx
		 *     xxx       xxx
		 *      x         x
		 * And so forth...
		 */

		int top_x = x_end; // coordinates of top dirty tile
		int top_y = y_start;
		int bot_x = top_x; // coordinates of bottom dirty tile
		int bot_y = top_y;

		do {
			/* topmost dirty point */
			TileIndex top_tile = TileVirtXY(top_x, top_y);
			Point top = RemapCoords(top_x, top_y, GetTileMaxPixelZ(top_tile));

			/* bottommost point */
			TileIndex bottom_tile = TileVirtXY(bot_x, bot_y);
			Point bot = RemapCoords(bot_x + TILE_SIZE, bot_y + TILE_SIZE, GetTilePixelZ(bottom_tile)); // bottommost point

			/* the 'x' coordinate of 'top' and 'bot' is the same (and always in the same distance from tile middle),
			 * tile height/slope affects only the 'y' on-screen coordinate! */

			int l = top.x - TILE_PIXELS * ZOOM_LVL_BASE; // 'x' coordinate of left   side of the dirty rectangle
			int t = top.y;                               // 'y' coordinate of top    side of the dirty rectangle
			int r = top.x + TILE_PIXELS * ZOOM_LVL_BASE; // 'x' coordinate of right  side of the dirty rectangle
			int b = bot.y;                               // 'y' coordinate of bottom side of the dirty rectangle

			static const int OVERLAY_WIDTH = 4 * ZOOM_LVL_BASE; // part of selection sprites is drawn outside the selected area (in particular: terraforming)

			/* For halftile foundations on SLOPE_STEEP_S the sprite extents some more towards the top */
			MarkAllViewportsDirty(l - OVERLAY_WIDTH, t - OVERLAY_WIDTH - TILE_HEIGHT * ZOOM_LVL_BASE, r + OVERLAY_WIDTH, b + OVERLAY_WIDTH);

			/* haven't we reached the topmost tile yet? */
			if (top_x != x_start) {
				top_x -= TILE_SIZE;
			} else {
				top_y += TILE_SIZE;
			}

			/* the way the bottom tile changes is different when we reach the bottommost tile */
			if (bot_y != y_end) {
				bot_y += TILE_SIZE;
			} else {
				bot_x -= TILE_SIZE;
			}
		} while (bot_x >= top_x);
	} else { // Selecting in a 45 degrees rotated (diagonal) rectangle.
		/* a_size, b_size describe a rectangle with rotated coordinates */
		int a_size = x_size + y_size, b_size = x_size - y_size;

		int interval_a = a_size < 0 ? -(int)TILE_SIZE : (int)TILE_SIZE;
		int interval_b = b_size < 0 ? -(int)TILE_SIZE : (int)TILE_SIZE;

		for (int a = -interval_a; a != a_size + interval_a; a += interval_a) {
			for (int b = -interval_b; b != b_size + interval_b; b += interval_b) {
				uint x = (_thd.pos.x + (a + b) / 2) / TILE_SIZE;
				uint y = (_thd.pos.y + (a - b) / 2) / TILE_SIZE;

				if (x < MapMaxX() && y < MapMaxY()) {
					MarkTileDirtyByTile(TileXY(x, y));
				}
			}
		}
	}
}


void SetSelectionRed(bool b)
{
	_thd.make_square_red = b;
	SetSelectionTilesDirty();
}

/**
 * Test whether a sign is below the mouse
 * @param vp the clicked viewport
 * @param x X position of click
 * @param y Y position of click
 * @param sign the sign to check
 * @return true if the sign was hit
 */
static bool CheckClickOnViewportSign(const ViewPort *vp, int x, int y, const ViewportSign *sign)
{
	bool small = (vp->zoom >= ZOOM_LVL_OUT_16X);
	int sign_half_width = ScaleByZoom((small ? sign->width_small : sign->width_normal) / 2, vp->zoom);
	int sign_height = ScaleByZoom(VPSM_TOP + (small ? FONT_HEIGHT_SMALL : FONT_HEIGHT_NORMAL) + VPSM_BOTTOM, vp->zoom);

	return y >= sign->top && y < sign->top + sign_height &&
			x >= sign->center - sign_half_width && x < sign->center + sign_half_width;
}


/**
 * Check whether any viewport sign was clicked, and dispatch the click.
 * @param vp the clicked viewport
 * @param x X position of click
 * @param y Y position of click
 * @return true if the sign was hit
 */
static bool CheckClickOnViewportSign(const ViewPort *vp, int x, int y)
{
	if (_game_mode == GM_MENU) return false;

	x = ScaleByZoom(x - vp->left, vp->zoom) + vp->virtual_left;
	y = ScaleByZoom(y - vp->top, vp->zoom) + vp->virtual_top;

	Rect search_rect{ x - 1, y - 1, x + 1, y + 1 };
	search_rect = ExpandRectWithViewportSignMargins(search_rect, vp->zoom);

	bool show_stations = HasBit(_display_opt, DO_SHOW_STATION_NAMES) && !IsInvisibilitySet(TO_SIGNS);
	bool show_waypoints = HasBit(_display_opt, DO_SHOW_WAYPOINT_NAMES) && !IsInvisibilitySet(TO_SIGNS);
	bool show_towns = HasBit(_display_opt, DO_SHOW_TOWN_NAMES);
	bool show_signs = HasBit(_display_opt, DO_SHOW_SIGNS) && !IsInvisibilitySet(TO_SIGNS);
	bool show_competitors = HasBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS);

	/* Topmost of each type that was hit */
	BaseStation *st = nullptr, *last_st = nullptr;
	Town *t = nullptr, *last_t = nullptr;
	Sign *si = nullptr, *last_si = nullptr;

	/* See ViewportAddKdtreeSigns() for details on the search logic */
	_viewport_sign_kdtree.FindContained(search_rect.left, search_rect.top, search_rect.right, search_rect.bottom, [&](const ViewportSignKdtreeItem & item) {
		switch (item.type) {
			case ViewportSignKdtreeItem::VKI_STATION:
				if (!show_stations) break;
				st = BaseStation::Get(item.id.station);
				if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) break;
				if (CheckClickOnViewportSign(vp, x, y, &st->sign)) last_st = st;
				break;

			case ViewportSignKdtreeItem::VKI_WAYPOINT:
				if (!show_waypoints) break;
				st = BaseStation::Get(item.id.station);
				if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) break;
				if (CheckClickOnViewportSign(vp, x, y, &st->sign)) last_st = st;
				break;

			case ViewportSignKdtreeItem::VKI_TOWN:
				if (!show_towns) break;
				t = Town::Get(item.id.town);
				if (CheckClickOnViewportSign(vp, x, y, &t->cache.sign)) last_t = t;
				break;

			case ViewportSignKdtreeItem::VKI_SIGN:
				if (!show_signs) break;
				si = Sign::Get(item.id.sign);
				if (!show_competitors && _local_company != si->owner && si->owner != OWNER_DEITY) break;
				if (CheckClickOnViewportSign(vp, x, y, &si->sign)) last_si = si;
				break;

			default:
				NOT_REACHED();
		}
	});

	/* Select which hit to handle based on priority */
	if (last_st != nullptr) {
		if (Station::IsExpected(last_st)) {
			ShowStationViewWindow(last_st->index);
		} else {
			ShowWaypointWindow(Waypoint::From(last_st));
		}
		return true;
	} else if (last_t != nullptr) {
		ShowTownViewWindow(last_t->index);
		return true;
	} else if (last_si != nullptr) {
		HandleClickOnSign(last_si);
		return true;
	} else {
		return false;
	}
}


ViewportSignKdtreeItem ViewportSignKdtreeItem::MakeStation(StationID id)
{
	ViewportSignKdtreeItem item;
	item.type = VKI_STATION;
	item.id.station = id;

	const Station *st = Station::Get(id);
	assert(st->sign.kdtree_valid);
	item.center = st->sign.center;
	item.top = st->sign.top;

	/* Assume the sign can be a candidate for drawing, so measure its width */
	_viewport_sign_maxwidth = max<int>(_viewport_sign_maxwidth, st->sign.width_normal);

	return item;
}

ViewportSignKdtreeItem ViewportSignKdtreeItem::MakeWaypoint(StationID id)
{
	ViewportSignKdtreeItem item;
	item.type = VKI_WAYPOINT;
	item.id.station = id;

	const Waypoint *st = Waypoint::Get(id);
	assert(st->sign.kdtree_valid);
	item.center = st->sign.center;
	item.top = st->sign.top;

	/* Assume the sign can be a candidate for drawing, so measure its width */
	_viewport_sign_maxwidth = max<int>(_viewport_sign_maxwidth, st->sign.width_normal);

	return item;
}

ViewportSignKdtreeItem ViewportSignKdtreeItem::MakeTown(TownID id)
{
	ViewportSignKdtreeItem item;
	item.type = VKI_TOWN;
	item.id.town = id;

	const Town *town = Town::Get(id);
	assert(town->cache.sign.kdtree_valid);
	item.center = town->cache.sign.center;
	item.top = town->cache.sign.top;

	/* Assume the sign can be a candidate for drawing, so measure its width */
	_viewport_sign_maxwidth = max<int>(_viewport_sign_maxwidth, town->cache.sign.width_normal);

	return item;
}

ViewportSignKdtreeItem ViewportSignKdtreeItem::MakeSign(SignID id)
{
	ViewportSignKdtreeItem item;
	item.type = VKI_SIGN;
	item.id.sign = id;

	const Sign *sign = Sign::Get(id);
	assert(sign->sign.kdtree_valid);
	item.center = sign->sign.center;
	item.top = sign->sign.top;

	/* Assume the sign can be a candidate for drawing, so measure its width */
	_viewport_sign_maxwidth = max<int>(_viewport_sign_maxwidth, sign->sign.width_normal);

	return item;
}

void RebuildViewportKdtree()
{
	/* Reset biggest size sign seen */
	_viewport_sign_maxwidth = 0;

	if (_network_dedicated) {
		_viewport_sign_kdtree_valid = false;
		_viewport_sign_kdtree.Build<ViewportSignKdtreeItem*>(nullptr, nullptr);
		return;
	}

	_viewport_sign_kdtree_valid = true;

	std::vector<ViewportSignKdtreeItem> items;
	items.reserve(BaseStation::GetNumItems() + Town::GetNumItems() + Sign::GetNumItems());

	for (const Station *st : Station::Iterate()) {
		if (st->sign.kdtree_valid) items.push_back(ViewportSignKdtreeItem::MakeStation(st->index));
	}

	for (const Waypoint *wp : Waypoint::Iterate()) {
		if (wp->sign.kdtree_valid) items.push_back(ViewportSignKdtreeItem::MakeWaypoint(wp->index));
	}

	for (const Town *town : Town::Iterate()) {
		if (town->cache.sign.kdtree_valid) items.push_back(ViewportSignKdtreeItem::MakeTown(town->index));
	}

	for (const Sign *sign : Sign::Iterate()) {
		if (sign->sign.kdtree_valid) items.push_back(ViewportSignKdtreeItem::MakeSign(sign->index));
	}

	_viewport_sign_kdtree.Build(items.begin(), items.end());
}


static bool CheckClickOnLandscape(const ViewPort *vp, int x, int y)
{
	Point pt = TranslateXYToTileCoord(vp, x, y);

	_tile_fract_coords.x = pt.x & TILE_UNIT_MASK;
	_tile_fract_coords.y = pt.y & TILE_UNIT_MASK;

	if (pt.x != -1) return ClickTile(TileVirtXY(pt.x, pt.y));
	return true;
}

static void PlaceObject()
{
	Point pt;
	Window *w;

	pt = GetTileBelowCursor();
	if (pt.x == -1) return;

	if ((_thd.place_mode & HT_DRAG_MASK) == HT_POINT) {
		pt.x += TILE_SIZE / 2;
		pt.y += TILE_SIZE / 2;
	}

	_tile_fract_coords.x = pt.x & TILE_UNIT_MASK;
	_tile_fract_coords.y = pt.y & TILE_UNIT_MASK;

	w = _thd.GetCallbackWnd();
	if (w != nullptr) w->OnPlaceObject(pt, TileVirtXY(pt.x, pt.y));
}

bool HandleViewportDoubleClicked(Window *w, int x, int y)
{
	ViewPort *vp = w->viewport;
	if (vp->zoom < ZOOM_LVL_DRAW_MAP) return false;

	switch (_settings_client.gui.action_when_viewport_map_is_dblclicked) {
		case 0: // Do nothing
			return false;
		case 1: // Zoom in main viewport
			while (vp->zoom != ZOOM_LVL_VIEWPORT)
				ZoomInOrOutToCursorWindow(true, w);
			return true;
		case 2: // Open an extra viewport
			ShowExtraViewPortWindowForTileUnderCursor();
			return true;
		default:
			return false;
	}
}

bool HandleViewportClicked(const ViewPort *vp, int x, int y, bool double_click)
{
	/* No click in smallmap mode except for plan making. */
	if (vp->zoom >= ZOOM_LVL_DRAW_MAP && !(_thd.place_mode == HT_POINT && _thd.select_proc == DDSP_DRAW_PLANLINE)) return true;

	const Vehicle *v = CheckClickOnVehicle(vp, x, y);

	if (_thd.place_mode & HT_VEHICLE) {
		if (v != nullptr && VehicleClicked(v)) return true;
	}

	/* Vehicle placement mode already handled above. */
	if ((_thd.place_mode & HT_DRAG_MASK) != HT_NONE) {
		if (_thd.place_mode & HT_POLY) {
			/* In polyline mode double-clicking on a single white line, finishes current polyline.
			 * If however the user double-clicks on a line that has a white and a blue section,
			 * both lines (white and blue) will be constructed consecutively. */
			static bool stop_snap_on_double_click = false;
			if (double_click && stop_snap_on_double_click) {
				SetRailSnapMode(RSM_NO_SNAP);
				return true;
			}
			stop_snap_on_double_click = !(_thd.drawstyle & HT_LINE) || (_thd.dir2 == HT_DIR_END);
		}

		PlaceObject();
		return true;
	}

	if (CheckClickOnViewportSign(vp, x, y)) return true;
	bool result = CheckClickOnLandscape(vp, x, y);

	if (v != nullptr) {
		DEBUG(misc, 2, "Vehicle %d (index %d) at %p", v->unitnumber, v->index, v);
		if (IsCompanyBuildableVehicleType(v)) {
			v = v->First();
			WindowClass wc = _thd.GetCallbackWnd()->window_class;
			if (_ctrl_pressed && v->owner == _local_company) {
				StartStopVehicle(v, true);
			} else if (wc != WC_CREATE_TEMPLATE && wc != WC_TEMPLATEGUI_MAIN) {
				ShowVehicleViewWindow(v);
			}
		}
		return true;
	}
	return result;
}

void RebuildViewportOverlay(Window *w, bool incremental)
{
	if (w->viewport->overlay != nullptr &&
			w->viewport->overlay->GetCompanyMask() != 0 &&
			w->viewport->overlay->GetCargoMask() != 0) {
		w->viewport->overlay->RebuildCache(incremental);
		if (!incremental) w->SetDirty();
	}
}

bool IsViewportOverlayOutsideCachedRegion(Window *w)
{
	if (w->viewport->overlay != nullptr &&
			w->viewport->overlay->GetCompanyMask() != 0 &&
			w->viewport->overlay->GetCargoMask() != 0) {
		return !w->viewport->overlay->CacheStillValid();
	} else {
		return false;
	}
}

/**
 * Scrolls the viewport in a window to a given location.
 * @param x       Desired x location of the map to scroll to (world coordinate).
 * @param y       Desired y location of the map to scroll to (world coordinate).
 * @param z       Desired z location of the map to scroll to (world coordinate). Use \c -1 to scroll to the height of the map at the \a x, \a y location.
 * @param w       %Window containing the viewport.
 * @param instant Jump to the location instead of slowly moving to it.
 * @return Destination of the viewport was changed (to activate other actions when the viewport is already at the desired position).
 */
bool ScrollWindowTo(int x, int y, int z, Window *w, bool instant)
{
	/* The slope cannot be acquired outside of the map, so make sure we are always within the map. */
	if (z == -1) {
		if ( x >= 0 && x <= (int)MapSizeX() * (int)TILE_SIZE - 1
				&& y >= 0 && y <= (int)MapSizeY() * (int)TILE_SIZE - 1) {
			z = GetSlopePixelZ(x, y);
		} else {
			z = TileHeightOutsideMap(x / (int)TILE_SIZE, y / (int)TILE_SIZE);
		}
	}

	Point pt = MapXYZToViewport(w->viewport, x, y, z);
	w->viewport->follow_vehicle = INVALID_VEHICLE;

	if (w->viewport->dest_scrollpos_x == pt.x && w->viewport->dest_scrollpos_y == pt.y) return false;

	if (instant) {
		w->viewport->scrollpos_x = pt.x;
		w->viewport->scrollpos_y = pt.y;
		RebuildViewportOverlay(w, true);
	}

	w->viewport->dest_scrollpos_x = pt.x;
	w->viewport->dest_scrollpos_y = pt.y;
	return true;
}

/**
 * Scrolls the viewport in a window to a given location.
 * @param tile    Desired tile to center on.
 * @param w       %Window containing the viewport.
 * @param instant Jump to the location instead of slowly moving to it.
 * @return Destination of the viewport was changed (to activate other actions when the viewport is already at the desired position).
 */
bool ScrollWindowToTile(TileIndex tile, Window *w, bool instant)
{
	return ScrollWindowTo(TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE, -1, w, instant);
}

/**
 * Scrolls the viewport of the main window to a given location.
 * @param tile    Desired tile to center on.
 * @param instant Jump to the location instead of slowly moving to it.
 * @return Destination of the viewport was changed (to activate other actions when the viewport is already at the desired position).
 */
bool ScrollMainWindowToTile(TileIndex tile, bool instant)
{
	return ScrollMainWindowTo(TileX(tile) * TILE_SIZE + TILE_SIZE / 2, TileY(tile) * TILE_SIZE + TILE_SIZE / 2, -1, instant);
}

/**
 * Set a tile to display a red error square.
 * @param tile Tile that should show the red error square.
 */
void SetRedErrorSquare(TileIndex tile)
{
	TileIndex old;

	old = _thd.redsq;
	_thd.redsq = tile;

	if (tile != old) {
		if (tile != INVALID_TILE) MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
		if (old  != INVALID_TILE) MarkTileDirtyByTile(old, ZOOM_LVL_DRAW_MAP);
	}
}

/**
 * Highlight \a w by \a h tiles at the cursor.
 * @param w Width of the highlighted tiles rectangle.
 * @param h Height of the highlighted tiles rectangle.
 */
void SetTileSelectSize(int w, int h)
{
	_thd.new_size.x = w * TILE_SIZE;
	_thd.new_size.y = h * TILE_SIZE;
	_thd.new_outersize.x = 0;
	_thd.new_outersize.y = 0;
}

void SetTileSelectBigSize(int ox, int oy, int sx, int sy)
{
	_thd.new_offs.x = ox * TILE_SIZE;
	_thd.new_offs.y = oy * TILE_SIZE;
	_thd.new_outersize.x = sx * TILE_SIZE;
	_thd.new_outersize.y = sy * TILE_SIZE;
}

/** returns the best autorail highlight type from map coordinates */
static HighLightStyle GetAutorailHT(int x, int y)
{
	return HT_RAIL | _autorail_piece[x & TILE_UNIT_MASK][y & TILE_UNIT_MASK];
}

/**
 * Reset tile highlighting.
 */
void TileHighlightData::Reset()
{
	this->pos.x = 0;
	this->pos.y = 0;
	this->new_pos.x = 0;
	this->new_pos.y = 0;
}

/**
 * Is the user dragging a 'diagonal rectangle'?
 * @return User is dragging a rotated rectangle.
 */
bool TileHighlightData::IsDraggingDiagonal()
{
	return (this->place_mode & HT_DIAGONAL) != 0 && _ctrl_pressed && _left_button_down;
}

/**
 * Get the window that started the current highlighting.
 * @return The window that requested the current tile highlighting, or \c nullptr if not available.
 */
Window *TileHighlightData::GetCallbackWnd()
{
	return FindWindowById(this->window_class, this->window_number);
}

static HighLightStyle CalcPolyrailDrawstyle(Point pt, bool dragging);

static inline void CalcNewPolylineOutersize()
{
	/* use the 'outersize' to mark the second (blue) part of a polyline selection */
	if (_thd.dir2 < HT_DIR_END) {
		/* get bounds of the second part */
		int outer_x1 = _thd.selstart2.x & ~TILE_UNIT_MASK;
		int outer_y1 = _thd.selstart2.y & ~TILE_UNIT_MASK;
		int outer_x2 = _thd.selend2.x & ~TILE_UNIT_MASK;
		int outer_y2 = _thd.selend2.y & ~TILE_UNIT_MASK;
		if (outer_x1 > outer_x2) Swap(outer_x1, outer_x2);
		if (outer_y1 > outer_y2) Swap(outer_y1, outer_y2);
		/* include the first part */
		outer_x1 = min<int>(outer_x1, _thd.new_pos.x);
		outer_y1 = min<int>(outer_y1, _thd.new_pos.y);
		outer_x2 = max<int>(outer_x2, _thd.new_pos.x + _thd.new_size.x - TILE_SIZE);
		outer_y2 = max<int>(outer_y2, _thd.new_pos.y + _thd.new_size.y - TILE_SIZE);
		/* write new values */
		_thd.new_offs.x = outer_x1 - _thd.new_pos.x;
		_thd.new_offs.y = outer_y1 - _thd.new_pos.y;
		_thd.new_outersize.x = outer_x2 - outer_x1 + TILE_SIZE - _thd.new_size.x;
		_thd.new_outersize.y = outer_y2 - outer_y1 + TILE_SIZE - _thd.new_size.y;
	} else {
		_thd.new_offs.x = 0;
		_thd.new_offs.y = 0;
		_thd.new_outersize.x = 0;
		_thd.new_outersize.y = 0;
	}
}

/**
 * Updates tile highlighting for all cases.
 * Uses _thd.selstart and _thd.selend and _thd.place_mode (set elsewhere) to determine _thd.pos and _thd.size
 * Also drawstyle is determined. Uses _thd.new.* as a buffer and calls SetSelectionTilesDirty() twice,
 * Once for the old and once for the new selection.
 * _thd is TileHighlightData, found in viewport.h
 */
void UpdateTileSelection()
{
	int x1;
	int y1;

	if (_thd.freeze) return;

	HighLightStyle new_drawstyle = HT_NONE;
	bool new_diagonal = false;

	if ((_thd.place_mode & HT_DRAG_MASK) == HT_SPECIAL) {
		x1 = _thd.selend.x;
		y1 = _thd.selend.y;
		if (x1 != -1) {
			int x2 = _thd.selstart.x & ~TILE_UNIT_MASK;
			int y2 = _thd.selstart.y & ~TILE_UNIT_MASK;
			x1 &= ~TILE_UNIT_MASK;
			y1 &= ~TILE_UNIT_MASK;

			if (_thd.IsDraggingDiagonal()) {
				new_diagonal = true;
			} else {
				if (x1 >= x2) Swap(x1, x2);
				if (y1 >= y2) Swap(y1, y2);
			}
			_thd.new_pos.x = x1;
			_thd.new_pos.y = y1;
			_thd.new_size.x = x2 - x1;
			_thd.new_size.y = y2 - y1;
			if (!new_diagonal) {
				_thd.new_size.x += TILE_SIZE;
				_thd.new_size.y += TILE_SIZE;
			}
			new_drawstyle = _thd.next_drawstyle;
		}
	} else if ((_thd.place_mode & HT_DRAG_MASK) != HT_NONE) {
		Point pt = GetTileBelowCursor();
		x1 = pt.x;
		y1 = pt.y;
		if (x1 != -1) {
			switch (_thd.place_mode & HT_DRAG_MASK) {
				case HT_RECT:
					new_drawstyle = HT_RECT;
					break;
				case HT_POINT:
					new_drawstyle = HT_POINT;
					x1 += TILE_SIZE / 2;
					y1 += TILE_SIZE / 2;
					break;
				case HT_RAIL:
				case HT_LINE:
					/* HT_POLY */
					if (_thd.place_mode & HT_POLY) {
						RailSnapMode snap_mode = GetRailSnapMode();
						if (snap_mode == RSM_NO_SNAP ||
								(snap_mode == RSM_SNAP_TO_TILE && GetRailSnapTile() == TileVirtXY(pt.x, pt.y))) {
							new_drawstyle = GetAutorailHT(pt.x, pt.y);
							_thd.new_offs.x = 0;
							_thd.new_offs.y = 0;
							_thd.new_outersize.x = 0;
							_thd.new_outersize.y = 0;
							_thd.dir2 = HT_DIR_END;
						} else {
							new_drawstyle = CalcPolyrailDrawstyle(pt, false);
							if (new_drawstyle != HT_NONE) {
								x1 = _thd.selstart.x & ~TILE_UNIT_MASK;
								y1 = _thd.selstart.y & ~TILE_UNIT_MASK;
								int x2 = _thd.selend.x & ~TILE_UNIT_MASK;
								int y2 = _thd.selend.y & ~TILE_UNIT_MASK;
								if (x1 > x2) Swap(x1, x2);
								if (y1 > y2) Swap(y1, y2);
								_thd.new_pos.x = x1;
								_thd.new_pos.y = y1;
								_thd.new_size.x = x2 - x1 + TILE_SIZE;
								_thd.new_size.y = y2 - y1 + TILE_SIZE;
							}
						}
						break;
					}
					/* HT_RAIL */
					if (_thd.place_mode & HT_RAIL) {
						/* Draw one highlighted tile in any direction */
						new_drawstyle = GetAutorailHT(pt.x, pt.y);
						break;
					}
					/* HT_LINE */
					switch (_thd.place_mode & HT_DIR_MASK) {
						case HT_DIR_X: new_drawstyle = HT_LINE | HT_DIR_X; break;
						case HT_DIR_Y: new_drawstyle = HT_LINE | HT_DIR_Y; break;

						case HT_DIR_HU:
						case HT_DIR_HL:
							new_drawstyle = (pt.x & TILE_UNIT_MASK) + (pt.y & TILE_UNIT_MASK) <= TILE_SIZE ? HT_LINE | HT_DIR_HU : HT_LINE | HT_DIR_HL;
							break;

						case HT_DIR_VL:
						case HT_DIR_VR:
							new_drawstyle = (pt.x & TILE_UNIT_MASK) > (pt.y & TILE_UNIT_MASK) ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;
							break;

						default: NOT_REACHED();
					}
					_thd.selstart.x = x1 & ~TILE_UNIT_MASK;
					_thd.selstart.y = y1 & ~TILE_UNIT_MASK;
					_thd.selend.x = x1;
					_thd.selend.y = y1;
					break;
				default:
					NOT_REACHED();
			}
			_thd.new_pos.x = x1 & ~TILE_UNIT_MASK;
			_thd.new_pos.y = y1 & ~TILE_UNIT_MASK;
		}
	}

	if (new_drawstyle & HT_LINE) CalcNewPolylineOutersize();

	/* redraw selection */
	if (_thd.drawstyle != new_drawstyle ||
			_thd.pos.x != _thd.new_pos.x || _thd.pos.y != _thd.new_pos.y ||
			_thd.size.x != _thd.new_size.x || _thd.size.y != _thd.new_size.y ||
			_thd.offs.x != _thd.new_offs.x || _thd.offs.y != _thd.new_offs.y ||
			_thd.outersize.x != _thd.new_outersize.x ||
			_thd.outersize.y != _thd.new_outersize.y ||
			_thd.diagonal    != new_diagonal) {
		/* Clear the old tile selection? */
		if ((_thd.drawstyle & HT_DRAG_MASK) != HT_NONE) SetSelectionTilesDirty();

		_thd.drawstyle = new_drawstyle;
		_thd.pos = _thd.new_pos;
		_thd.size = _thd.new_size;
		_thd.offs = _thd.new_offs;
		_thd.outersize = _thd.new_outersize;
		_thd.diagonal = new_diagonal;
		_thd.dirty = 0xff;

		/* Draw the new tile selection? */
		if ((new_drawstyle & HT_DRAG_MASK) != HT_NONE) SetSelectionTilesDirty();
	}
}

/**
 * Displays the measurement tooltips when selecting multiple tiles
 * @param str String to be displayed
 * @param paramcount number of params to deal with
 * @param params (optional) up to 5 pieces of additional information that may be added to a tooltip
 * @param close_cond Condition for closing this tooltip.
 */
static inline void ShowMeasurementTooltips(StringID str, uint paramcount, const uint64 params[], TooltipCloseCondition close_cond = TCC_NONE)
{
	if (!_settings_client.gui.measure_tooltip) return;
	GuiShowTooltips(_thd.GetCallbackWnd(), str, paramcount, params, close_cond);
}

static void HideMeasurementTooltips()
{
	DeleteWindowById(WC_TOOLTIPS, 0);
}

/** highlighting tiles while only going over them with the mouse */
void VpStartPlaceSizing(TileIndex tile, ViewportPlaceMethod method, ViewportDragDropSelectionProcess process)
{
	_thd.select_method = method;
	_thd.select_proc   = process;
	_thd.selend.x = TileX(tile) * TILE_SIZE;
	_thd.selstart.x = TileX(tile) * TILE_SIZE;
	_thd.selend.y = TileY(tile) * TILE_SIZE;
	_thd.selstart.y = TileY(tile) * TILE_SIZE;

	/* Needed so several things (road, autoroad, bridges, ...) are placed correctly.
	 * In effect, placement starts from the centre of a tile
	 */
	if (method == VPM_X_OR_Y || method == VPM_FIX_X || method == VPM_FIX_Y) {
		_thd.selend.x += TILE_SIZE / 2;
		_thd.selend.y += TILE_SIZE / 2;
		_thd.selstart.x += TILE_SIZE / 2;
		_thd.selstart.y += TILE_SIZE / 2;
	}

	HighLightStyle others = _thd.place_mode & ~(HT_DRAG_MASK | HT_DIR_MASK);
	if ((_thd.place_mode & HT_DRAG_MASK) == HT_RECT) {
		_thd.place_mode = HT_SPECIAL | others;
		_thd.next_drawstyle = HT_RECT | others;
	} else if (_thd.place_mode & (HT_RAIL | HT_LINE)) {
		_thd.place_mode = HT_SPECIAL | others;
		_thd.next_drawstyle = _thd.drawstyle | others;
		_current_snap_lock.x = -1;
		if ((_thd.place_mode & HT_POLY) != 0 && GetRailSnapMode() == RSM_NO_SNAP) {
			SetRailSnapMode(RSM_SNAP_TO_TILE);
			SetRailSnapTile(tile);
		}
	} else {
		_thd.place_mode = HT_SPECIAL | others;
		_thd.next_drawstyle = HT_POINT | others;
	}
	_special_mouse_mode = WSM_SIZING;
}

void VpSetPlaceSizingLimit(int limit)
{
	_thd.sizelimit = limit;
}

/**
 * Highlights all tiles between a set of two tiles. Used in dock and tunnel placement
 * @param from TileIndex of the first tile to highlight
 * @param to TileIndex of the last tile to highlight
 */
void VpSetPresizeRange(TileIndex from, TileIndex to)
{
	uint64 distance = DistanceManhattan(from, to) + 1;

	_thd.selend.x = TileX(to) * TILE_SIZE;
	_thd.selend.y = TileY(to) * TILE_SIZE;
	_thd.selstart.x = TileX(from) * TILE_SIZE;
	_thd.selstart.y = TileY(from) * TILE_SIZE;
	_thd.next_drawstyle = HT_RECT;

	/* show measurement only if there is any length to speak of */
	if (distance > 1) {
		ShowMeasurementTooltips(STR_MEASURE_LENGTH, 1, &distance);
	} else {
		HideMeasurementTooltips();
	}
}

static void VpStartPreSizing()
{
	_thd.selend.x = -1;
	_special_mouse_mode = WSM_PRESIZE;
}

/**
 * returns information about the 2x1 piece to be build.
 * The lower bits (0-3) are the track type.
 */
static HighLightStyle Check2x1AutoRail(int mode)
{
	int fxpy = _tile_fract_coords.x + _tile_fract_coords.y;
	int sxpy = (_thd.selend.x & TILE_UNIT_MASK) + (_thd.selend.y & TILE_UNIT_MASK);
	int fxmy = _tile_fract_coords.x - _tile_fract_coords.y;
	int sxmy = (_thd.selend.x & TILE_UNIT_MASK) - (_thd.selend.y & TILE_UNIT_MASK);

	switch (mode) {
		default: NOT_REACHED();
		case 0: // end piece is lower right
			if (fxpy >= 20 && sxpy <= 12) return HT_DIR_HL;
			if (fxmy < -3 && sxmy > 3) return HT_DIR_VR;
			return HT_DIR_Y;

		case 1:
			if (fxmy > 3 && sxmy < -3) return HT_DIR_VL;
			if (fxpy <= 12 && sxpy >= 20) return HT_DIR_HU;
			return HT_DIR_Y;

		case 2:
			if (fxmy > 3 && sxmy < -3) return HT_DIR_VL;
			if (fxpy >= 20 && sxpy <= 12) return HT_DIR_HL;
			return HT_DIR_X;

		case 3:
			if (fxmy < -3 && sxmy > 3) return HT_DIR_VR;
			if (fxpy <= 12 && sxpy >= 20) return HT_DIR_HU;
			return HT_DIR_X;
	}
}

/**
 * Check if the direction of start and end tile should be swapped based on
 * the dragging-style. Default directions are:
 * in the case of a line (HT_RAIL, HT_LINE):  DIR_NE, DIR_NW, DIR_N, DIR_E
 * in the case of a rect (HT_RECT, HT_POINT): DIR_S, DIR_E
 * For example dragging a rectangle area from south to north should be swapped to
 * north-south (DIR_S) to obtain the same results with less code. This is what
 * the return value signifies.
 * @param style HighLightStyle dragging style
 * @param start_tile start tile of drag
 * @param end_tile end tile of drag
 * @return boolean value which when true means start/end should be swapped
 */
static bool SwapDirection(HighLightStyle style, TileIndex start_tile, TileIndex end_tile)
{
	uint start_x = TileX(start_tile);
	uint start_y = TileY(start_tile);
	uint end_x = TileX(end_tile);
	uint end_y = TileY(end_tile);

	switch (style & HT_DRAG_MASK) {
		case HT_RAIL:
		case HT_LINE: return (end_x > start_x || (end_x == start_x && end_y > start_y));

		case HT_RECT:
		case HT_POINT: return (end_x != start_x && end_y < start_y);
		default: NOT_REACHED();
	}

	return false;
}

/**
 * Calculates height difference between one tile and another.
 * Multiplies the result to suit the standard given by #TILE_HEIGHT_STEP.
 *
 * To correctly get the height difference we need the direction we are dragging
 * in, as well as with what kind of tool we are dragging. For example a horizontal
 * autorail tool that starts in bottom and ends at the top of a tile will need the
 * maximum of SW, S and SE, N corners respectively. This is handled by the lookup table below
 * See #_tileoffs_by_dir in map.cpp for the direction enums if you can't figure out the values yourself.
 * @param style      Highlighting style of the drag. This includes direction and style (autorail, rect, etc.)
 * @param distance   Number of tiles dragged, important for horizontal/vertical drags, ignored for others.
 * @param start_tile Start tile of the drag operation.
 * @param end_tile   End tile of the drag operation.
 * @return Height difference between two tiles. The tile measurement tool utilizes this value in its tooltip.
 */
static int CalcHeightdiff(HighLightStyle style, uint distance, TileIndex start_tile, TileIndex end_tile)
{
	bool swap = SwapDirection(style, start_tile, end_tile);
	uint h0, h1; // Start height and end height.

	if (start_tile == end_tile) return 0;
	if (swap) Swap(start_tile, end_tile);

	switch (style & HT_DRAG_MASK) {
		case HT_RECT: {
			static const TileIndexDiffC heightdiff_area_by_dir[] = {
				/* Start */ {1, 0}, /* Dragging east */ {0, 0}, // Dragging south
				/* End   */ {0, 1}, /* Dragging east */ {1, 1}  // Dragging south
			};

			/* In the case of an area we can determine whether we were dragging south or
			 * east by checking the X-coordinates of the tiles */
			byte style_t = (byte)(TileX(end_tile) > TileX(start_tile));
			start_tile = TILE_ADD(start_tile, ToTileIndexDiff(heightdiff_area_by_dir[style_t]));
			end_tile   = TILE_ADD(end_tile, ToTileIndexDiff(heightdiff_area_by_dir[2 + style_t]));
			FALLTHROUGH;
		}

		case HT_POINT:
			h0 = TileHeight(start_tile);
			h1 = TileHeight(end_tile);
			break;
		default: { // All other types, this is mostly only line/autorail
			static const HighLightStyle flip_style_direction[] = {
				HT_DIR_X, HT_DIR_Y, HT_DIR_HL, HT_DIR_HU, HT_DIR_VR, HT_DIR_VL
			};
			static const TileIndexDiffC heightdiff_line_by_dir[] = {
				/* Start */ {1, 0}, {1, 1}, /* HT_DIR_X  */ {0, 1}, {1, 1}, // HT_DIR_Y
				/* Start */ {1, 0}, {0, 0}, /* HT_DIR_HU */ {1, 0}, {1, 1}, // HT_DIR_HL
				/* Start */ {1, 0}, {1, 1}, /* HT_DIR_VL */ {0, 1}, {1, 1}, // HT_DIR_VR

				/* Start */ {0, 1}, {0, 0}, /* HT_DIR_X  */ {1, 0}, {0, 0}, // HT_DIR_Y
				/* End   */ {0, 1}, {0, 0}, /* HT_DIR_HU */ {1, 1}, {0, 1}, // HT_DIR_HL
				/* End   */ {1, 0}, {0, 0}, /* HT_DIR_VL */ {0, 0}, {0, 1}, // HT_DIR_VR
			};

			distance %= 2; // we're only interested if the distance is even or uneven
			style &= HT_DIR_MASK;

			/* To handle autorail, we do some magic to be able to use a lookup table.
			 * Firstly if we drag the other way around, we switch start&end, and if needed
			 * also flip the drag-position. Eg if it was on the left, and the distance is even
			 * that means the end, which is now the start is on the right */
			if (swap && distance == 0) style = flip_style_direction[style];

			/* Use lookup table for start-tile based on HighLightStyle direction */
			byte style_t = style * 2;
			assert(style_t < lengthof(heightdiff_line_by_dir) - 13);
			h0 = TileHeight(TILE_ADD(start_tile, ToTileIndexDiff(heightdiff_line_by_dir[style_t])));
			uint ht = TileHeight(TILE_ADD(start_tile, ToTileIndexDiff(heightdiff_line_by_dir[style_t + 1])));
			h0 = max(h0, ht);

			/* Use lookup table for end-tile based on HighLightStyle direction
			 * flip around side (lower/upper, left/right) based on distance */
			if (distance == 0) style_t = flip_style_direction[style] * 2;
			assert(style_t < lengthof(heightdiff_line_by_dir) - 13);
			h1 = TileHeight(TILE_ADD(end_tile, ToTileIndexDiff(heightdiff_line_by_dir[12 + style_t])));
			ht = TileHeight(TILE_ADD(end_tile, ToTileIndexDiff(heightdiff_line_by_dir[12 + style_t + 1])));
			h1 = max(h1, ht);
			break;
		}
	}

	if (swap) Swap(h0, h1);
	return (int)(h1 - h0) * TILE_HEIGHT_STEP;
}

static void ShowLengthMeasurement(HighLightStyle style, TileIndex start_tile, TileIndex end_tile, TooltipCloseCondition close_cond = TCC_NONE, bool show_single_tile_length = false)
{
	static const StringID measure_strings_length[] = {STR_NULL, STR_MEASURE_LENGTH, STR_MEASURE_LENGTH_HEIGHTDIFF};

	if (_settings_client.gui.measure_tooltip) {
		uint distance = DistanceManhattan(start_tile, end_tile) + 1;
		byte index = 0;
		uint64 params[2];

		if (show_single_tile_length || distance != 1) {
			int heightdiff = CalcHeightdiff(style, distance, start_tile, end_tile);
			/* If we are showing a tooltip for horizontal or vertical drags,
			 * 2 tiles have a length of 1. To bias towards the ceiling we add
			 * one before division. It feels more natural to count 3 lengths as 2 */
			if ((style & HT_DIR_MASK) != HT_DIR_X && (style & HT_DIR_MASK) != HT_DIR_Y) {
				distance = CeilDiv(distance, 2);
			}

			params[index++] = distance;
			if (heightdiff != 0) params[index++] = heightdiff;
		}

		ShowMeasurementTooltips(measure_strings_length[index], index, params, close_cond);
	}
}

/**
 * Check for underflowing the map.
 * @param test  the variable to test for underflowing
 * @param other the other variable to update to keep the line
 * @param mult  the constant to multiply the difference by for \c other
 */
static void CheckUnderflow(int &test, int &other, int mult)
{
	if (test >= 0) return;

	other += mult * test;
	test = 0;
}

/**
 * Check for overflowing the map.
 * @param test  the variable to test for overflowing
 * @param other the other variable to update to keep the line
 * @param max   the maximum value for the \c test variable
 * @param mult  the constant to multiply the difference by for \c other
 */
static void CheckOverflow(int &test, int &other, int max, int mult)
{
	if (test <= max) return;

	other += mult * (test - max);
	test = max;
}

static const uint X_DIRS = (1 << DIR_NE) | (1 << DIR_SW);
static const uint Y_DIRS = (1 << DIR_SE) | (1 << DIR_NW);
static const uint HORZ_DIRS = (1 << DIR_W) | (1 << DIR_E);
//static const uint VERT_DIRS = (1 << DIR_N) | (1 << DIR_S);

Trackdir PointDirToTrackdir(const Point &pt, Direction dir)
{
	Trackdir ret;

	if (IsDiagonalDirection(dir)) {
		ret = DiagDirToDiagTrackdir(DirToDiagDir(dir));
	} else {
		int x = pt.x & TILE_UNIT_MASK;
		int y = pt.y & TILE_UNIT_MASK;
		int ns = x + y;
		int we = y - x;
		if (HasBit(HORZ_DIRS, dir)) {
			ret = TrackDirectionToTrackdir(ns < (int)TILE_SIZE ? TRACK_UPPER : TRACK_LOWER, dir);
		} else {
			ret = TrackDirectionToTrackdir(we < 0 ? TRACK_LEFT : TRACK_RIGHT, dir);
		}
	}

	return ret;
}

static bool FindPolyline(const Point &pt, const LineSnapPoint &start, PolylineInfo *ret)
{
	/* relative coordinats of the mouse point (offset against the snap point) */
	int x = pt.x - start.x;
	int y = pt.y - start.y;
	int we = y - x;
	int ns = x + y;

	/* in-tile alignment of the snap point (there are two variants: [0, 8] or [8, 0]) */
	uint align_x = start.x & TILE_UNIT_MASK;
	uint align_y = start.y & TILE_UNIT_MASK;
	assert((align_x == TILE_SIZE / 2 && align_y == 0 && !(start.dirs & X_DIRS)) || (align_x == 0 && align_y == TILE_SIZE / 2 && !(start.dirs & Y_DIRS)));

	/* absolute distance between points (in tiles) */
	uint d_x = abs(RoundDivSU(x < 0 ? x - align_y : x + align_y, TILE_SIZE));
	uint d_y = abs(RoundDivSU(y < 0 ? y - align_x : y + align_x, TILE_SIZE));
	uint d_ns = abs(RoundDivSU(ns, TILE_SIZE));
	uint d_we = abs(RoundDivSU(we, TILE_SIZE));

	/* Find on which quadrant is the mouse point (reltively to the snap point).
	 * Numeration (clockwise like in Direction):
	 * ortho            diag
	 *   \   2   /       2 | 3
	 *     \   /         --+---> [we]
	 *  1    X    3      1 | 0
	 *     /   \           v
	 *  [x]  0  [y]       [ns]          */
	uint ortho_quadrant = 2 * (x < 0) + ((x < 0) != (y < 0)); // implicit cast: false/true --> 0/1
	uint diag_quadrant = 2 * (ns < 0) + ((ns < 0) != (we < 0));

	/* direction from the snap point to the mouse point */
	Direction ortho_line_dir = ChangeDir(DIR_S, (DirDiff)(2 * ortho_quadrant)); // DIR_S is the middle of the ortho quadrant no. 0
	Direction diag_line_dir = ChangeDir(DIR_SE, (DirDiff)(2 * diag_quadrant));  // DIR_SE is the middle of the diag quadrant no. 0
	if (!HasBit(start.dirs, ortho_line_dir) && !HasBit(start.dirs, diag_line_dir)) return false;

	/* length of booth segments of auto line (choosing orthogonal direction first) */
	uint ortho_len = 0, ortho_len2 = 0;
	if (HasBit(start.dirs, ortho_line_dir)) {
		bool is_len_even = (align_x != 0) ? d_x >= d_y : d_x <= d_y;
		ortho_len = 2 * min(d_x, d_y) - (int)is_len_even;
		assert((int)ortho_len >= 0);
		if (d_ns == 0 || d_we == 0) { // just single segment?
			ortho_len++;
		} else {
			ortho_len2 = abs((int)d_x - (int)d_y) + (int)is_len_even;
		}
	}

	/* length of booth segments of auto line (choosing diagonal direction first) */
	uint diag_len = 0, diag_len2 = 0;
	if (HasBit(start.dirs, diag_line_dir)) {
		if (d_x == 0 || d_y == 0) { // just single segment?
			diag_len = d_x + d_y;
		} else {
			diag_len = min(d_ns, d_we);
			diag_len2 = d_x + d_y - diag_len;
		}
	}

	/* choose the best variant */
	if (ortho_len != 0 && diag_len != 0) {
		/* in the first place, choose this line whose first segment ends up closer
		 * to the mouse point (thus the second segment is shorter) */
		int cmp = ortho_len2 - diag_len2;
		/* if equeal, choose the shorter line */
		if (cmp == 0) cmp = ortho_len - diag_len;
		/* finally look at small "units" and choose the line which is closer to the mouse point */
		if (cmp == 0) cmp = min(abs(we), abs(ns)) - min(abs(x), abs(y));
		/* based on comparison, disable one of variants */
		if (cmp > 0) {
			ortho_len = 0;
		} else {
			diag_len = 0;
		}
	}

	/* store results */
	if (ortho_len != 0) {
		ret->first_dir = ortho_line_dir;
		ret->first_len = ortho_len;
		ret->second_dir = (ortho_len2 != 0) ? diag_line_dir : INVALID_DIR;
		ret->second_len = ortho_len2;
	} else if (diag_len != 0) {
		ret->first_dir = diag_line_dir;
		ret->first_len = diag_len;
		ret->second_dir = (diag_len2 != 0) ? ortho_line_dir : INVALID_DIR;
		ret->second_len = diag_len2;
	} else {
		return false;
	}

	ret->start = start;
	return true;
}

/**
 * Calculate squared euclidean distance between two points.
 * @param a the first point
 * @param b the second point
 * @return |b - a| ^ 2
 */
static inline uint SqrDist(const Point &a, const Point &b)
{
	return (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
}

static LineSnapPoint *FindBestPolyline(const Point &pt, LineSnapPoint *snap_points, uint num_points, PolylineInfo *ret)
{
	/* Find the best polyline (a pair of two lines - the white one and the blue
	 * one) led from any of saved snap points to the mouse cursor. */

	LineSnapPoint *best_snap_point = nullptr; // the best polyline we found so far is led from this snap point

	for (int i = 0; i < (int)num_points; i++) {
		/* try to fit a polyline */
		PolylineInfo polyline;
		if (!FindPolyline(pt, snap_points[i], &polyline)) continue; // skip non-matching snap points
		/* check whether we've found a better polyline */
		if (best_snap_point != nullptr) {
			/* firstly choose shorter polyline (the one with smaller amount of
			 * track pieces composing booth the white and the blue line) */
			uint cur_len = polyline.first_len + polyline.second_len;
			uint best_len = ret->first_len + ret->second_len;
			if (cur_len > best_len) continue;
			/* secondly choose that polyline which has longer first (white) line */
			if (cur_len == best_len && polyline.first_len < ret->first_len) continue;
			/* finally check euclidean distance to snap points and choose the
			 * one which is closer */
			if (cur_len == best_len && polyline.first_len == ret->first_len && SqrDist(pt, snap_points[i]) >= SqrDist(pt, *best_snap_point)) continue;
		}
		/* save the found polyline */
		*ret = polyline;
		best_snap_point = &snap_points[i];
	}

	return best_snap_point;
}

/** while dragging */
static void CalcRaildirsDrawstyle(int x, int y, int method)
{
	HighLightStyle b;

	int dx = _thd.selstart.x - (_thd.selend.x & ~TILE_UNIT_MASK);
	int dy = _thd.selstart.y - (_thd.selend.y & ~TILE_UNIT_MASK);
	uint w = abs(dx) + TILE_SIZE;
	uint h = abs(dy) + TILE_SIZE;

	if (method & ~(VPM_RAILDIRS | VPM_SIGNALDIRS)) {
		/* We 'force' a selection direction; first four rail buttons. */
		method &= ~(VPM_RAILDIRS | VPM_SIGNALDIRS);
		int raw_dx = _thd.selstart.x - _thd.selend.x;
		int raw_dy = _thd.selstart.y - _thd.selend.y;
		switch (method) {
			case VPM_FIX_X:
				b = HT_LINE | HT_DIR_Y;
				x = _thd.selstart.x;
				break;

			case VPM_FIX_Y:
				b = HT_LINE | HT_DIR_X;
				y = _thd.selstart.y;
				break;

			case VPM_FIX_HORIZONTAL:
				if (dx == -dy) {
					/* We are on a straight horizontal line. Determine the 'rail'
					 * to build based the sub tile location. */
					b = (x & TILE_UNIT_MASK) + (y & TILE_UNIT_MASK) >= TILE_SIZE ? HT_LINE | HT_DIR_HL : HT_LINE | HT_DIR_HU;
				} else {
					/* We are not on a straight line. Determine the rail to build
					 * based on whether we are above or below it. */
					b = dx + dy >= (int)TILE_SIZE ? HT_LINE | HT_DIR_HU : HT_LINE | HT_DIR_HL;

					/* Calculate where a horizontal line through the start point and
					 * a vertical line from the selected end point intersect and
					 * use that point as the end point. */
					int offset = (raw_dx - raw_dy) / 2;
					x = _thd.selstart.x - (offset & ~TILE_UNIT_MASK);
					y = _thd.selstart.y + (offset & ~TILE_UNIT_MASK);

					/* 'Build' the last half rail tile if needed */
					if ((offset & TILE_UNIT_MASK) > (TILE_SIZE / 2)) {
						if (dx + dy >= (int)TILE_SIZE) {
							x += (dx + dy < 0) ? (int)TILE_SIZE : -(int)TILE_SIZE;
						} else {
							y += (dx + dy < 0) ? (int)TILE_SIZE : -(int)TILE_SIZE;
						}
					}

					/* Make sure we do not overflow the map! */
					CheckUnderflow(x, y, 1);
					CheckUnderflow(y, x, 1);
					CheckOverflow(x, y, (MapMaxX() - 1) * TILE_SIZE, 1);
					CheckOverflow(y, x, (MapMaxY() - 1) * TILE_SIZE, 1);
					assert(x >= 0 && y >= 0 && x <= (int)(MapMaxX() * TILE_SIZE) && y <= (int)(MapMaxY() * TILE_SIZE));
				}
				break;

			case VPM_FIX_VERTICAL:
				if (dx == dy) {
					/* We are on a straight vertical line. Determine the 'rail'
					 * to build based the sub tile location. */
					b = (x & TILE_UNIT_MASK) > (y & TILE_UNIT_MASK) ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;
				} else {
					/* We are not on a straight line. Determine the rail to build
					 * based on whether we are left or right from it. */
					b = dx < dy ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;

					/* Calculate where a vertical line through the start point and
					 * a horizontal line from the selected end point intersect and
					 * use that point as the end point. */
					int offset = (raw_dx + raw_dy + (int)TILE_SIZE) / 2;
					x = _thd.selstart.x - (offset & ~TILE_UNIT_MASK);
					y = _thd.selstart.y - (offset & ~TILE_UNIT_MASK);

					/* 'Build' the last half rail tile if needed */
					if ((offset & TILE_UNIT_MASK) > (TILE_SIZE / 2)) {
						if (dx - dy < 0) {
							y += (dx > dy) ? (int)TILE_SIZE : -(int)TILE_SIZE;
						} else {
							x += (dx < dy) ? (int)TILE_SIZE : -(int)TILE_SIZE;
						}
					}

					/* Make sure we do not overflow the map! */
					CheckUnderflow(x, y, -1);
					CheckUnderflow(y, x, -1);
					CheckOverflow(x, y, (MapMaxX() - 1) * TILE_SIZE, -1);
					CheckOverflow(y, x, (MapMaxY() - 1) * TILE_SIZE, -1);
					assert(x >= 0 && y >= 0 && x <= (int)(MapMaxX() * TILE_SIZE) && y <= (int)(MapMaxY() * TILE_SIZE));
				}
				break;

			default:
				NOT_REACHED();
		}
	} else if (TileVirtXY(_thd.selstart.x, _thd.selstart.y) == TileVirtXY(x, y)) { // check if we're only within one tile
		if (method & VPM_RAILDIRS) {
			b = GetAutorailHT(x, y);
		} else { // rect for autosignals on one tile
			b = HT_RECT;
		}
	} else if (h == TILE_SIZE) { // Is this in X direction?
		if (dx == (int)TILE_SIZE) { // 2x1 special handling
			b = (Check2x1AutoRail(3)) | HT_LINE;
		} else if (dx == -(int)TILE_SIZE) {
			b = (Check2x1AutoRail(2)) | HT_LINE;
		} else {
			b = HT_LINE | HT_DIR_X;
		}
		y = _thd.selstart.y;
	} else if (w == TILE_SIZE) { // Or Y direction?
		if (dy == (int)TILE_SIZE) { // 2x1 special handling
			b = (Check2x1AutoRail(1)) | HT_LINE;
		} else if (dy == -(int)TILE_SIZE) { // 2x1 other direction
			b = (Check2x1AutoRail(0)) | HT_LINE;
		} else {
			b = HT_LINE | HT_DIR_Y;
		}
		x = _thd.selstart.x;
	} else if (w > h * 2) { // still count as x dir?
		b = HT_LINE | HT_DIR_X;
		y = _thd.selstart.y;
	} else if (h > w * 2) { // still count as y dir?
		b = HT_LINE | HT_DIR_Y;
		x = _thd.selstart.x;
	} else { // complicated direction
		int d = w - h;
		_thd.selend.x = _thd.selend.x & ~TILE_UNIT_MASK;
		_thd.selend.y = _thd.selend.y & ~TILE_UNIT_MASK;

		/* four cases. */
		if (x > _thd.selstart.x) {
			if (y > _thd.selstart.y) {
				/* south */
				if (d == 0) {
					b = (x & TILE_UNIT_MASK) > (y & TILE_UNIT_MASK) ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;
				} else if (d >= 0) {
					x = _thd.selstart.x + h;
					b = HT_LINE | HT_DIR_VL;
				} else {
					y = _thd.selstart.y + w;
					b = HT_LINE | HT_DIR_VR;
				}
			} else {
				/* west */
				if (d == 0) {
					b = (x & TILE_UNIT_MASK) + (y & TILE_UNIT_MASK) >= TILE_SIZE ? HT_LINE | HT_DIR_HL : HT_LINE | HT_DIR_HU;
				} else if (d >= 0) {
					x = _thd.selstart.x + h;
					b = HT_LINE | HT_DIR_HL;
				} else {
					y = _thd.selstart.y - w;
					b = HT_LINE | HT_DIR_HU;
				}
			}
		} else {
			if (y > _thd.selstart.y) {
				/* east */
				if (d == 0) {
					b = (x & TILE_UNIT_MASK) + (y & TILE_UNIT_MASK) >= TILE_SIZE ? HT_LINE | HT_DIR_HL : HT_LINE | HT_DIR_HU;
				} else if (d >= 0) {
					x = _thd.selstart.x - h;
					b = HT_LINE | HT_DIR_HU;
				} else {
					y = _thd.selstart.y + w;
					b = HT_LINE | HT_DIR_HL;
				}
			} else {
				/* north */
				if (d == 0) {
					b = (x & TILE_UNIT_MASK) > (y & TILE_UNIT_MASK) ? HT_LINE | HT_DIR_VL : HT_LINE | HT_DIR_VR;
				} else if (d >= 0) {
					x = _thd.selstart.x - h;
					b = HT_LINE | HT_DIR_VR;
				} else {
					y = _thd.selstart.y - w;
					b = HT_LINE | HT_DIR_VL;
				}
			}
		}
	}

	_thd.selend.x = x;
	_thd.selend.y = y;
	_thd.dir2 = HT_DIR_END;
	_thd.next_drawstyle = b;

	ShowLengthMeasurement(b, TileVirtXY(_thd.selstart.x, _thd.selstart.y), TileVirtXY(_thd.selend.x, _thd.selend.y));
}

static HighLightStyle CalcPolyrailDrawstyle(Point pt, bool dragging)
{
	RailSnapMode snap_mode = GetRailSnapMode();

	/* are we only within one tile? */
	if (snap_mode == RSM_SNAP_TO_TILE && GetRailSnapTile() == TileVirtXY(pt.x, pt.y)) {
		_thd.selend.x = pt.x;
		_thd.selend.y = pt.y;
		return GetAutorailHT(pt.x, pt.y);
	}

	/* find the best track */
	PolylineInfo line;

	bool lock_snapping = dragging && snap_mode == RSM_SNAP_TO_RAIL;
	if (!lock_snapping) _current_snap_lock.x = -1;

	const LineSnapPoint *snap_point;
	if (_current_snap_lock.x != -1) {
		snap_point = FindBestPolyline(pt, &_current_snap_lock, 1, &line);
	} else if (snap_mode == RSM_SNAP_TO_TILE) {
		snap_point = FindBestPolyline(pt, _tile_snap_points.data(), _tile_snap_points.size(), &line);
	} else {
		assert(snap_mode == RSM_SNAP_TO_RAIL);
		snap_point = FindBestPolyline(pt, _rail_snap_points.data(), _rail_snap_points.size(), &line);
	}

	if (snap_point == nullptr) return HT_NONE; // no match

	if (lock_snapping && _current_snap_lock.x == -1) {
		/* lock down the snap point */
		_current_snap_lock = *snap_point;
		_current_snap_lock.dirs &= (1 << line.first_dir) | (1 << ReverseDir(line.first_dir));
	}

	TileIndexDiffC first_dir = TileIndexDiffCByDir(line.first_dir);
	_thd.selstart.x  = line.start.x;
	_thd.selstart.y  = line.start.y;
	_thd.selend.x    = _thd.selstart.x + line.first_len * first_dir.x * (IsDiagonalDirection(line.first_dir) ? TILE_SIZE : TILE_SIZE / 2);
	_thd.selend.y    = _thd.selstart.y + line.first_len * first_dir.y * (IsDiagonalDirection(line.first_dir) ? TILE_SIZE : TILE_SIZE / 2);
	_thd.selstart2.x = _thd.selend.x;
	_thd.selstart2.y = _thd.selend.y;
	_thd.selstart.x  += first_dir.x;
	_thd.selstart.y  += first_dir.y;
	_thd.selend.x    -= first_dir.x;
	_thd.selend.y    -= first_dir.y;
	Trackdir seldir = PointDirToTrackdir(_thd.selstart, line.first_dir);
	_thd.selstart.x  &= ~TILE_UNIT_MASK;
	_thd.selstart.y  &= ~TILE_UNIT_MASK;

	if (line.second_len != 0) {
		TileIndexDiffC second_dir = TileIndexDiffCByDir(line.second_dir);
		_thd.selend2.x   = _thd.selstart2.x + line.second_len * second_dir.x * (IsDiagonalDirection(line.second_dir) ? TILE_SIZE : TILE_SIZE / 2);
		_thd.selend2.y   = _thd.selstart2.y + line.second_len * second_dir.y * (IsDiagonalDirection(line.second_dir) ? TILE_SIZE : TILE_SIZE / 2);
		_thd.selstart2.x += second_dir.x;
		_thd.selstart2.y += second_dir.y;
		_thd.selend2.x   -= second_dir.x;
		_thd.selend2.y   -= second_dir.y;
		Trackdir seldir2 = PointDirToTrackdir(_thd.selstart2, line.second_dir);
		_thd.selstart2.x &= ~TILE_UNIT_MASK;
		_thd.selstart2.y &= ~TILE_UNIT_MASK;
		_thd.dir2 = (HighLightStyle)TrackdirToTrack(seldir2);
	} else {
		_thd.dir2 = HT_DIR_END;
	}

	HighLightStyle ret = HT_LINE | (HighLightStyle)TrackdirToTrack(seldir);
	ShowLengthMeasurement(ret, TileVirtXY(_thd.selstart.x, _thd.selstart.y), TileVirtXY(_thd.selend.x, _thd.selend.y), TCC_HOVER, true);
	return ret;
}

/**
 * Selects tiles while dragging
 * @param x X coordinate of end of selection
 * @param y Y coordinate of end of selection
 * @param method modifies the way tiles are selected. Possible
 * methods are VPM_* in viewport.h
 */
void VpSelectTilesWithMethod(int x, int y, ViewportPlaceMethod method)
{
	int sx, sy;
	HighLightStyle style;

	if (x == -1) {
		_thd.selend.x = -1;
		return;
	}

	if ((_thd.place_mode & HT_POLY) && GetRailSnapMode() != RSM_NO_SNAP) {
		Point pt = { x, y };
		_thd.next_drawstyle = CalcPolyrailDrawstyle(pt, true);
		return;
	}

	/* Special handling of drag in any (8-way) direction */
	if (method & (VPM_RAILDIRS | VPM_SIGNALDIRS)) {
		_thd.selend.x = x;
		_thd.selend.y = y;
		CalcRaildirsDrawstyle(x, y, method);
		return;
	}

	/* Needed so level-land is placed correctly */
	if ((_thd.next_drawstyle & HT_DRAG_MASK) == HT_POINT) {
		x += TILE_SIZE / 2;
		y += TILE_SIZE / 2;
	}

	sx = _thd.selstart.x;
	sy = _thd.selstart.y;

	int limit = 0;

	switch (method) {
		case VPM_X_OR_Y: // drag in X or Y direction
			if (abs(sy - y) < abs(sx - x)) {
				y = sy;
				style = HT_DIR_X;
			} else {
				x = sx;
				style = HT_DIR_Y;
			}
			goto calc_heightdiff_single_direction;

		case VPM_X_LIMITED: // Drag in X direction (limited size).
			limit = (_thd.sizelimit - 1) * TILE_SIZE;
			FALLTHROUGH;

		case VPM_FIX_X: // drag in Y direction
			x = sx;
			style = HT_DIR_Y;
			goto calc_heightdiff_single_direction;

		case VPM_Y_LIMITED: // Drag in Y direction (limited size).
			limit = (_thd.sizelimit - 1) * TILE_SIZE;
			FALLTHROUGH;

		case VPM_FIX_Y: // drag in X direction
			y = sy;
			style = HT_DIR_X;

calc_heightdiff_single_direction:;
			if (limit > 0) {
				x = sx + Clamp(x - sx, -limit, limit);
				y = sy + Clamp(y - sy, -limit, limit);
			}
			/* With current code passing a HT_LINE style to calculate the height
			 * difference is enough. However if/when a point-tool is created
			 * with this method, function should be called with new_style (below)
			 * instead of HT_LINE | style case HT_POINT is handled specially
			 * new_style := (_thd.next_drawstyle & HT_RECT) ? HT_LINE | style : _thd.next_drawstyle; */
			ShowLengthMeasurement(HT_LINE | style, TileVirtXY(sx, sy), TileVirtXY(x, y));
			break;

		case VPM_A_B_LINE: { // drag an A to B line
			TileIndex t0 = TileVirtXY(sx, sy);
			TileIndex t1 = TileVirtXY(x, y);
			uint dx = Delta(TileX(t0), TileX(t1)) + 1;
			uint dy = Delta(TileY(t0), TileY(t1)) + 1;
			byte index = 0;
			uint64 params[5];
			memset( params, 0, sizeof( params ) );

			/* If dragging an area (eg dynamite tool) and it is actually a single
			 * row/column, change the type to 'line' to get proper calculation for height */
			style = (HighLightStyle)_thd.next_drawstyle;
			if (style & HT_RECT) {
				if (dx == 1) {
					style = HT_LINE | HT_DIR_Y;
				} else if (dy == 1) {
					style = HT_LINE | HT_DIR_X;
				}
			}

			int heightdiff = 0;

			if (dx != 1 || dy != 1) {
				heightdiff = CalcHeightdiff(style, 0, t0, t1);
				params[index++] = DistanceManhattan(t0, t1);
				params[index++] = sqrtl(dx * dx + dy * dy); //DistanceSquare does not like big numbers

			} else {
				index += 2;
			}

			params[index++] = DistanceFromEdge(t1);
			params[index++] = GetTileMaxZ(t1) * TILE_HEIGHT_STEP;
			params[index++] = heightdiff;
			//Show always the measurement tooltip
			GuiShowTooltips(_thd.GetCallbackWnd(),STR_MEASURE_DIST_HEIGHTDIFF, index, params, TCC_NONE);
			break;
		}

		case VPM_X_AND_Y_LIMITED: // Drag an X by Y constrained rect area.
			limit = (_thd.sizelimit - 1) * TILE_SIZE;
			x = sx + Clamp(x - sx, -limit, limit);
			y = sy + Clamp(y - sy, -limit, limit);
			FALLTHROUGH;

		case VPM_X_AND_Y: // drag an X by Y area
			if (_settings_client.gui.measure_tooltip) {
				static const StringID measure_strings_area[] = {
					STR_NULL, STR_NULL, STR_MEASURE_AREA, STR_MEASURE_AREA_HEIGHTDIFF
				};

				TileIndex t0 = TileVirtXY(sx, sy);
				TileIndex t1 = TileVirtXY(x, y);
				uint dx = Delta(TileX(t0), TileX(t1)) + 1;
				uint dy = Delta(TileY(t0), TileY(t1)) + 1;
				byte index = 0;
				uint64 params[3];

				/* If dragging an area (eg dynamite tool) and it is actually a single
				 * row/column, change the type to 'line' to get proper calculation for height */
				style = (HighLightStyle)_thd.next_drawstyle;
				if (_thd.IsDraggingDiagonal()) {
					/* Determine the "area" of the diagonal dragged selection.
					 * We assume the area is the number of tiles along the X
					 * edge and the number of tiles along the Y edge. However,
					 * multiplying these two numbers does not give the exact
					 * number of tiles; basically we are counting the black
					 * squares on a chess board and ignore the white ones to
					 * make the tile counts at the edges match up. There is no
					 * other way to make a proper count though.
					 *
					 * First convert to the rotated coordinate system. */
					int dist_x = TileX(t0) - TileX(t1);
					int dist_y = TileY(t0) - TileY(t1);
					int a_max = dist_x + dist_y;
					int b_max = dist_y - dist_x;

					/* Now determine the size along the edge, but due to the
					 * chess board principle this counts double. */
					a_max = abs(a_max + (a_max > 0 ? 2 : -2)) / 2;
					b_max = abs(b_max + (b_max > 0 ? 2 : -2)) / 2;

					/* We get a 1x1 on normal 2x1 rectangles, due to it being
					 * a seen as two sides. As the result for actual building
					 * will be the same as non-diagonal dragging revert to that
					 * behaviour to give it a more normally looking size. */
					if (a_max != 1 || b_max != 1) {
						dx = a_max;
						dy = b_max;
					}
				} else if (style & HT_RECT) {
					if (dx == 1) {
						style = HT_LINE | HT_DIR_Y;
					} else if (dy == 1) {
						style = HT_LINE | HT_DIR_X;
					}
				}

				if (dx != 1 || dy != 1) {
					int heightdiff = CalcHeightdiff(style, 0, t0, t1);

					params[index++] = dx - (style & HT_POINT ? 1 : 0);
					params[index++] = dy - (style & HT_POINT ? 1 : 0);
					if (heightdiff != 0) params[index++] = heightdiff;
				}

				ShowMeasurementTooltips(measure_strings_area[index], index, params);
			}
			break;

		default: NOT_REACHED();
	}

	_thd.selend.x = x;
	_thd.selend.y = y;
	_thd.dir2 = HT_DIR_END;
}

/**
 * Handle the mouse while dragging for placement/resizing.
 * @return State of handling the event.
 */
EventState VpHandlePlaceSizingDrag()
{
	if (_special_mouse_mode != WSM_SIZING) return ES_NOT_HANDLED;

	/* stop drag mode if the window has been closed */
	Window *w = _thd.GetCallbackWnd();
	if (w == nullptr) {
		ResetObjectToPlace();
		return ES_HANDLED;
	}

	/* While dragging execute the drag procedure of the corresponding window (mostly VpSelectTilesWithMethod() ).
	 * Do it even if the button is no longer pressed to make sure that OnPlaceDrag was called at least once. */
	w->OnPlaceDrag(_thd.select_method, _thd.select_proc, GetTileBelowCursor());
	if (_left_button_down) return ES_HANDLED;

	/* mouse button released..
	 * keep the selected tool, but reset it to the original mode. */
	_special_mouse_mode = WSM_NONE;
	HighLightStyle others = _thd.place_mode & ~(HT_DRAG_MASK | HT_DIR_MASK);
	if ((_thd.next_drawstyle & HT_DRAG_MASK) == HT_RECT) {
		_thd.place_mode = HT_RECT | others;
	} else if (_thd.select_method & VPM_SIGNALDIRS) {
		_thd.place_mode = HT_RECT | others;
	} else if (_thd.select_method & VPM_RAILDIRS) {
		_thd.place_mode = (_thd.select_method & ~VPM_RAILDIRS ? _thd.next_drawstyle : HT_RAIL) | others;
	} else {
		_thd.place_mode = HT_POINT | others;
	}
	SetTileSelectSize(1, 1);

	if (_thd.place_mode & HT_POLY) {
		if (GetRailSnapMode() == RSM_SNAP_TO_TILE) SetRailSnapMode(RSM_NO_SNAP);
		if (_thd.drawstyle == HT_NONE) return ES_HANDLED;
	}
	HideMeasurementTooltips();

	w->OnPlaceMouseUp(_thd.select_method, _thd.select_proc, _thd.selend, TileVirtXY(_thd.selstart.x, _thd.selstart.y), TileVirtXY(_thd.selend.x, _thd.selend.y));
	return ES_HANDLED;
}

/**
 * Change the cursor and mouse click/drag handling to a mode for performing special operations like tile area selection, object placement, etc.
 * @param icon New shape of the mouse cursor.
 * @param pal Palette to use.
 * @param mode Mode to perform.
 * @param w %Window requesting the mode change.
 */
void SetObjectToPlaceWnd(CursorID icon, PaletteID pal, HighLightStyle mode, Window *w)
{
	SetObjectToPlace(icon, pal, mode, w->window_class, w->window_number);
}

#include "table/animcursors.h"

/**
 * Change the cursor and mouse click/drag handling to a mode for performing special operations like tile area selection, object placement, etc.
 * @param icon New shape of the mouse cursor.
 * @param pal Palette to use.
 * @param mode Mode to perform.
 * @param window_class %Window class of the window requesting the mode change.
 * @param window_num Number of the window in its class requesting the mode change.
 */
void SetObjectToPlace(CursorID icon, PaletteID pal, HighLightStyle mode, WindowClass window_class, WindowNumber window_num)
{
	if (_thd.window_class != WC_INVALID) {
		/* Undo clicking on button and drag & drop */
		Window *w = _thd.GetCallbackWnd();
		/* Call the abort function, but set the window class to something
		 * that will never be used to avoid infinite loops. Setting it to
		 * the 'next' window class must not be done because recursion into
		 * this function might in some cases reset the newly set object to
		 * place or not properly reset the original selection. */
		_thd.window_class = WC_INVALID;
		if (w != nullptr) {
			w->OnPlaceObjectAbort();
			HideMeasurementTooltips();
		}
	}

	/* Mark the old selection dirty, in case the selection shape or colour changes */
	if ((_thd.drawstyle & HT_DRAG_MASK) != HT_NONE) SetSelectionTilesDirty();

	SetTileSelectSize(1, 1);

	_thd.make_square_red = false;

	if (mode == HT_DRAG) { // HT_DRAG is for dragdropping trains in the depot window
		mode = HT_NONE;
		_special_mouse_mode = WSM_DRAGDROP;
	} else {
		_special_mouse_mode = WSM_NONE;
	}

	_thd.place_mode = mode;
	_thd.window_class = window_class;
	_thd.window_number = window_num;

	if ((mode & HT_DRAG_MASK) == HT_SPECIAL) { // special tools, like tunnels or docks start with presizing mode
		VpStartPreSizing();
	}

	if (mode & HT_POLY) {
		SetRailSnapMode((mode & HT_NEW_POLY) == HT_NEW_POLY ? RSM_NO_SNAP : RSM_SNAP_TO_RAIL);
	}

	if ((icon & ANIMCURSOR_FLAG) != 0) {
		SetAnimatedMouseCursor(_animcursors[icon & ~ANIMCURSOR_FLAG]);
	} else {
		SetMouseCursor(icon, pal);
	}

}

/** Reset the cursor and mouse mode handling back to default (normal cursor, only clicking in windows). */
void ResetObjectToPlace()
{
	SetObjectToPlace(SPR_CURSOR_MOUSE, PAL_NONE, HT_NONE, WC_MAIN_WINDOW, 0);
}

ViewportMapType ChangeRenderMode(const ViewPort *vp, bool down) {
	ViewportMapType map_type = vp->map_type;
	if (vp->zoom < ZOOM_LVL_DRAW_MAP) return map_type;
	if (down) {
		return (map_type == VPMT_MIN) ? VPMT_MAX : (ViewportMapType) (map_type - 1);
	} else {
		return (map_type == VPMT_MAX) ? VPMT_MIN : (ViewportMapType) (map_type + 1);
	}
}

Point GetViewportStationMiddle(const ViewPort *vp, const Station *st)
{
	int x = TileX(st->xy) * TILE_SIZE;
	int y = TileY(st->xy) * TILE_SIZE;
	int z = GetSlopePixelZ(Clamp(x, 0, MapSizeX() * TILE_SIZE - 1), Clamp(y, 0, MapSizeY() * TILE_SIZE - 1));

	Point p = RemapCoords(x, y, z);
	p.x = UnScaleByZoom(p.x - vp->virtual_left, vp->zoom) + vp->left;
	p.y = UnScaleByZoom(p.y - vp->virtual_top, vp->zoom) + vp->top;
	return p;
}

/** Helper class for getting the best sprite sorter. */
struct ViewportSSCSS {
	VpSorterChecker fct_checker; ///< The check function.
	VpSpriteSorter fct_sorter;   ///< The sorting function.
};

/** List of sorters ordered from best to worst. */
static ViewportSSCSS _vp_sprite_sorters[] = {
#ifdef WITH_SSE
	{ &ViewportSortParentSpritesSSE41Checker, &ViewportSortParentSpritesSSE41 },
#endif
	{ &ViewportSortParentSpritesChecker, &ViewportSortParentSprites }
};

/** Choose the "best" sprite sorter and set _vp_sprite_sorter. */
void InitializeSpriteSorter()
{
	for (uint i = 0; i < lengthof(_vp_sprite_sorters); i++) {
		if (_vp_sprite_sorters[i].fct_checker()) {
			_vp_sprite_sorter = _vp_sprite_sorters[i].fct_sorter;
			break;
		}
	}
	assert(_vp_sprite_sorter != nullptr);
}

/**
 * Scroll players main viewport.
 * @param tile tile to center viewport on
 * @param flags type of operation
 * @param p1 ViewportScrollTarget of scroll target
 * @param p2 company or client id depending on the target
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScrollViewport(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	ViewportScrollTarget target = (ViewportScrollTarget)p1;
	switch (target) {
		case VST_EVERYONE:
			break;
		case VST_COMPANY:
			if (_local_company != (CompanyID)p2) return CommandCost();
			break;
		case VST_CLIENT:
			if (_network_own_client_id != (ClientID)p2) return CommandCost();
			break;
		default:
			return CMD_ERROR;
	}

	if (flags & DC_EXEC) {
		ResetObjectToPlace();
		ScrollMainWindowToTile(tile);
	}
	return CommandCost();
}

static LineSnapPoint LineSnapPointAtRailTrackEndpoint(TileIndex tile, DiagDirection exit_dir, bool bidirectional)
{
	LineSnapPoint ret;
	ret.x = (TILE_SIZE / 2) * (uint)(2 * TileX(tile) + TileIndexDiffCByDiagDir(exit_dir).x + 1);
	ret.y = (TILE_SIZE / 2) * (uint)(2 * TileY(tile) + TileIndexDiffCByDiagDir(exit_dir).y + 1);

	ret.dirs = 0;
	SetBit(ret.dirs, DiagDirToDir(exit_dir));
	SetBit(ret.dirs, ChangeDir(DiagDirToDir(exit_dir), DIRDIFF_45LEFT));
	SetBit(ret.dirs, ChangeDir(DiagDirToDir(exit_dir), DIRDIFF_45RIGHT));
	if (bidirectional) ret.dirs |= ROR<uint8>(ret.dirs, DIRDIFF_REVERSE);

	return ret;
}

/**
 * Store the position of lastly built rail track; for highlighting purposes.
 *
 * In "polyline" highlighting mode, the stored end point will be used as a snapping point for new
 * tracks allowing to place multi-segment polylines.
 *
 * @param start_tile         tile where the track starts
 * @param end_tile           tile where the track ends
 * @param start_track        track piece on the start_tile
 * @param bidirectional_exit whether to allow to highlight next track in any direction; otherwise new track will have to fallow the stored one (usefull when placing tunnels and bridges)
 */
void StoreRailPlacementEndpoints(TileIndex start_tile, TileIndex end_tile, Track start_track, bool bidirectional_exit)
{
	if (start_tile != INVALID_TILE && end_tile != INVALID_TILE) {
		/* calculate trackdirs at booth ends of the track */
		Trackdir exit_trackdir_at_start = TrackToTrackdir(start_track);
		Trackdir exit_trackdir_at_end = ReverseTrackdir(TrackToTrackdir(start_track));
		if (start_tile != end_tile) { // multi-tile case
			/* determine proper direction (pointing outside of the track) */
			uint distance = DistanceManhattan(start_tile, end_tile);
			if (distance > DistanceManhattan(TileAddByDiagDir(start_tile, TrackdirToExitdir(exit_trackdir_at_start)), end_tile)) {
				Swap(exit_trackdir_at_start, exit_trackdir_at_end);
			}
			/* determine proper track on the end tile - switch between upper/lower or left/right based on the length */
			if (distance % 2 != 0) exit_trackdir_at_end = NextTrackdir(exit_trackdir_at_end);
		}

		LineSnapPoint snap_start = LineSnapPointAtRailTrackEndpoint(start_tile, TrackdirToExitdir(exit_trackdir_at_start), bidirectional_exit);
		LineSnapPoint snap_end = LineSnapPointAtRailTrackEndpoint(end_tile, TrackdirToExitdir(exit_trackdir_at_end), bidirectional_exit);
		/* Find if we already had these coordinates before. */
		bool had_start = false;
		bool had_end = false;
		for (const LineSnapPoint &snap : _rail_snap_points) {
			had_start |= (snap.x == snap_start.x && snap.y == snap_start.y);
			had_end |= (snap.x == snap_end.x && snap.y == snap_end.y);
		}
		/* Create new snap point set. */
		if (had_start && had_end) {
			/* just stop snaping, don't forget snap points */
			SetRailSnapMode(RSM_NO_SNAP);
		} else {
			/* include only new points */
			_rail_snap_points.clear();
			if (!had_start) _rail_snap_points.push_back(snap_start);
			if (!had_end) _rail_snap_points.push_back(snap_end);
			SetRailSnapMode(RSM_SNAP_TO_RAIL);
		}
	}
}

static void MarkCatchmentTilesDirty()
{
	if (_viewport_highlight_town != nullptr) {
		MarkWholeScreenDirty();
		return;
	}
	if (_viewport_highlight_station != nullptr) {
		if (_viewport_highlight_station->catchment_tiles.tile == INVALID_TILE) {
			MarkWholeScreenDirty();
			_viewport_highlight_station = nullptr;
		} else {
			BitmapTileIterator it(_viewport_highlight_station->catchment_tiles);
			for (TileIndex tile = it; tile != INVALID_TILE; tile = ++it) {
				MarkTileDirtyByTile(tile);
			}
		}
	}
}

bool CurrentlySnappingRailPlacement()
{
	return (_thd.place_mode & HT_POLY) && GetRailSnapMode() == RSM_SNAP_TO_RAIL;
}

static RailSnapMode GetRailSnapMode()
{
	if (_rail_snap_mode == RSM_SNAP_TO_TILE && _tile_snap_points.size() == 0) return RSM_NO_SNAP;
	if (_rail_snap_mode == RSM_SNAP_TO_RAIL && _rail_snap_points.size() == 0) return RSM_NO_SNAP;
	return _rail_snap_mode;
}

static void SetRailSnapMode(RailSnapMode mode)
{
	_rail_snap_mode = mode;

	if ((_thd.place_mode & HT_POLY) && (GetRailSnapMode() == RSM_NO_SNAP)) {
		SetTileSelectSize(1, 1);
	}
}

static TileIndex GetRailSnapTile()
{
	if (_tile_snap_points.size() == 0) return INVALID_TILE;
	return TileVirtXY(_tile_snap_points[DIAGDIR_NE].x, _tile_snap_points[DIAGDIR_NE].y);
}

static void SetRailSnapTile(TileIndex tile)
{
	_tile_snap_points.clear();
	if (tile == INVALID_TILE) return;

	for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
		_tile_snap_points.push_back(LineSnapPointAtRailTrackEndpoint(tile, dir, false));
		LineSnapPoint &point = _tile_snap_points.back();
		point.dirs = ROR<uint8>(point.dirs, DIRDIFF_REVERSE);
	}
}

void ResetRailPlacementSnapping()
{
	_rail_snap_mode = RSM_NO_SNAP;
	_tile_snap_points.clear();
	_rail_snap_points.clear();
	_current_snap_lock.x = -1;
}

/**
 * Select or deselect station for coverage area highlight.
 * Selecting a station will deselect a town.
 * @param *st Station in question
 * @param sel Select or deselect given station
 */
void SetViewportCatchmentStation(const Station *st, bool sel)
{
	if (_viewport_highlight_station != nullptr) SetWindowDirty(WC_STATION_VIEW, _viewport_highlight_station->index);
	if (_viewport_highlight_town != nullptr) SetWindowDirty(WC_TOWN_VIEW, _viewport_highlight_town->index);
	if (sel && _viewport_highlight_station != st) {
		MarkCatchmentTilesDirty();
		_viewport_highlight_station = st;
		_viewport_highlight_town = nullptr;
		MarkCatchmentTilesDirty();
	} else if (!sel && _viewport_highlight_station == st) {
		MarkCatchmentTilesDirty();
		_viewport_highlight_station = nullptr;
	}
	if (_viewport_highlight_station != nullptr) SetWindowDirty(WC_STATION_VIEW, _viewport_highlight_station->index);
}

/**
 * Select or deselect town for coverage area highlight.
 * Selecting a town will deselect a station.
 * @param *t Town in question
 * @param sel Select or deselect given town
 */
void SetViewportCatchmentTown(const Town *t, bool sel)
{
	if (_viewport_highlight_town != nullptr) SetWindowDirty(WC_TOWN_VIEW, _viewport_highlight_town->index);
	if (_viewport_highlight_station != nullptr) SetWindowDirty(WC_STATION_VIEW, _viewport_highlight_station->index);
	if (sel && _viewport_highlight_town != t) {
		_viewport_highlight_station = nullptr;
		_viewport_highlight_town = t;
		MarkWholeScreenDirty();
	} else if (!sel && _viewport_highlight_town == t) {
		_viewport_highlight_town = nullptr;
		MarkWholeScreenDirty();
	}
	if (_viewport_highlight_town != nullptr) SetWindowDirty(WC_TOWN_VIEW, _viewport_highlight_town->index);
}
