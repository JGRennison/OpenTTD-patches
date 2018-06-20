/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_gui.cpp Implementation of linkgraph overlay GUI. */

#include "../stdafx.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../company_base.h"
#include "../company_gui.h"
#include "../date_func.h"
#include "../viewport_func.h"
#include "../smallmap_gui.h"
#include "../zoom_func.h"
#include "../core/geometry_func.hpp"
#include "../widgets/link_graph_legend_widget.h"

#include "table/strings.h"

#include "../3rdparty/cpp-btree/btree_map.h"

#include <algorithm>

#include "../safeguards.h"

/**
 * Colours for the various "load" states of links. Ordered from "unused" to
 * "overloaded".
 */
const uint8 LinkGraphOverlay::LINK_COLOURS[] = {
	0x0f, 0xd1, 0xd0, 0x57,
	0x55, 0x53, 0xbf, 0xbd,
	0xba, 0xb9, 0xb7, 0xb5
};

/**
 * Get a DPI for the widget we will be drawing to.
 * @param dpi DrawPixelInfo to fill with the desired dimensions.
 */
void LinkGraphOverlay::GetWidgetDpi(DrawPixelInfo *dpi, uint margin) const
{
	const NWidgetBase *wi = this->window->GetWidget<NWidgetBase>(this->widget_id);
	dpi->left = dpi->top = -margin;
	dpi->width = wi->current_x + 2 * margin;
	dpi->height = wi->current_y + 2 * margin;
}

bool LinkGraphOverlay::CacheStillValid() const
{
	if (this->window->viewport) {
		const ViewPort *vp = this->window->viewport;
		Rect region { vp->virtual_left, vp->virtual_top,
				vp->virtual_left + vp->virtual_width, vp->virtual_top + vp->virtual_height };
		return (region.left >= this->cached_region.left &&
				region.right <= this->cached_region.right &&
				region.top >= this->cached_region.top &&
				region.bottom <= this->cached_region.bottom);
	} else {
		return true;
	}
}

/**
 * Rebuild the cache and recalculate which links and stations to be shown.
 */
void LinkGraphOverlay::RebuildCache(bool incremental)
{
	if (!incremental) {
		this->cached_links.clear();
		this->cached_stations.clear();
	}
	if (this->company_mask == 0) return;

	DrawPixelInfo dpi;
	bool cache_all = false;
	if (this->window->viewport) {
		const ViewPort *vp = this->window->viewport;
		const int pixel_margin = 256;
		const int vp_margin = ScaleByZoom(pixel_margin, vp->zoom);
		this->GetWidgetDpi(&dpi, pixel_margin);
		this->cached_region = Rect({ vp->virtual_left - vp_margin, vp->virtual_top - vp_margin,
				vp->virtual_left + vp->virtual_width + vp_margin, vp->virtual_top + vp->virtual_height + vp_margin });
	} else {
		this->GetWidgetDpi(&dpi);
		cache_all = true;
	}

	struct LinkCacheItem {
		Point from_pt;
		Point to_pt;
		LinkProperties prop;
	};
	btree::btree_map<std::pair<StationID, StationID>, LinkCacheItem> link_cache_map;
	std::vector<StationID> incremental_station_exclude;
	std::vector<std::pair<StationID, StationID>> incremental_link_exclude;

	if (incremental) {
		incremental_station_exclude.reserve(this->cached_stations.size());
		for (StationSupplyList::iterator i(this->cached_stations.begin()); i != this->cached_stations.end(); ++i) {
			incremental_station_exclude.push_back(i->id);
		}
		incremental_link_exclude.reserve(this->cached_links.size());
		for (LinkList::iterator i(this->cached_links.begin()); i != this->cached_links.end(); ++i) {
			incremental_link_exclude.push_back(std::make_pair(i->from_id, i->to_id));
		}
	}

	auto AddLinks = [&](const Station *from, const Station *to, Point from_pt, Point to_pt, btree::btree_map<std::pair<StationID, StationID>, LinkCacheItem>::iterator insert_iter) {
		LinkCacheItem *item = nullptr;
		CargoID c;
		FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
			if (!CargoSpec::Get(c)->IsValid()) continue;
			const GoodsEntry &ge = from->goods[c];
			if (!LinkGraph::IsValidID(ge.link_graph) ||
					ge.link_graph != to->goods[c].link_graph) {
				continue;
			}
			const LinkGraph &lg = *LinkGraph::Get(ge.link_graph);
			ConstEdge edge = lg[ge.node][to->goods[c].node];
			if (edge.Capacity() > 0) {
				if (!item) {
					auto iter = link_cache_map.insert(insert_iter, std::make_pair(std::make_pair(from->index, to->index), LinkCacheItem()));
					item = &(iter->second);
					item->from_pt = from_pt;
					item->to_pt = to_pt;
				}
				this->AddStats(lg.Monthly(edge.Capacity()), lg.Monthly(edge.Usage()),
						ge.flows.GetFlowVia(to->index), from->owner == OWNER_NONE || to->owner == OWNER_NONE,
						item->prop);
			}
		}
	};

	const size_t previous_cached_stations_count = this->cached_stations.size();
	const Station *sta;
	FOR_ALL_STATIONS(sta) {
		if (sta->rect.IsEmpty()) continue;

		if (incremental && std::binary_search(incremental_station_exclude.begin(), incremental_station_exclude.end(), sta->index)) continue;

		Point pta = this->GetStationMiddle(sta);

		StationID from = sta->index;

		uint supply = 0;
		CargoID c;
		FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
			if (!CargoSpec::Get(c)->IsValid()) continue;
			if (!LinkGraph::IsValidID(sta->goods[c].link_graph)) continue;
			const LinkGraph &lg = *LinkGraph::Get(sta->goods[c].link_graph);

			ConstNode from_node = lg[sta->goods[c].node];
			supply += lg.Monthly(from_node.Supply());
			for (ConstEdgeIterator i = from_node.Begin(); i != from_node.End(); ++i) {
				StationID to = lg[i->first].Station();
				assert(from != to);
				if (!Station::IsValidID(to)) continue;

				const Station *stb = Station::Get(to);
				assert(sta != stb);

				/* Show links between stations of selected companies or "neutral" ones like oilrigs. */
				if (stb->owner != OWNER_NONE && sta->owner != OWNER_NONE && !HasBit(this->company_mask, stb->owner)) continue;
				if (stb->rect.IsEmpty()) continue;

				if (incremental && std::binary_search(incremental_station_exclude.begin(), incremental_station_exclude.end(), to)) continue;
				if (incremental && std::binary_search(incremental_link_exclude.begin(), incremental_link_exclude.end(), std::make_pair(from, to))) continue;

				auto key = std::make_pair(from, to);
				auto iter = link_cache_map.lower_bound(key);
				if (iter != link_cache_map.end() && !(link_cache_map.key_comp()(key, iter->first))) {
					continue;
				}

				Point ptb = this->GetStationMiddle(stb);

				if (!cache_all && !this->IsLinkVisible(pta, ptb, &dpi)) continue;

				AddLinks(sta, stb, pta, ptb, iter);
			}
		}
		if (cache_all || this->IsPointVisible(pta, &dpi)) {
			this->cached_stations.push_back({ from, supply, pta });
		}
	}

	const size_t previous_cached_links_count = this->cached_links.size();
	this->cached_links.reserve(this->cached_links.size() + link_cache_map.size());
	for (auto &iter : link_cache_map) {
		this->cached_links.push_back({ iter.first.first, iter.first.second, iter.second.from_pt, iter.second.to_pt, iter.second.prop });
	}

	if (previous_cached_stations_count > 0) {
		std::inplace_merge(this->cached_stations.begin(), this->cached_stations.begin() + previous_cached_stations_count, this->cached_stations.end(),
				[](const StationSupplyInfo &a, const StationSupplyInfo &b) {
					return a.id < b.id;
				});
	}
	if (previous_cached_links_count > 0) {
		std::inplace_merge(this->cached_links.begin(), this->cached_links.begin() + previous_cached_links_count, this->cached_links.end(),
				[](const LinkInfo &a, const LinkInfo &b) {
					return std::make_pair(a.from_id, a.to_id) < std::make_pair(b.from_id, b.to_id);
				});
	}
}

/**
 * Determine if a certain point is inside the given DPI, with some lee way.
 * @param pt Point we are looking for.
 * @param dpi Visible area.
 * @param padding Extent of the point.
 * @return If the point or any of its 'extent' is inside the dpi.
 */
inline bool LinkGraphOverlay::IsPointVisible(Point pt, const DrawPixelInfo *dpi, int padding) const
{
	return pt.x > dpi->left - padding && pt.y > dpi->top - padding &&
			pt.x < dpi->left + dpi->width + padding &&
			pt.y < dpi->top + dpi->height + padding;
}

/**
 * Determine if a certain link crosses through the area given by the dpi with some lee way.
 * @param pta First end of the link.
 * @param ptb Second end of the link.
 * @param dpi Visible area.
 * @param padding Width or thickness of the link.
 * @return If the link or any of its "thickness" is visible. This may return false positives.
 */
inline bool LinkGraphOverlay::IsLinkVisible(Point pta, Point ptb, const DrawPixelInfo *dpi, int padding) const
{
	const int left = dpi->left - padding;
	const int right = dpi->left + dpi->width + padding;
	const int top = dpi->top - padding;
	const int bottom = dpi->top + dpi->height + padding;

	// Cut-down Cohen–Sutherland algorithm

	const unsigned char INSIDE = 0; // 0000
	const unsigned char LEFT   = 1; // 0001
	const unsigned char RIGHT  = 2; // 0010
	const unsigned char BOTTOM = 4; // 0100
	const unsigned char TOP    = 8; // 1000

	int x0 = pta.x;
	int y0 = pta.y;
	int x1 = ptb.x;
	int y1 = ptb.y;

	auto out_code = [&](int x, int y) -> unsigned char {
		unsigned char out = INSIDE;
		if (x < left) {
			out |= LEFT;
		} else if (x > right) {
			out |= RIGHT;
		}
		if (y < top) {
			out |= TOP;
		} else if (y > bottom) {
			out |= BOTTOM;
		}
		return out;
	};

	unsigned char c0 = out_code(x0, y0);
	unsigned char c1 = out_code(x1, y1);

	while (true) {
		if (c0 == 0 || c1 == 0) return true;
		if ((c0 & c1) != 0) return false;

		if (c0 & TOP) {           // point 0 is above the clip window
			x0 = x0 + (int)(((int64) (x1 - x0)) * ((int64) (top - y0)) / ((int64) (y1 - y0)));
			y0 = top;
		} else if (c0 & BOTTOM) { // point 0 is below the clip window
			x0 = x0 + (int)(((int64) (x1 - x0)) * ((int64) (bottom - y0)) / ((int64) (y1 - y0)));
			y0 = bottom;
		} else if (c0 & RIGHT) {  // point 0 is to the right of clip window
			y0 = y0 + (int)(((int64) (y1 - y0)) * ((int64) (right - x0)) / ((int64) (x1 - x0)));
			x0 = right;
		} else if (c0 & LEFT) {   // point 0 is to the left of clip window
			y0 = y0 + (int)(((int64) (y1 - y0)) * ((int64) (left - x0)) / ((int64) (x1 - x0)));
			x0 = left;
		}

		c0 = out_code(x0, y0);
	}

	NOT_REACHED();
}

/**
 * Add information from a given pair of link stat and flow stat to the given
 * link properties. The shown usage or plan is always the maximum of all link
 * stats involved.
 * @param new_cap Capacity of the new link.
 * @param new_usg Usage of the new link.
 * @param new_plan Planned flow for the new link.
 * @param new_shared If the new link is shared.
 * @param cargo LinkProperties to write the information to.
 */
/* static */ void LinkGraphOverlay::AddStats(uint new_cap, uint new_usg, uint new_plan, bool new_shared, LinkProperties &cargo)
{
	/* multiply the numbers by 32 in order to avoid comparing to 0 too often. */
	if (cargo.capacity == 0 ||
			max(cargo.usage, cargo.planned) * 32 / (cargo.capacity + 1) < max(new_usg, new_plan) * 32 / (new_cap + 1)) {
		cargo.capacity = new_cap;
		cargo.usage = new_usg;
		cargo.planned = new_plan;
	}
	if (new_shared) cargo.shared = true;
}

void LinkGraphOverlay::RefreshDrawCache()
{
	for (StationSupplyList::iterator i(this->cached_stations.begin()); i != this->cached_stations.end(); ++i) {
		const Station *st = Station::GetIfValid(i->id);
		if (st == NULL) continue;

		i->pt = this->GetStationMiddle(st);
	}
	for (LinkList::iterator i(this->cached_links.begin()); i != this->cached_links.end(); ++i) {
		const Station *sta = Station::GetIfValid(i->from_id);
		if (sta == NULL) continue;
		const Station *stb = Station::GetIfValid(i->to_id);
		if (stb == NULL) continue;

		i->from_pt = this->GetStationMiddle(sta);
		i->to_pt = this->GetStationMiddle(stb);
	}
}

/**
 * Draw the linkgraph overlay or some part of it, in the area given.
 * @param dpi Area to be drawn to.
 */
void LinkGraphOverlay::Draw(const DrawPixelInfo *dpi)
{
	if (this->last_update_number != GetWindowUpdateNumber()) {
		this->last_update_number = GetWindowUpdateNumber();
		this->RefreshDrawCache();
	}
	this->DrawLinks(dpi);
	this->DrawStationDots(dpi);
}

/**
 * Draw the cached links or part of them into the given area.
 * @param dpi Area to be drawn to.
 */
void LinkGraphOverlay::DrawLinks(const DrawPixelInfo *dpi) const
{
	for (LinkList::const_iterator i(this->cached_links.begin()); i != this->cached_links.end(); ++i) {
		if (!this->IsLinkVisible(i->from_pt, i->to_pt, dpi, this->scale + 2)) continue;
		if (!Station::IsValidID(i->from_id)) continue;
		if (!Station::IsValidID(i->to_id)) continue;
		this->DrawContent(i->from_pt, i->to_pt, i->prop);
	}
}

/**
 * Draw one specific link.
 * @param pta Source of the link.
 * @param ptb Destination of the link.
 * @param cargo Properties of the link.
 */
void LinkGraphOverlay::DrawContent(Point pta, Point ptb, const LinkProperties &cargo) const
{
	uint usage_or_plan = min(cargo.capacity * 2 + 1, max(cargo.usage, cargo.planned));
	int colour = LinkGraphOverlay::LINK_COLOURS[usage_or_plan * lengthof(LinkGraphOverlay::LINK_COLOURS) / (cargo.capacity * 2 + 2)];
	int dash = cargo.shared ? this->scale * 4 : 0;

	/* Move line a bit 90° against its dominant direction to prevent it from
	 * being hidden below the grey line. */
	int side = _settings_game.vehicle.road_side ? 1 : -1;
	if (abs(pta.x - ptb.x) < abs(pta.y - ptb.y)) {
		int offset_x = (pta.y > ptb.y ? 1 : -1) * side * this->scale;
		GfxDrawLine(pta.x + offset_x, pta.y, ptb.x + offset_x, ptb.y, colour, this->scale, dash);
	} else {
		int offset_y = (pta.x < ptb.x ? 1 : -1) * side * this->scale;
		GfxDrawLine(pta.x, pta.y + offset_y, ptb.x, ptb.y + offset_y, colour, this->scale, dash);
	}

	GfxDrawLine(pta.x, pta.y, ptb.x, ptb.y, _colour_gradient[COLOUR_GREY][1], this->scale);
}

/**
 * Draw dots for stations into the smallmap. The dots' sizes are determined by the amount of
 * cargo produced there, their colours by the type of cargo produced.
 */
void LinkGraphOverlay::DrawStationDots(const DrawPixelInfo *dpi) const
{
	for (StationSupplyList::const_iterator i(this->cached_stations.begin()); i != this->cached_stations.end(); ++i) {
		const Point &pt = i->pt;
		if (!this->IsPointVisible(pt, dpi, 3 * this->scale)) continue;

		const Station *st = Station::GetIfValid(i->id);
		if (st == NULL) continue;

		uint r = this->scale * 2 + this->scale * 2 * min(200, i->quantity) / 200;

		LinkGraphOverlay::DrawVertex(pt.x, pt.y, r,
				_colour_gradient[st->owner != OWNER_NONE ?
						(Colours)Company::Get(st->owner)->colour : COLOUR_GREY][5],
				_colour_gradient[COLOUR_GREY][1]);
	}
}

/**
 * Draw a square symbolizing a producer of cargo.
 * @param x X coordinate of the middle of the vertex.
 * @param y Y coordinate of the middle of the vertex.
 * @param size Y and y extend of the vertex.
 * @param colour Colour with which the vertex will be filled.
 * @param border_colour Colour for the border of the vertex.
 */
/* static */ void LinkGraphOverlay::DrawVertex(int x, int y, int size, int colour, int border_colour)
{
	size--;
	int w1 = size / 2;
	int w2 = size / 2 + size % 2;

	GfxFillRect(x - w1, y - w1, x + w2, y + w2, colour);

	w1++;
	w2++;
	GfxDrawLine(x - w1, y - w1, x + w2, y - w1, border_colour);
	GfxDrawLine(x - w1, y + w2, x + w2, y + w2, border_colour);
	GfxDrawLine(x - w1, y - w1, x - w1, y + w2, border_colour);
	GfxDrawLine(x + w2, y - w1, x + w2, y + w2, border_colour);
}

/**
 * Determine the middle of a station in the current window.
 * @param st The station we're looking for.
 * @return Middle point of the station in the current window.
 */
Point LinkGraphOverlay::GetStationMiddle(const Station *st) const
{
	if (this->window->viewport != NULL) {
		return GetViewportStationMiddle(this->window->viewport, st);
	} else {
		/* assume this is a smallmap */
		return static_cast<const SmallMapWindow *>(this->window)->GetStationMiddle(st);
	}
}

/**
 * Set a new cargo mask and rebuild the cache.
 * @param cargo_mask New cargo mask.
 */
void LinkGraphOverlay::SetCargoMask(CargoTypes cargo_mask)
{
	this->cargo_mask = cargo_mask;
	this->RebuildCache();
	this->window->GetWidget<NWidgetBase>(this->widget_id)->SetDirty(this->window);
}

/**
 * Set a new company mask and rebuild the cache.
 * @param company_mask New company mask.
 */
void LinkGraphOverlay::SetCompanyMask(uint32 company_mask)
{
	this->company_mask = company_mask;
	this->RebuildCache();
	this->window->GetWidget<NWidgetBase>(this->widget_id)->SetDirty(this->window);
}

/** Make a number of rows with buttons for each company for the linkgraph legend window. */
NWidgetBase *MakeCompanyButtonRowsLinkGraphGUI(int *biggest_index)
{
	return MakeCompanyButtonRows(biggest_index, WID_LGL_COMPANY_FIRST, WID_LGL_COMPANY_LAST, 3, 0);
}

NWidgetBase *MakeSaturationLegendLinkGraphGUI(int *biggest_index)
{
	NWidgetVertical *panel = new NWidgetVertical(NC_EQUALSIZE);
	for (uint i = 0; i < lengthof(LinkGraphOverlay::LINK_COLOURS); ++i) {
		NWidgetBackground * wid = new NWidgetBackground(WWT_PANEL, COLOUR_DARK_GREEN, i + WID_LGL_SATURATION_FIRST);
		wid->SetMinimalSize(50, FONT_HEIGHT_SMALL);
		wid->SetFill(1, 1);
		wid->SetResize(0, 0);
		panel->Add(wid);
	}
	*biggest_index = WID_LGL_SATURATION_LAST;
	return panel;
}

NWidgetBase *MakeCargoesLegendLinkGraphGUI(int *biggest_index)
{
	static const uint ENTRIES_PER_ROW = CeilDiv(NUM_CARGO, 5);
	NWidgetVertical *panel = new NWidgetVertical(NC_EQUALSIZE);
	NWidgetHorizontal *row = NULL;
	for (uint i = 0; i < NUM_CARGO; ++i) {
		if (i % ENTRIES_PER_ROW == 0) {
			if (row) panel->Add(row);
			row = new NWidgetHorizontal(NC_EQUALSIZE);
		}
		NWidgetBackground * wid = new NWidgetBackground(WWT_PANEL, COLOUR_GREY, i + WID_LGL_CARGO_FIRST);
		wid->SetMinimalSize(25, FONT_HEIGHT_SMALL);
		wid->SetFill(1, 1);
		wid->SetResize(0, 0);
		row->Add(wid);
	}
	/* Fill up last row */
	for (uint i = 0; i < 4 - (NUM_CARGO - 1) % 5; ++i) {
		NWidgetSpacer *spc = new NWidgetSpacer(25, FONT_HEIGHT_SMALL);
		spc->SetFill(1, 1);
		spc->SetResize(0, 0);
		row->Add(spc);
	}
	panel->Add(row);
	*biggest_index = WID_LGL_CARGO_LAST;
	return panel;
}


static const NWidgetPart _nested_linkgraph_legend_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_LGL_CAPTION), SetDataTip(STR_LINKGRAPH_LEGEND_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_LGL_SATURATION),
				SetPadding(WD_FRAMERECT_TOP, 0, WD_FRAMERECT_BOTTOM, WD_CAPTIONTEXT_LEFT),
				NWidgetFunction(MakeSaturationLegendLinkGraphGUI),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_LGL_COMPANIES),
				SetPadding(WD_FRAMERECT_TOP, 0, WD_FRAMERECT_BOTTOM, WD_CAPTIONTEXT_LEFT),
				NWidget(NWID_VERTICAL, NC_EQUALSIZE),
					NWidgetFunction(MakeCompanyButtonRowsLinkGraphGUI),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_LGL_COMPANIES_ALL), SetDataTip(STR_LINKGRAPH_LEGEND_ALL, STR_NULL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_LGL_COMPANIES_NONE), SetDataTip(STR_LINKGRAPH_LEGEND_NONE, STR_NULL),
				EndContainer(),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_LGL_CARGOES),
				SetPadding(WD_FRAMERECT_TOP, WD_FRAMERECT_RIGHT, WD_FRAMERECT_BOTTOM, WD_CAPTIONTEXT_LEFT),
				NWidget(NWID_VERTICAL, NC_EQUALSIZE),
					NWidgetFunction(MakeCargoesLegendLinkGraphGUI),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_LGL_CARGOES_ALL), SetDataTip(STR_LINKGRAPH_LEGEND_ALL, STR_NULL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_LGL_CARGOES_NONE), SetDataTip(STR_LINKGRAPH_LEGEND_NONE, STR_NULL),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

assert_compile(WID_LGL_SATURATION_LAST - WID_LGL_SATURATION_FIRST ==
		lengthof(LinkGraphOverlay::LINK_COLOURS) - 1);

static WindowDesc _linkgraph_legend_desc(
	WDP_AUTO, "toolbar_linkgraph", 0, 0,
	WC_LINKGRAPH_LEGEND, WC_NONE,
	0,
	_nested_linkgraph_legend_widgets, lengthof(_nested_linkgraph_legend_widgets)
);

/**
 * Open a link graph legend window.
 */
void ShowLinkGraphLegend()
{
	AllocateWindowDescFront<LinkGraphLegendWindow>(&_linkgraph_legend_desc, 0);
}

LinkGraphLegendWindow::LinkGraphLegendWindow(WindowDesc *desc, int window_number) : Window(desc)
{
	this->InitNested(window_number);
	this->InvalidateData(0);
	this->SetOverlay(FindWindowById(WC_MAIN_WINDOW, 0)->viewport->overlay);
}

/**
 * Set the overlay belonging to this menu and import its company/cargo settings.
 * @params overlay New overlay for this menu.
 */
void LinkGraphLegendWindow::SetOverlay(LinkGraphOverlay *overlay) {
	this->overlay = overlay;
	uint32 companies = this->overlay->GetCompanyMask();
	for (uint c = 0; c < MAX_COMPANIES; c++) {
		if (!this->IsWidgetDisabled(WID_LGL_COMPANY_FIRST + c)) {
			this->SetWidgetLoweredState(WID_LGL_COMPANY_FIRST + c, HasBit(companies, c));
		}
	}
	CargoTypes cargoes = this->overlay->GetCargoMask();
	for (uint c = 0; c < NUM_CARGO; c++) {
		if (!this->IsWidgetDisabled(WID_LGL_CARGO_FIRST + c)) {
			this->SetWidgetLoweredState(WID_LGL_CARGO_FIRST + c, HasBit(cargoes, c));
		}
	}
}

void LinkGraphLegendWindow::UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
{
	if (IsInsideMM(widget, WID_LGL_SATURATION_FIRST, WID_LGL_SATURATION_LAST + 1)) {
		StringID str = STR_NULL;
		if (widget == WID_LGL_SATURATION_FIRST) {
			str = STR_LINKGRAPH_LEGEND_UNUSED;
		} else if (widget == WID_LGL_SATURATION_LAST) {
			str = STR_LINKGRAPH_LEGEND_OVERLOADED;
		} else if (widget == (WID_LGL_SATURATION_LAST + WID_LGL_SATURATION_FIRST) / 2) {
			str = STR_LINKGRAPH_LEGEND_SATURATED;
		}
		if (str != STR_NULL) {
			Dimension dim = GetStringBoundingBox(str);
			dim.width += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
			dim.height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
			*size = maxdim(*size, dim);
		}
	}
	if (IsInsideMM(widget, WID_LGL_CARGO_FIRST, WID_LGL_CARGO_LAST + 1)) {
		CargoSpec *cargo = CargoSpec::Get(widget - WID_LGL_CARGO_FIRST);
		if (cargo->IsValid()) {
			Dimension dim = GetStringBoundingBox(cargo->abbrev);
			dim.width += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
			dim.height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
			*size = maxdim(*size, dim);
		}
	}
}

void LinkGraphLegendWindow::DrawWidget(const Rect &r, int widget) const
{
	if (IsInsideMM(widget, WID_LGL_COMPANY_FIRST, WID_LGL_COMPANY_LAST + 1)) {
		if (this->IsWidgetDisabled(widget)) return;
		CompanyID cid = (CompanyID)(widget - WID_LGL_COMPANY_FIRST);
		Dimension sprite_size = GetSpriteSize(SPR_COMPANY_ICON);
		DrawCompanyIcon(cid, (r.left + r.right + 1 - sprite_size.width) / 2, (r.top + r.bottom + 1 - sprite_size.height) / 2);
	}
	if (IsInsideMM(widget, WID_LGL_SATURATION_FIRST, WID_LGL_SATURATION_LAST + 1)) {
		GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, LinkGraphOverlay::LINK_COLOURS[widget - WID_LGL_SATURATION_FIRST]);
		StringID str = STR_NULL;
		if (widget == WID_LGL_SATURATION_FIRST) {
			str = STR_LINKGRAPH_LEGEND_UNUSED;
		} else if (widget == WID_LGL_SATURATION_LAST) {
			str = STR_LINKGRAPH_LEGEND_OVERLOADED;
		} else if (widget == (WID_LGL_SATURATION_LAST + WID_LGL_SATURATION_FIRST) / 2) {
			str = STR_LINKGRAPH_LEGEND_SATURATED;
		}
		if (str != STR_NULL) DrawString(r.left, r.right, (r.top + r.bottom + 1 - FONT_HEIGHT_SMALL) / 2, str, TC_FROMSTRING, SA_HOR_CENTER);
	}
	if (IsInsideMM(widget, WID_LGL_CARGO_FIRST, WID_LGL_CARGO_LAST + 1)) {
		if (this->IsWidgetDisabled(widget)) return;
		CargoSpec *cargo = CargoSpec::Get(widget - WID_LGL_CARGO_FIRST);
		GfxFillRect(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2, cargo->legend_colour);
		DrawString(r.left, r.right, (r.top + r.bottom + 1 - FONT_HEIGHT_SMALL) / 2, cargo->abbrev, GetContrastColour(cargo->legend_colour, 42), SA_HOR_CENTER);
	}
}

void LinkGraphLegendWindow::OnHover(Point pt, int widget)
{
	if (IsInsideMM(widget, WID_LGL_COMPANY_FIRST, WID_LGL_COMPANY_LAST + 1)) {
		uint64 params[5];
		if (this->IsWidgetDisabled(widget)) {
			GuiShowTooltips(this, STR_LINKGRAPH_LEGEND_SELECT_COMPANIES, 0, params, TCC_HOVER);
		} else {
			CompanyID cid = (CompanyID)(widget - WID_LGL_COMPANY_FIRST);
			params[0] = STR_LINKGRAPH_LEGEND_SELECT_COMPANIES;
			params[1] = cid;
			GuiShowTooltips(this, STR_LINKGRAPH_LEGEND_COMPANY_TOOLTIP, 2, params, TCC_HOVER);
		}
	}
	if (IsInsideMM(widget, WID_LGL_CARGO_FIRST, WID_LGL_CARGO_LAST + 1)) {
		if (this->IsWidgetDisabled(widget)) return;
		CargoSpec *cargo = CargoSpec::Get(widget - WID_LGL_CARGO_FIRST);
		uint64 params[5];
		params[0] = cargo->name;
		GuiShowTooltips(this, STR_BLACK_STRING, 1, params, TCC_HOVER);
	}
}

/**
 * Update the overlay with the new company selection.
 */
void LinkGraphLegendWindow::UpdateOverlayCompanies()
{
	uint32 mask = 0;
	for (uint c = 0; c < MAX_COMPANIES; c++) {
		if (this->IsWidgetDisabled(c + WID_LGL_COMPANY_FIRST)) continue;
		if (!this->IsWidgetLowered(c + WID_LGL_COMPANY_FIRST)) continue;
		SetBit(mask, c);
	}
	this->overlay->SetCompanyMask(mask);
}

/**
 * Update the overlay with the new cargo selection.
 */
void LinkGraphLegendWindow::UpdateOverlayCargoes()
{
	CargoTypes mask = 0;
	for (uint c = 0; c < NUM_CARGO; c++) {
		if (this->IsWidgetDisabled(c + WID_LGL_CARGO_FIRST)) continue;
		if (!this->IsWidgetLowered(c + WID_LGL_CARGO_FIRST)) continue;
		SetBit(mask, c);
	}
	this->overlay->SetCargoMask(mask);
}

void LinkGraphLegendWindow::OnClick(Point pt, int widget, int click_count)
{
	/* Check which button is clicked */
	if (IsInsideMM(widget, WID_LGL_COMPANY_FIRST, WID_LGL_COMPANY_LAST + 1)) {
		if (!this->IsWidgetDisabled(widget)) {
			this->ToggleWidgetLoweredState(widget);
			this->UpdateOverlayCompanies();
		}
	} else if (widget == WID_LGL_COMPANIES_ALL || widget == WID_LGL_COMPANIES_NONE) {
		for (uint c = 0; c < MAX_COMPANIES; c++) {
			if (this->IsWidgetDisabled(c + WID_LGL_COMPANY_FIRST)) continue;
			this->SetWidgetLoweredState(WID_LGL_COMPANY_FIRST + c, widget == WID_LGL_COMPANIES_ALL);
		}
		this->UpdateOverlayCompanies();
		this->SetDirty();
	} else if (IsInsideMM(widget, WID_LGL_CARGO_FIRST, WID_LGL_CARGO_LAST + 1)) {
		if (!this->IsWidgetDisabled(widget)) {
			this->ToggleWidgetLoweredState(widget);
			this->UpdateOverlayCargoes();
		}
	} else if (widget == WID_LGL_CARGOES_ALL || widget == WID_LGL_CARGOES_NONE) {
		for (uint c = 0; c < NUM_CARGO; c++) {
			if (this->IsWidgetDisabled(c + WID_LGL_CARGO_FIRST)) continue;
			this->SetWidgetLoweredState(WID_LGL_CARGO_FIRST + c, widget == WID_LGL_CARGOES_ALL);
		}
		this->UpdateOverlayCargoes();
	}
	this->SetDirty();
}

/**
 * Invalidate the data of this window if the cargoes or companies have changed.
 * @param data ignored
 * @param gui_scope ignored
 */
void LinkGraphLegendWindow::OnInvalidateData(int data, bool gui_scope)
{
	/* Disable the companies who are not active */
	for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) {
		this->SetWidgetDisabledState(i + WID_LGL_COMPANY_FIRST, !Company::IsValidID(i));
	}
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		this->SetWidgetDisabledState(i + WID_LGL_CARGO_FIRST, !CargoSpec::Get(i)->IsValid());
	}
}
