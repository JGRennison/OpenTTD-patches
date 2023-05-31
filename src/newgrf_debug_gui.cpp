/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_debug_gui.cpp GUIs for debugging NewGRFs. */

#include "stdafx.h"
#include <stdarg.h>
#include <functional>
#include "core/backup_type.hpp"
#include "window_gui.h"
#include "window_func.h"
#include "random_access_file_type.h"
#include "spritecache.h"
#include "string_func.h"
#include "strings_func.h"
#include "textbuf_gui.h"
#include "vehicle_gui.h"
#include "zoom_func.h"
#include "scope.h"
#include "debug_settings.h"

#include "engine_base.h"
#include "industry.h"
#include "object_base.h"
#include "station_base.h"
#include "town.h"
#include "vehicle_base.h"
#include "train.h"
#include "roadveh.h"

#include "newgrf_airporttiles.h"
#include "newgrf_debug.h"
#include "newgrf_object.h"
#include "newgrf_spritegroup.h"
#include "newgrf_station.h"
#include "newgrf_town.h"
#include "newgrf_railtype.h"
#include "newgrf_industries.h"
#include "newgrf_industrytiles.h"

#include "newgrf_config.h"

#include "widgets/newgrf_debug_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** The sprite picker. */
NewGrfDebugSpritePicker _newgrf_debug_sprite_picker = { SPM_NONE, nullptr, std::vector<SpriteID>() };

/**
 * Get the feature index related to the window number.
 * @param window_number The window to get the feature index from.
 * @return the feature index
 */
static inline uint GetFeatureIndex(uint window_number)
{
	return GB(window_number, 0, 27);
}

/**
 * Get the window number for the inspect window given a
 * feature and index.
 * @param feature The feature we want to inspect.
 * @param index   The index/identifier of the feature to inspect.
 * @return the InspectWindow (Window)Number
 */
static inline uint GetInspectWindowNumber(GrfSpecFeature feature, uint index)
{
	assert((index >> 27) == 0);
	return (feature << 27) | index;
}

/**
 * The type of a property to show. This is used to
 * provide an appropriate representation in the GUI.
 */
enum NIType {
	NIT_INT,   ///< The property is a simple integer
	NIT_CARGO, ///< The property is a cargo
};

/** Representation of the data from a NewGRF property. */
struct NIProperty {
	const char *name;       ///< A (human readable) name for the property
	ptrdiff_t offset;       ///< Offset of the variable in the class
	byte read_size;         ///< Number of bytes (i.e. byte, word, dword etc)
	byte prop;              ///< The number of the property
	byte type;
};


/**
 * Representation of the available callbacks with
 * information on when they actually apply.
 */
struct NICallback {
	const char *name; ///< The human readable name of the callback
	ptrdiff_t offset; ///< Offset of the variable in the class
	byte read_size;   ///< The number of bytes (i.e. byte, word, dword etc) to read
	byte cb_bit;      ///< The bit that needs to be set for this callback to be enabled
	uint16 cb_id;     ///< The number of the callback
};
/** Mask to show no bit needs to be enabled for the callback. */
static const int CBM_NO_BIT = UINT8_MAX;

/** Representation on the NewGRF variables. */
struct NIVariable {
	const char *name;
	uint16 var;
};

struct NIExtraInfoOutput {
	std::function<void(const char *)> print;
	std::function<void(uint)> register_next_line_click_flag_toggle;
	uint32 flags;
};

/** Helper class to wrap some functionality/queries in. */
class NIHelper {
public:
	/** Silence a warning. */
	virtual ~NIHelper() {}

	/**
	 * Is the item with the given index inspectable?
	 * @param index the index to check.
	 * @return true iff the index is inspectable.
	 */
	virtual bool IsInspectable(uint index) const = 0;

	/**
	 * Get the parent "window_number" of a given instance.
	 * @param index the instance to get the parent for.
	 * @return the parent's window_number or UINT32_MAX if there is none.
	 */
	virtual uint GetParent(uint index) const = 0;

	/**
	 * Get the instance given an index.
	 * @param index the index to get the instance for.
	 * @return the instance.
	 */
	virtual const void *GetInstance(uint index) const = 0;

	/**
	 * Get (NewGRF) specs given an index.
	 * @param index the index to get the specs for for.
	 * @return the specs.
	 */
	virtual const void *GetSpec(uint index) const = 0;

	/**
	 * Set the string parameters to write the right data for a STRINGn.
	 * @param index the index to get the string parameters for.
	 */
	virtual void SetStringParameters(uint index) const = 0;

	/**
	 * Get the GRFID of the file that includes this item.
	 * @param index index to check.
	 * @return GRFID of the item. 0 means that the item is not inspectable.
	 */
	virtual uint32 GetGRFID(uint index) const = 0;

	/**
	 * Resolve (action2) variable for a given index.
	 * @param index The (instance) index to resolve the variable for.
	 * @param var   The variable to actually resolve.
	 * @param param The varaction2 0x60+x parameter to pass.
	 * @param avail Return whether the variable is available.
	 * @return The resolved variable's value.
	 */
	virtual uint Resolve(uint index, uint var, uint param, GetVariableExtra *extra) const = 0;

	/**
	 * Used to decide if the PSA needs a parameter or not.
	 * @return True iff this item has a PSA that requires a parameter.
	 */
	virtual bool PSAWithParameter() const
	{
		return false;
	}

	/**
	 * Allows to know the size of the persistent storage.
	 * @param index Index of the item.
	 * @param grfid Parameter for the PSA. Only required for items with parameters.
	 * @return Size of the persistent storage in indices.
	 */
	virtual uint GetPSASize(uint index, uint32 grfid) const
	{
		return 0;
	}

	/**
	 * Gets the first position of the array containing the persistent storage.
	 * @param index Index of the item.
	 * @param grfid Parameter for the PSA. Only required for items with parameters.
	 * @return Pointer to the first position of the storage array or nullptr if not present.
	 */
	virtual const int32 *GetPSAFirstPosition(uint index, uint32 grfid) const
	{
		return nullptr;
	}

	virtual std::vector<uint32> GetPSAGRFIDs(uint index) const
	{
		return {};
	}

	virtual void ExtraInfo(uint index, NIExtraInfoOutput &output) const {}
	virtual void SpriteDump(uint index, DumpSpriteGroupPrinter print) const {}
	virtual bool ShowExtraInfoOnly(uint index) const { return false; };
	virtual bool ShowExtraInfoIncludingGRFIDOnly(uint index) const { return false; };
	virtual bool ShowSpriteDumpButton(uint index) const { return false; };

protected:
	/**
	 * Helper to make setting the strings easier.
	 * @param string the string to actually draw.
	 * @param index  the (instance) index for the string.
	 */
	void SetSimpleStringParameters(StringID string, uint32 index) const
	{
		SetDParam(0, string);
		SetDParam(1, index);
	}


	/**
	 * Helper to make setting the strings easier for objects at a specific tile.
	 * @param string the string to draw the object's name
	 * @param index  the (instance) index for the string.
	 * @param tile   the tile the object is at
	 */
	void SetObjectAtStringParameters(StringID string, uint32 index, TileIndex tile) const
	{
		SetDParam(0, STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT);
		SetDParam(1, string);
		SetDParam(2, index);
		SetDParam(3, tile);
	}
};


/** Container for all information for a given feature. */
struct NIFeature {
	const NIProperty *properties; ///< The properties associated with this feature.
	const NICallback *callbacks;  ///< The callbacks associated with this feature.
	const NIVariable *variables;  ///< The variables associated with this feature.
	const NIHelper   *helper;     ///< The class container all helper functions.
};

/* Load all the NewGRF debug data; externalised as it is just a huge bunch of tables. */
#include "table/newgrf_debug_data.h"

/**
 * Get the feature number related to the window number.
 * @param window_number The window to get the feature number for.
 * @return The feature number.
 */
static inline GrfSpecFeature GetFeatureNum(uint window_number)
{
	return (GrfSpecFeature)GB(window_number, 27, 5);
}

/**
 * Get the NIFeature related to the window number.
 * @param window_number The window to get the NIFeature for.
 * @return the NIFeature, or nullptr is there isn't one.
 */
static inline const NIFeature *GetFeature(uint window_number)
{
	GrfSpecFeature idx = GetFeatureNum(window_number);
	return idx < GSF_FAKE_END ? _nifeatures[idx] : nullptr;
}

/**
 * Get the NIHelper related to the window number.
 * @param window_number The window to get the NIHelper for.
 * @pre GetFeature(window_number) != nullptr
 * @return the NIHelper
 */
static inline const NIHelper *GetFeatureHelper(uint window_number)
{
	return GetFeature(window_number)->helper;
}

/** Window used for inspecting NewGRFs. */
struct NewGRFInspectWindow : Window {
	/** The value for the variable 60 parameters. */
	static uint32 var60params[GSF_FAKE_END][0x20];

	/** GRFID of the caller of this window, 0 if it has no caller. */
	uint32 caller_grfid;

	/** For ground vehicles: Index in vehicle chain. */
	uint chain_index;

	/** The currently edited parameter, to update the right one. */
	byte current_edit_param;

	Scrollbar *vscroll;

	int first_variable_line_index = 0;
	bool redraw_panel = false;
	bool redraw_scrollbar = false;

	bool auto_refresh = false;
	bool log_console = false;
	bool sprite_dump = false;
	bool sprite_dump_unopt = false;

	uint32 extra_info_flags = 0;
	btree::btree_map<int, uint> extra_info_click_flag_toggles;
	btree::btree_map<int, const SpriteGroup *> sprite_group_lines;
	btree::btree_map<int, uint16> nfo_line_lines;
	const SpriteGroup *selected_sprite_group = nullptr;
	btree::btree_map<int, uint32> highlight_tag_lines;
	uint32 selected_highlight_tags[6] = {};
	btree::btree_set<const SpriteGroup *> collapsed_groups;

	/**
	 * Check whether the given variable has a parameter.
	 * @param variable the variable to check.
	 * @return true iff the variable has a parameter.
	 */
	static bool HasVariableParameter(uint variable)
	{
		return IsInsideBS(variable, 0x60, 0x20);
	}

	/**
	 * Set the GRFID of the item opening this window.
	 * @param grfid GRFID of the item opening this window, or 0 if not opened by other window.
	 */
	void SetCallerGRFID(uint32 grfid)
	{
		this->caller_grfid = grfid;
		this->SetDirty();
	}

	/**
	 * Check whether this feature has chain index, i.e. refers to ground vehicles.
	 */
	bool HasChainIndex() const
	{
		GrfSpecFeature f = GetFeatureNum(this->window_number);
		return f == GSF_TRAINS || f == GSF_ROADVEHICLES || f == GSF_SHIPS;
	}

	/**
	 * Get the feature index.
	 * @return the feature index
	 */
	uint GetFeatureIndex() const
	{
		uint index = ::GetFeatureIndex(this->window_number);
		if (this->chain_index > 0) {
			assert(this->HasChainIndex());
			const Vehicle *v = Vehicle::Get(index);
			v = v->Move(this->chain_index);
			if (v != nullptr) index = v->index;
		}
		return index;
	}

	/**
	 * Ensure that this->chain_index is in range.
	 */
	void ValidateChainIndex()
	{
		if (this->chain_index == 0) return;

		assert(this->HasChainIndex());

		const Vehicle *v = Vehicle::Get(::GetFeatureIndex(this->window_number));
		v = v->Move(this->chain_index);
		if (v == nullptr) this->chain_index = 0;
	}

	NewGRFInspectWindow(WindowDesc *desc, WindowNumber wno) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_NGRFI_SCROLLBAR);
		bool show_sprite_dump_button = GetFeatureHelper(wno)->ShowSpriteDumpButton(::GetFeatureIndex(wno));
		this->GetWidget<NWidgetStacked>(WID_NGRFI_SPRITE_DUMP_SEL)->SetDisplayedPlane(show_sprite_dump_button ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_NGRFI_SPRITE_DUMP_UNOPT_SEL)->SetDisplayedPlane(show_sprite_dump_button ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_NGRFI_SPRITE_DUMP_GOTO_SEL)->SetDisplayedPlane(show_sprite_dump_button ? 0 : SZSP_NONE);
		this->SetWidgetDisabledState(WID_NGRFI_SPRITE_DUMP_UNOPT, true);
		this->SetWidgetDisabledState(WID_NGRFI_SPRITE_DUMP_GOTO, true);
		this->FinishInitNested(wno);

		this->vscroll->SetCount(0);
		this->SetWidgetDisabledState(WID_NGRFI_PARENT, GetFeatureHelper(this->window_number)->GetParent(this->GetFeatureIndex()) == UINT32_MAX);

		this->OnInvalidateData(0, true);
	}

	void SetStringParameters(int widget) const override
	{
		if (widget != WID_NGRFI_CAPTION) return;

		GetFeatureHelper(this->window_number)->SetStringParameters(this->GetFeatureIndex());
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_NGRFI_VEH_CHAIN: {
				assert(this->HasChainIndex());
				GrfSpecFeature f = GetFeatureNum(this->window_number);
				if (f == GSF_SHIPS) {
					size->height = FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.framerect.Vertical();
					break;
				}
				size->height = std::max(size->height, GetVehicleImageCellSize((VehicleType)(VEH_TRAIN + (f - GSF_TRAINS)), EIT_IN_DEPOT).height + 2 + WidgetDimensions::scaled.bevel.Vertical());
				break;
			}

			case WID_NGRFI_MAINPANEL:
				resize->height = std::max(11, FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal);
				resize->width  = 1;

				size->height = 5 * resize->height + WidgetDimensions::scaled.frametext.Vertical();
				break;
		}
	}

	/**
	 * Helper function to draw a string (line) in the window.
	 * @param r      The (screen) rectangle we must draw within
	 * @param offset The offset (in lines) we want to draw for
	 * @param format The format string
	 */
	void WARN_FORMAT(4, 5) DrawString(const Rect &r, int offset, const char *format, ...) const
	{
		char buf[1024];

		va_list va;
		va_start(va, format);
		vseprintf(buf, lastof(buf), format, va);
		va_end(va);

		if (this->log_console) DEBUG(misc, 0, "  %s", buf);

		offset -= this->vscroll->GetPosition();
		if (offset < 0 || offset >= this->vscroll->GetCapacity()) return;

		::DrawString(r.Shrink(WidgetDimensions::scaled.frametext).Shrink(0, offset * this->resize.step_height, 0, 0), buf, TC_BLACK);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_NGRFI_VEH_CHAIN: {
				const Vehicle *v = Vehicle::Get(this->GetFeatureIndex());
				if (GetFeatureNum(this->window_number) == GSF_SHIPS) {
					Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
					char buffer[64];
					uint count = 0;
					for (const Vehicle *u = v->First(); u != nullptr; u = u->Next()) count++;
					seprintf(buffer, lastof(buffer), "Part %u of %u", this->chain_index + 1, count);
					::DrawString(ir.left, ir.right, ir.top, buffer, TC_BLACK);
					break;
				}
				int total_width = 0;
				int sel_start = 0;
				int sel_end = 0;
				for (const Vehicle *u = v->First(); u != nullptr; u = u->Next()) {
					if (u == v) sel_start = total_width;
					switch (u->type) {
						case VEH_TRAIN: total_width += Train      ::From(u)->GetDisplayImageWidth(); break;
						case VEH_ROAD:  total_width += RoadVehicle::From(u)->GetDisplayImageWidth(); break;
						default: NOT_REACHED();
					}
					if (u == v) sel_end = total_width;
				}

				Rect br = r.Shrink(WidgetDimensions::scaled.bevel);
				int width = br.Width();
				int skip = 0;
				if (total_width > width) {
					int sel_center = (sel_start + sel_end) / 2;
					if (sel_center > width / 2) skip = std::min(total_width - width, sel_center - width / 2);
				}

				GrfSpecFeature f = GetFeatureNum(this->window_number);
				int h = GetVehicleImageCellSize((VehicleType)(VEH_TRAIN + (f - GSF_TRAINS)), EIT_IN_DEPOT).height;
				int y = CenterBounds(br.top, br.bottom, h);
				DrawVehicleImage(v->First(), br, INVALID_VEHICLE, EIT_IN_DETAILS, skip);

				/* Highlight the articulated part (this is different to the whole-vehicle highlighting of DrawVehicleImage */
				if (_current_text_dir == TD_RTL) {
					DrawFrameRect(r.right - sel_end   + skip, y, r.right - sel_start + skip, y + h, COLOUR_WHITE, FR_BORDERONLY);
				} else {
					DrawFrameRect(r.left  + sel_start - skip, y, r.left  + sel_end   - skip, y + h, COLOUR_WHITE, FR_BORDERONLY);
				}
				break;
			}
		}

		if (widget != WID_NGRFI_MAINPANEL) return;

		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);

		if (this->log_console) {
			GetFeatureHelper(this->window_number)->SetStringParameters(this->GetFeatureIndex());
			char buf[1024];
			GetString(buf, STR_NEWGRF_INSPECT_CAPTION, lastof(buf));
			DEBUG(misc, 0, "*** %s ***", buf + Utf8EncodedCharLen(buf[0]));
		}

		uint index = this->GetFeatureIndex();
		const NIFeature *nif  = GetFeature(this->window_number);
		const NIHelper *nih   = nif->helper;
		const void *base      = nih->GetInstance(index);
		const void *base_spec = nih->GetSpec(index);

		uint i = 0;

		auto guard = scope_guard([&]() {
			if (this->log_console) {
				const_cast<NewGRFInspectWindow*>(this)->log_console = false;
				DEBUG(misc, 0, "*** END ***");
			}

			uint count = std::min<uint>(UINT16_MAX, i);
			if (vscroll->GetCount() != count) {
				/* Not nice and certainly a hack, but it beats duplicating
				 * this whole function just to count the actual number of
				 * elements. Especially because they need to be redrawn. */
				uint position = this->vscroll->GetPosition();
				const_cast<NewGRFInspectWindow*>(this)->vscroll->SetCount(count);
				const_cast<NewGRFInspectWindow*>(this)->redraw_scrollbar = true;
				if (position != this->vscroll->GetPosition()) {
					const_cast<NewGRFInspectWindow*>(this)->redraw_panel = true;
				}
			}
		});

		auto line_handler = [&](const char *buf) {
			if (this->log_console) DEBUG(misc, 0, "  %s", buf);

			int offset = i++;
			offset -= this->vscroll->GetPosition();
			if (offset < 0 || offset >= this->vscroll->GetCapacity()) return;

			::DrawString(ir.left, ir.right, ir.top + (offset * this->resize.step_height), buf, TC_BLACK);
		};
		const_cast<NewGRFInspectWindow *>(this)->sprite_group_lines.clear();
		const_cast<NewGRFInspectWindow *>(this)->highlight_tag_lines.clear();
		const_cast<NewGRFInspectWindow *>(this)->nfo_line_lines.clear();
		if (this->sprite_dump) {
			SpriteGroupDumper::use_shadows = this->sprite_dump_unopt;
			bool collapsed = false;
			const SpriteGroup *collapse_group = nullptr;
			uint collapse_lines = 0;
			char tmp_buf[256];
			nih->SpriteDump(index, [&](const SpriteGroup *group, DumpSpriteGroupPrintOp operation, uint32 highlight_tag, const char *buf) {
				if (this->log_console && operation == DSGPO_PRINT) DEBUG(misc, 0, "  %s", buf);

				if (operation == DSGPO_NFO_LINE) {
					btree::btree_map<int, uint16> &lines = const_cast<NewGRFInspectWindow *>(this)->nfo_line_lines;
					auto iter = lines.lower_bound(highlight_tag);
					if (iter != lines.end() && iter->first == (int)highlight_tag) {
						/* Already stored, don't insert again */
					} else {
						lines.insert(iter, std::make_pair<int, uint16>(highlight_tag, std::min<uint>(UINT16_MAX, i)));
					}
				}

				if (operation == DSGPO_START && !collapsed && this->collapsed_groups.count(group)) {
					collapsed = true;
					collapse_group = group;
					collapse_lines = 0;
				}
				if (operation == DSGPO_END && collapsed && collapse_group == group) {
					seprintf(tmp_buf, lastof(tmp_buf), "%sCOLLAPSED: %u lines omitted", buf, collapse_lines);
					buf = tmp_buf;
					collapsed = false;
					highlight_tag = 0;
					operation = DSGPO_PRINT;
				}

				if (operation != DSGPO_PRINT) return;
				if (collapsed) {
					collapse_lines++;
					return;
				}

				int offset = i++;
				int scroll_offset = offset - this->vscroll->GetPosition();
				if (scroll_offset < 0 || scroll_offset >= this->vscroll->GetCapacity()) return;

				if (group != nullptr) const_cast<NewGRFInspectWindow *>(this)->sprite_group_lines[offset] = group;
				if (highlight_tag != 0) const_cast<NewGRFInspectWindow *>(this)->highlight_tag_lines[offset] = highlight_tag;

				TextColour colour = (this->selected_sprite_group == group && group != nullptr) ? TC_LIGHT_BLUE : TC_BLACK;
				if (highlight_tag != 0) {
					for (uint i = 0; i < lengthof(this->selected_highlight_tags); i++) {
						if (this->selected_highlight_tags[i] == highlight_tag) {
							static const TextColour text_colours[] = { TC_YELLOW, TC_GREEN, TC_ORANGE, TC_CREAM, TC_BROWN, TC_RED };
							static_assert(lengthof(this->selected_highlight_tags) == lengthof(text_colours));
							colour = text_colours[i];
							break;
						}
					}
				}
				::DrawString(ir.left, ir.right, ir.top + (scroll_offset * this->resize.step_height), buf, colour);
			});
			SpriteGroupDumper::use_shadows = false;
			return;
		} else {
			NewGRFInspectWindow *this_mutable = const_cast<NewGRFInspectWindow *>(this);
			this_mutable->extra_info_click_flag_toggles.clear();
			auto register_next_line_click_flag_toggle = [this_mutable, &i](uint flag) {
				this_mutable->extra_info_click_flag_toggles[i] = flag;
			};
			NIExtraInfoOutput output { line_handler, register_next_line_click_flag_toggle, this->extra_info_flags };
			nih->ExtraInfo(index, output);
		}

		if (nih->ShowExtraInfoOnly(index)) return;

		uint32 grfid = nih->GetGRFID(index);
		if (grfid) {
			this->DrawString(r, i++, "GRF:");
			this->DrawString(r, i++, "  ID: %08X", BSWAP32(grfid));
			GRFConfig *grfconfig = GetGRFConfig(grfid);
			if (grfconfig) {
				this->DrawString(r, i++, "  Name: %s", grfconfig->GetName());
				this->DrawString(r, i++, "  File: %s", grfconfig->filename.c_str());
			}
		}

		if (nih->ShowExtraInfoIncludingGRFIDOnly(index)) return;

		const_cast<NewGRFInspectWindow*>(this)->first_variable_line_index = i;

		if (nif->variables != nullptr) {
			this->DrawString(r, i++, "Variables:");
			uint prefix_width = 0;
			for (const NIVariable *niv = nif->variables; niv->name != nullptr; niv++) {
				if (niv->var >= 0x100) {
					extern const GRFVariableMapDefinition _grf_action2_remappable_variables[];
					for (const GRFVariableMapDefinition *info = _grf_action2_remappable_variables; info->name != nullptr; info++) {
						if (niv->var == info->id) {
							char buf[512];
							seprintf(buf, lastof(buf), "  %s: ", info->name);
							prefix_width = std::max<uint>(prefix_width, GetStringBoundingBox(buf).width);
							break;
						}
					}
				}
			}
			for (const NIVariable *niv = nif->variables; niv->name != nullptr; niv++) {
				GetVariableExtra extra;
				uint param = HasVariableParameter(niv->var) ? NewGRFInspectWindow::var60params[GetFeatureNum(this->window_number)][niv->var - 0x60] : 0;
				uint value = nih->Resolve(index, niv->var, param, &extra);

				if (!extra.available) continue;

				if (HasVariableParameter(niv->var)) {
					this->DrawString(r, i++, "  %02x[%02x]: %08x (%s)", niv->var, param, value, niv->name);
				} else if (niv->var >= 0x100) {
					extern const GRFVariableMapDefinition _grf_action2_remappable_variables[];
					for (const GRFVariableMapDefinition *info = _grf_action2_remappable_variables; info->name != nullptr; info++) {
						if (niv->var == info->id) {
							if (_current_text_dir == TD_RTL) {
								this->DrawString(r, i++, "  %s: %08x (%s)", info->name, value, niv->name);
							} else {
								if (this->log_console) DEBUG(misc, 0, "    %s: %08x (%s)", info->name, value, niv->name);

								int offset = i - this->vscroll->GetPosition();
								i++;
								if (offset >= 0 && offset < this->vscroll->GetCapacity()) {
									Rect sr = r.Shrink(WidgetDimensions::scaled.frametext).Shrink(0, offset * this->resize.step_height, 0, 0);
									char buf[512];
									seprintf(buf, lastof(buf), "  %s: ", info->name);
									::DrawString(sr.left, sr.right, sr.top, buf, TC_BLACK);
									seprintf(buf, lastof(buf), "%08x (%s)", value, niv->name);
									::DrawString(sr.left + prefix_width, sr.right, sr.top, buf, TC_BLACK);
								}
							}
							break;
						}
					}
				} else {
					this->DrawString(r, i++, "  %02x: %08x (%s)", niv->var, value, niv->name);
				}
			}
		}

		std::vector<uint32> psa_grfids = nih->GetPSAGRFIDs(index);
		for (const uint32 grfid : psa_grfids) {
			uint psa_size = nih->GetPSASize(index, grfid);
			const int32 *psa = nih->GetPSAFirstPosition(index, grfid);
			if (psa_size != 0 && psa != nullptr) {
				if (nih->PSAWithParameter()) {
					this->DrawString(r, i++, "Persistent storage [%08X]:", BSWAP32(grfid));
				} else {
					this->DrawString(r, i++, "Persistent storage:");
				}
				assert(psa_size % 4 == 0);
				uint last_non_blank = 0;
				for (uint j = 0; j < psa_size; j++) {
					if (psa[j] != 0) last_non_blank = j + 1;
				}
				const uint psa_limit = (last_non_blank + 3) & ~3;
				for (uint j = 0; j < psa_limit; j += 4, psa += 4) {
					this->DrawString(r, i++, "  %i: %i %i %i %i", j, psa[0], psa[1], psa[2], psa[3]);
				}
				if (last_non_blank != psa_size) {
					this->DrawString(r, i++, "  %i to %i are all 0", psa_limit, psa_size - 1);
				}
			}
		}

		if (nif->properties != nullptr) {
			this->DrawString(r, i++, "Properties:");
			for (const NIProperty *nip = nif->properties; nip->name != nullptr; nip++) {
				const void *ptr = (const byte *)base + nip->offset;
				uint value;
				switch (nip->read_size) {
					case 1: value = *(const uint8  *)ptr; break;
					case 2: value = *(const uint16 *)ptr; break;
					case 4: value = *(const uint32 *)ptr; break;
					default: NOT_REACHED();
				}

				StringID string;
				SetDParam(0, value);
				switch (nip->type) {
					case NIT_INT:
						string = STR_JUST_INT;
						break;

					case NIT_CARGO:
						string = value != INVALID_CARGO ? CargoSpec::Get(value)->name : STR_QUANTITY_N_A;
						break;

					default:
						NOT_REACHED();
				}

				char buffer[64];
				GetString(buffer, string, lastof(buffer));
				this->DrawString(r, i++, "  %02x: %s (%s)", nip->prop, buffer, nip->name);
			}
		}

		if (nif->callbacks != nullptr) {
			this->DrawString(r, i++, "Callbacks:");
			for (const NICallback *nic = nif->callbacks; nic->name != nullptr; nic++) {
				if (nic->cb_bit != CBM_NO_BIT) {
					const void *ptr = (const byte *)base_spec + nic->offset;
					uint value;
					switch (nic->read_size) {
						case 1: value = *(const uint8  *)ptr; break;
						case 2: value = *(const uint16 *)ptr; break;
						case 4: value = *(const uint32 *)ptr; break;
						default: NOT_REACHED();
					}

					if (!HasBit(value, nic->cb_bit)) continue;
					this->DrawString(r, i++, "  %03x: %s", nic->cb_id, nic->name);
				} else {
					this->DrawString(r, i++, "  %03x: %s (unmasked)", nic->cb_id, nic->name);
				}
			}
		}
	}

	bool UnOptimisedSpriteDumpOK() const
	{
		if (_grfs_loaded_with_sg_shadow_enable) return true;

		if (_networking && !_network_server) return false;

		extern uint NetworkClientCount();
		if (_networking && NetworkClientCount() > 1) {
			return false;
		}

		return true;
	}

	void SelectHighlightTag(uint32 tag)
	{
		for (uint i = 0; i < lengthof(this->selected_highlight_tags); i++) {
			if (this->selected_highlight_tags[i] == tag) {
				this->selected_highlight_tags[i] = 0;
				return;
			}
		}
		for (uint i = 0; i < lengthof(this->selected_highlight_tags); i++) {
			if (this->selected_highlight_tags[i] == 0) {
				this->selected_highlight_tags[i] = tag;
				return;
			}
		}
		this->selected_highlight_tags[lengthof(this->selected_highlight_tags) - 1] = tag;
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_NGRFI_PARENT: {
				const NIHelper *nih   = GetFeatureHelper(this->window_number);
				uint index = nih->GetParent(this->GetFeatureIndex());
				::ShowNewGRFInspectWindow(GetFeatureNum(index), ::GetFeatureIndex(index), nih->GetGRFID(this->GetFeatureIndex()));
				break;
			}

			case WID_NGRFI_VEH_PREV:
				if (this->chain_index > 0) {
					this->chain_index--;
					this->InvalidateData();
				}
				break;

			case WID_NGRFI_VEH_NEXT:
				if (this->HasChainIndex()) {
					uint index = this->GetFeatureIndex();
					Vehicle *v = Vehicle::Get(index);
					if (v != nullptr && v->Next() != nullptr) {
						this->chain_index++;
						this->InvalidateData();
					}
				}
				break;

			case WID_NGRFI_MAINPANEL: {
				/* Get the line, make sure it's within the boundaries. */
				int line = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_NGRFI_MAINPANEL, WidgetDimensions::scaled.framerect.top);
				if (line == INT_MAX) return;

				if (this->sprite_dump) {
					if (_ctrl_pressed) {
						uint32 highlight_tag = 0;
						auto iter = this->highlight_tag_lines.find(line);
						if (iter != this->highlight_tag_lines.end()) highlight_tag = iter->second;
						if (highlight_tag != 0) {
							this->SelectHighlightTag(highlight_tag);
							this->SetWidgetDirty(WID_NGRFI_MAINPANEL);
						}
					} else if (_shift_pressed) {
						const SpriteGroup *group = nullptr;
						auto iter = this->sprite_group_lines.find(line);
						if (iter != this->sprite_group_lines.end()) group = iter->second;
						if (group != nullptr) {
							auto iter = this->collapsed_groups.lower_bound(group);
							if (iter != this->collapsed_groups.end() && *iter == group) {
								this->collapsed_groups.erase(iter);
							} else {
								this->collapsed_groups.insert(iter, group);
							}
							this->SetWidgetDirty(WID_NGRFI_MAINPANEL);
						}

					} else {
						const SpriteGroup *group = nullptr;
						auto iter = this->sprite_group_lines.find(line);
						if (iter != this->sprite_group_lines.end()) group = iter->second;
						if (group != nullptr || this->selected_sprite_group != nullptr) {
							this->selected_sprite_group = (group == this->selected_sprite_group) ? nullptr : group;
							this->SetWidgetDirty(WID_NGRFI_MAINPANEL);
						}
					}
					return;
				}

				auto iter = this->extra_info_click_flag_toggles.find(line);
				if (iter != this->extra_info_click_flag_toggles.end()) {
					this->extra_info_flags ^= iter->second;
					this->SetDirty();
					return;
				}

				/* Does this feature have variables? */
				const NIFeature *nif  = GetFeature(this->window_number);
				if (nif->variables == nullptr) return;

				if (line < this->first_variable_line_index) return;
				line -= this->first_variable_line_index;

				/* Find the variable related to the line */
				for (const NIVariable *niv = nif->variables; niv->name != nullptr; niv++, line--) {
					if (line != 1) continue; // 1 because of the "Variables:" line

					if (!HasVariableParameter(niv->var)) break;

					this->current_edit_param = niv->var;
					ShowQueryString(STR_EMPTY, STR_NEWGRF_INSPECT_QUERY_CAPTION, 9, this, CS_HEXADECIMAL, QSF_NONE);
				}
				break;
			}

			case WID_NGRFI_REFRESH: {
				this->auto_refresh = !this->auto_refresh;
				this->SetWidgetLoweredState(WID_NGRFI_REFRESH, this->auto_refresh);
				this->SetWidgetDirty(WID_NGRFI_REFRESH);
				break;
			}

			case WID_NGRFI_LOG_CONSOLE: {
				this->log_console = true;
				this->SetWidgetDirty(WID_NGRFI_MAINPANEL);
				break;
			}

			case WID_NGRFI_DUPLICATE: {
				NewGRFInspectWindow *w = new NewGRFInspectWindow(this->window_desc, this->window_number);
				w->SetCallerGRFID(this->caller_grfid);
				break;
			}

			case WID_NGRFI_SPRITE_DUMP: {
				this->sprite_dump = !this->sprite_dump;
				this->SetWidgetLoweredState(WID_NGRFI_SPRITE_DUMP, this->sprite_dump);
				this->SetWidgetDisabledState(WID_NGRFI_SPRITE_DUMP_UNOPT, !this->sprite_dump || !UnOptimisedSpriteDumpOK());
				this->SetWidgetDisabledState(WID_NGRFI_SPRITE_DUMP_GOTO, !this->sprite_dump);
				this->GetWidget<NWidgetCore>(WID_NGRFI_MAINPANEL)->SetToolTip(this->sprite_dump ? STR_NEWGRF_INSPECT_SPRITE_DUMP_PANEL_TOOLTIP : STR_NULL);
				this->SetWidgetDirty(WID_NGRFI_SPRITE_DUMP);
				this->SetWidgetDirty(WID_NGRFI_SPRITE_DUMP_UNOPT);
				this->SetWidgetDirty(WID_NGRFI_SPRITE_DUMP_GOTO);
				this->SetWidgetDirty(WID_NGRFI_MAINPANEL);
				this->SetWidgetDirty(WID_NGRFI_SCROLLBAR);
				break;
			}

			case WID_NGRFI_SPRITE_DUMP_UNOPT: {
				if (!this->sprite_dump_unopt) {
					if (!UnOptimisedSpriteDumpOK()) {
						this->SetWidgetDisabledState(WID_NGRFI_SPRITE_DUMP_UNOPT, true);
						this->SetWidgetDirty(WID_NGRFI_SPRITE_DUMP_UNOPT);
						return;
					}
					if (!_grfs_loaded_with_sg_shadow_enable) {
						SetBit(_misc_debug_flags, MDF_NEWGRF_SG_SAVE_RAW);

						ReloadNewGRFData();

						extern void PostCheckNewGRFLoadWarnings();
						PostCheckNewGRFLoadWarnings();
					}
				}
				this->sprite_dump_unopt = !this->sprite_dump_unopt;
				this->SetWidgetLoweredState(WID_NGRFI_SPRITE_DUMP_UNOPT, this->sprite_dump_unopt);
				this->SetWidgetDirty(WID_NGRFI_SPRITE_DUMP_UNOPT);
				this->SetWidgetDirty(WID_NGRFI_MAINPANEL);
				this->SetWidgetDirty(WID_NGRFI_SCROLLBAR);
				break;
			}

			case WID_NGRFI_SPRITE_DUMP_GOTO: {
				this->current_edit_param = 0;
				ShowQueryString(STR_EMPTY, STR_SPRITE_ALIGNER_GOTO_CAPTION, 10, this, CS_NUMERAL, QSF_NONE);
				break;
			}
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (StrEmpty(str)) return;

		if (this->current_edit_param == 0 && this->sprite_dump) {
			auto iter = this->nfo_line_lines.find(atoi(str));
			if (iter != this->nfo_line_lines.end()) {
				this->vscroll->SetPosition(std::min<int>(iter->second, std::max<int>(0, this->vscroll->GetCount() - this->vscroll->GetCapacity())));
				this->SetWidgetDirty(WID_NGRFI_MAINPANEL);
				this->SetWidgetDirty(WID_NGRFI_SCROLLBAR);
			}
		} else if (this->current_edit_param != 0 && !this->sprite_dump) {
			NewGRFInspectWindow::var60params[GetFeatureNum(this->window_number)][this->current_edit_param - 0x60] = std::strtol(str, nullptr, 16);
			this->SetDirty();
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_NGRFI_MAINPANEL, WidgetDimensions::scaled.frametext.Vertical());
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (this->HasChainIndex()) {
			this->ValidateChainIndex();
			this->SetWidgetDisabledState(WID_NGRFI_VEH_PREV, this->chain_index == 0);
			Vehicle *v = Vehicle::Get(this->GetFeatureIndex());
			this->SetWidgetDisabledState(WID_NGRFI_VEH_NEXT, v == nullptr || v->Next() == nullptr);
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (this->auto_refresh) {
			this->SetDirty();
		} else {
			if (this->redraw_panel) this->SetWidgetDirty(WID_NGRFI_MAINPANEL);
			if (this->redraw_scrollbar) this->SetWidgetDirty(WID_NGRFI_SCROLLBAR);
		}
		this->redraw_panel = false;
		this->redraw_scrollbar = false;
	}
};

/* static */ uint32 NewGRFInspectWindow::var60params[GSF_FAKE_END][0x20] = { {0} }; // Use spec to have 0s in whole array

static const NWidgetPart _nested_newgrf_inspect_chain_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_NGRFI_CAPTION), SetDataTip(STR_NEWGRF_INSPECT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_NGRFI_SPRITE_DUMP_GOTO_SEL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NGRFI_SPRITE_DUMP_GOTO), SetDataTip(STR_NEWGRF_INSPECT_SPRITE_DUMP_GOTO, STR_NEWGRF_INSPECT_SPRITE_DUMP_GOTO_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_NGRFI_SPRITE_DUMP_UNOPT_SEL),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_NGRFI_SPRITE_DUMP_UNOPT), SetDataTip(STR_NEWGRF_INSPECT_SPRITE_DUMP_UNOPT, STR_NEWGRF_INSPECT_SPRITE_DUMP_UNOPT_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_NGRFI_SPRITE_DUMP_SEL),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_NGRFI_SPRITE_DUMP), SetDataTip(STR_NEWGRF_INSPECT_SPRITE_DUMP, STR_NEWGRF_INSPECT_SPRITE_DUMP_TOOLTIP),
		EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NGRFI_DUPLICATE), SetDataTip(STR_NEWGRF_INSPECT_DUPLICATE, STR_NEWGRF_INSPECT_DUPLICATE_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NGRFI_LOG_CONSOLE), SetDataTip(STR_NEWGRF_INSPECT_LOG_CONSOLE, STR_NEWGRF_INSPECT_LOG_CONSOLE_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_NGRFI_REFRESH), SetDataTip(STR_NEWGRF_INSPECT_REFRESH, STR_NEWGRF_INSPECT_REFRESH_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_NGRFI_VEH_PREV), SetDataTip(AWV_DECREASE, STR_NULL),
			NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_NGRFI_VEH_NEXT), SetDataTip(AWV_INCREASE, STR_NULL),
			NWidget(WWT_EMPTY, COLOUR_GREY, WID_NGRFI_VEH_CHAIN), SetFill(1, 0), SetResize(1, 0),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_NGRFI_MAINPANEL), SetMinimalSize(300, 0), SetScrollbar(WID_NGRFI_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_NGRFI_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static const NWidgetPart _nested_newgrf_inspect_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_NGRFI_CAPTION), SetDataTip(STR_NEWGRF_INSPECT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NGRFI_PARENT), SetDataTip(STR_NEWGRF_INSPECT_PARENT_BUTTON, STR_NEWGRF_INSPECT_PARENT_TOOLTIP),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_NGRFI_SPRITE_DUMP_GOTO_SEL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NGRFI_SPRITE_DUMP_GOTO), SetDataTip(STR_NEWGRF_INSPECT_SPRITE_DUMP_GOTO, STR_NEWGRF_INSPECT_SPRITE_DUMP_GOTO_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_NGRFI_SPRITE_DUMP_UNOPT_SEL),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_NGRFI_SPRITE_DUMP_UNOPT), SetDataTip(STR_NEWGRF_INSPECT_SPRITE_DUMP_UNOPT, STR_NEWGRF_INSPECT_SPRITE_DUMP_UNOPT_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_NGRFI_SPRITE_DUMP_SEL),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_NGRFI_SPRITE_DUMP), SetDataTip(STR_NEWGRF_INSPECT_SPRITE_DUMP, STR_NEWGRF_INSPECT_SPRITE_DUMP_TOOLTIP),
		EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NGRFI_DUPLICATE), SetDataTip(STR_NEWGRF_INSPECT_DUPLICATE, STR_NEWGRF_INSPECT_DUPLICATE_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_NGRFI_LOG_CONSOLE), SetDataTip(STR_NEWGRF_INSPECT_LOG_CONSOLE, STR_NEWGRF_INSPECT_LOG_CONSOLE_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_NGRFI_REFRESH), SetDataTip(STR_NEWGRF_INSPECT_REFRESH, STR_NEWGRF_INSPECT_REFRESH_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_NGRFI_MAINPANEL), SetMinimalSize(300, 0), SetScrollbar(WID_NGRFI_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_NGRFI_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _newgrf_inspect_chain_desc(
	WDP_AUTO, "newgrf_inspect_chain", 400, 300,
	WC_NEWGRF_INSPECT, WC_NONE,
	0,
	_nested_newgrf_inspect_chain_widgets, lengthof(_nested_newgrf_inspect_chain_widgets)
);

static WindowDesc _newgrf_inspect_desc(
	WDP_AUTO, "newgrf_inspect", 400, 300,
	WC_NEWGRF_INSPECT, WC_NONE,
	0,
	_nested_newgrf_inspect_widgets, lengthof(_nested_newgrf_inspect_widgets)
);

/**
 * Show the inspect window for a given feature and index.
 * The index is normally an in-game location/identifier, such
 * as a TileIndex or an IndustryID depending on the feature
 * we want to inspect.
 * @param feature The feature we want to inspect.
 * @param index   The index/identifier of the feature to inspect.
 * @param grfid   GRFID of the item opening this window, or 0 if not opened by other window.
 */
void ShowNewGRFInspectWindow(GrfSpecFeature feature, uint index, const uint32 grfid)
{
	if (index >= (1 << 27)) return;
	if (!IsNewGRFInspectable(feature, index)) return;

	WindowNumber wno = GetInspectWindowNumber(feature, index);
	WindowDesc *desc = (feature == GSF_TRAINS || feature == GSF_ROADVEHICLES || feature == GSF_SHIPS) ? &_newgrf_inspect_chain_desc : &_newgrf_inspect_desc;
	NewGRFInspectWindow *w = AllocateWindowDescFront<NewGRFInspectWindow>(desc, wno, true);
	w->SetCallerGRFID(grfid);
}

/**
 * Invalidate the inspect window for a given feature and index.
 * The index is normally an in-game location/identifier, such
 * as a TileIndex or an IndustryID depending on the feature
 * we want to inspect.
 * @param feature The feature we want to invalidate the window for.
 * @param index   The index/identifier of the feature to invalidate.
 */
void InvalidateNewGRFInspectWindow(GrfSpecFeature feature, uint index)
{
	if (feature == GSF_INVALID) return;
	if (index >= (1 << 27)) return;

	WindowNumber wno = GetInspectWindowNumber(feature, index);
	InvalidateWindowData(WC_NEWGRF_INSPECT, wno);
}

/**
 * Delete inspect window for a given feature and index.
 * The index is normally an in-game location/identifier, such
 * as a TileIndex or an IndustryID depending on the feature
 * we want to inspect.
 * @param feature The feature we want to delete the window for.
 * @param index   The index/identifier of the feature to delete.
 */
void DeleteNewGRFInspectWindow(GrfSpecFeature feature, uint index)
{
	if (feature == GSF_INVALID) return;
	if (index >= (1 << 27)) return;

	WindowNumber wno = GetInspectWindowNumber(feature, index);
	DeleteAllWindowsById(WC_NEWGRF_INSPECT, wno);

	/* Reinitialise the land information window to remove the "debug" sprite if needed.
	 * Note: Since we might be called from a command here, it is important to not execute
	 * the invalidation immediately. The landinfo window tests commands itself. */
	InvalidateWindowData(WC_LAND_INFO, 0, 1);
}

/**
 * Can we inspect the data given a certain feature and index.
 * The index is normally an in-game location/identifier, such
 * as a TileIndex or an IndustryID depending on the feature
 * we want to inspect.
 * @param feature The feature we want to inspect.
 * @param index   The index/identifier of the feature to inspect.
 * @return true if there is something to show.
 */
bool IsNewGRFInspectable(GrfSpecFeature feature, uint index)
{
	if (index >= (1 << 27)) return false;
	const NIFeature *nif = GetFeature(GetInspectWindowNumber(feature, index));
	if (nif == nullptr) return false;
	return nif->helper->IsInspectable(index);
}

/**
 * Get the GrfSpecFeature associated with the tile.
 * @param tile The tile to get the feature from.
 * @return the GrfSpecFeature.
 */
GrfSpecFeature GetGrfSpecFeature(TileIndex tile)
{
	switch (GetTileType(tile)) {
		default:              return GSF_INVALID;
		case MP_CLEAR:
			if (GetRawClearGround(tile) == CLEAR_ROCKS) return GSF_NEWLANDSCAPE;
			return GSF_INVALID;
		case MP_RAILWAY: {
			extern std::vector<const GRFFile *> _new_signals_grfs;
			if (HasSignals(tile) && !_new_signals_grfs.empty()) {
				return GSF_SIGNALS;
			}
			return GSF_RAILTYPES;
		}
		case MP_ROAD:         return IsLevelCrossing(tile) ? GSF_RAILTYPES : GSF_ROADTYPES;
		case MP_HOUSE:        return GSF_HOUSES;
		case MP_INDUSTRY:     return GSF_INDUSTRYTILES;
		case MP_OBJECT:       return GSF_OBJECTS;

		case MP_STATION:
			switch (GetStationType(tile)) {
				case STATION_RAIL:    return GSF_STATIONS;
				case STATION_AIRPORT: return GSF_AIRPORTTILES;

				case STATION_BUS:
				case STATION_TRUCK:
				case STATION_ROADWAYPOINT:
					return GSF_ROADSTOPS;

				default:
					return GSF_INVALID;
			}

		case MP_TUNNELBRIDGE: {
			if (IsTunnelBridgeWithSignalSimulation(tile)) return GSF_SIGNALS;
			return GSF_INVALID;
		}

	}
}

/**
 * Get the GrfSpecFeature associated with the vehicle.
 * @param type The vehicle type to get the feature from.
 * @return the GrfSpecFeature.
 */
GrfSpecFeature GetGrfSpecFeature(VehicleType type)
{
	switch (type) {
		case VEH_TRAIN:    return GSF_TRAINS;
		case VEH_ROAD:     return GSF_ROADVEHICLES;
		case VEH_SHIP:     return GSF_SHIPS;
		case VEH_AIRCRAFT: return GSF_AIRCRAFT;
		default:           return GSF_INVALID;
	}
}



/**** Sprite Aligner ****/

/** Window used for aligning sprites. */
struct SpriteAlignerWindow : Window {
	typedef std::pair<int16, int16> XyOffs;    ///< Pair for x and y offsets of the sprite before alignment. First value contains the x offset, second value y offset.

	SpriteID current_sprite;                   ///< The currently shown sprite.
	Scrollbar *vscroll;
	SmallMap<SpriteID, XyOffs> offs_start_map; ///< Mapping of starting offsets for the sprites which have been aligned in the sprite aligner window.

	static bool centre;
	static bool crosshair;

	SpriteAlignerWindow(WindowDesc *desc, WindowNumber wno) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_SA_SCROLLBAR);
		this->FinishInitNested(wno);

		this->SetWidgetLoweredState(WID_SA_CENTRE, SpriteAlignerWindow::centre);
		this->SetWidgetLoweredState(WID_SA_CROSSHAIR, SpriteAlignerWindow::crosshair);

		/* Oh yes, we assume there is at least one normal sprite! */
		while (GetSpriteType(this->current_sprite) != SpriteType::Normal) this->current_sprite++;
	}

	void SetStringParameters(int widget) const override
	{
		const Sprite *spr = GetSprite(this->current_sprite, SpriteType::Normal);
		switch (widget) {
			case WID_SA_CAPTION:
				SetDParam(0, this->current_sprite);
				SetDParamStr(1, GetOriginFile(this->current_sprite)->GetSimplifiedFilename());
				break;

			case WID_SA_OFFSETS_ABS:
				SetDParam(0, spr->x_offs);
				SetDParam(1, spr->y_offs);
				break;

			case WID_SA_OFFSETS_REL: {
				/* Relative offset is new absolute offset - starting absolute offset.
				 * Show 0, 0 as the relative offsets if entry is not in the map (meaning they have not been changed yet).
				 */
				const auto key_offs_pair = this->offs_start_map.Find(this->current_sprite);
				if (key_offs_pair != this->offs_start_map.end()) {
					SetDParam(0, spr->x_offs - key_offs_pair->second.first);
					SetDParam(1, spr->y_offs - key_offs_pair->second.second);
				} else {
					SetDParam(0, 0);
					SetDParam(1, 0);
				}
				break;
			}

			default:
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_SA_SPRITE:
				size->height = ScaleGUITrad(200);
				break;
			case WID_SA_LIST:
				SetDParamMaxDigits(0, 6);
				size->width = GetStringBoundingBox(STR_JUST_COMMA).width + padding.width;
				resize->height = FONT_HEIGHT_NORMAL + padding.height;
				resize->width  = 1;
				fill->height = resize->height;
				break;
			default:
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_SA_SPRITE: {
				/* Center the sprite ourselves */
				const Sprite *spr = GetSprite(this->current_sprite, SpriteType::Normal);
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				int x;
				int y;
				if (SpriteAlignerWindow::centre) {
					x = -UnScaleGUI(spr->x_offs) + (ir.Width() - UnScaleGUI(spr->width)) / 2;
					y = -UnScaleGUI(spr->y_offs) + (ir.Height() - UnScaleGUI(spr->height)) / 2;
				} else {
					x = ir.Width() / 2;
					y = ir.Height() / 2;
				}

				DrawPixelInfo new_dpi;
				if (!FillDrawPixelInfo(&new_dpi, ir.left, ir.top, ir.Width(), ir.Height())) break;
				AutoRestoreBackup dpi_backup(_cur_dpi, &new_dpi);

				DrawSprite(this->current_sprite, PAL_NONE, x, y, nullptr, ZOOM_LVL_GUI);
				if (this->crosshair) {
					GfxDrawLine(x, 0, x, ir.Height() - 1, PC_WHITE, 1, 1);
					GfxDrawLine(0, y, ir.Width() - 1, y, PC_WHITE, 1, 1);
				}
				break;
			}

			case WID_SA_LIST: {
				const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(widget);
				int step_size = nwid->resize_y;

				std::vector<SpriteID> &list = _newgrf_debug_sprite_picker.sprites;
				int max = std::min<int>(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), (uint)list.size());

				Rect ir = r.Shrink(WidgetDimensions::scaled.matrix);
				for (int i = this->vscroll->GetPosition(); i < max; i++) {
					SetDParam(0, list[i]);
					DrawString(ir, STR_JUST_COMMA, TC_BLACK, SA_RIGHT | SA_FORCE);
					ir.top += step_size;
				}
				break;
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_SA_PREVIOUS:
				do {
					this->current_sprite = (this->current_sprite == 0 ? GetMaxSpriteID() :  this->current_sprite) - 1;
				} while (GetSpriteType(this->current_sprite) != SpriteType::Normal);
				this->SetDirty();
				break;

			case WID_SA_GOTO:
				ShowQueryString(STR_EMPTY, STR_SPRITE_ALIGNER_GOTO_CAPTION, 7, this, CS_NUMERAL, QSF_NONE);
				break;

			case WID_SA_NEXT:
				do {
					this->current_sprite = (this->current_sprite + 1) % GetMaxSpriteID();
				} while (GetSpriteType(this->current_sprite) != SpriteType::Normal);
				this->SetDirty();
				break;

			case WID_SA_PICKER:
				this->LowerWidget(WID_SA_PICKER);
				_newgrf_debug_sprite_picker.mode = SPM_WAIT_CLICK;
				this->SetDirty();
				break;

			case WID_SA_LIST: {
				const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(widget);
				int step_size = nwid->resize_y;

				uint i = this->vscroll->GetPosition() + (pt.y - nwid->pos_y) / step_size;
				if (i < _newgrf_debug_sprite_picker.sprites.size()) {
					SpriteID spr = _newgrf_debug_sprite_picker.sprites[i];
					if (GetSpriteType(spr) == SpriteType::Normal) this->current_sprite = spr;
				}
				this->SetDirty();
				break;
			}

			case WID_SA_UP:
			case WID_SA_DOWN:
			case WID_SA_LEFT:
			case WID_SA_RIGHT: {
				/*
				 * Yes... this is a hack.
				 *
				 * No... I don't think it is useful to make this less of a hack.
				 *
				 * If you want to align sprites, you just need the number. Generally
				 * the sprite caches are big enough to not remove the sprite from the
				 * cache. If that's not the case, just let the NewGRF developer
				 * increase the cache size instead of storing thousands of offsets
				 * for the incredibly small chance that it's actually going to be
				 * used by someone and the sprite cache isn't big enough for that
				 * particular NewGRF developer.
				 */
				Sprite *spr = const_cast<Sprite *>(GetSprite(this->current_sprite, SpriteType::Normal));

				/* Remember the original offsets of the current sprite, if not already in mapping. */
				if (!(this->offs_start_map.Contains(this->current_sprite))) {
					this->offs_start_map.Insert(this->current_sprite, XyOffs(spr->x_offs, spr->y_offs));
				}
				switch (widget) {
					/* Move eight units at a time if ctrl is pressed. */
					case WID_SA_UP:    spr->y_offs -= _ctrl_pressed ? 8 : 1; break;
					case WID_SA_DOWN:  spr->y_offs += _ctrl_pressed ? 8 : 1; break;
					case WID_SA_LEFT:  spr->x_offs -= _ctrl_pressed ? 8 : 1; break;
					case WID_SA_RIGHT: spr->x_offs += _ctrl_pressed ? 8 : 1; break;
				}
				/* Of course, we need to redraw the sprite, but where is it used?
				 * Everywhere is a safe bet. */
				MarkWholeScreenDirty();
				break;
			}

			case WID_SA_RESET_REL:
				/* Reset the starting offsets for the current sprite. */
				this->offs_start_map.Erase(this->current_sprite);
				this->SetDirty();
				break;

			case WID_SA_CENTRE:
				SpriteAlignerWindow::centre = !SpriteAlignerWindow::centre;
				this->SetWidgetLoweredState(widget, SpriteAlignerWindow::centre);
				this->SetDirty();
				break;

			case WID_SA_CROSSHAIR:
				SpriteAlignerWindow::crosshair = !SpriteAlignerWindow::crosshair;
				this->SetWidgetLoweredState(widget, SpriteAlignerWindow::crosshair);
				this->SetDirty();
				break;
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (StrEmpty(str)) return;

		this->current_sprite = atoi(str);
		if (this->current_sprite >= GetMaxSpriteID()) this->current_sprite = 0;
		while (GetSpriteType(this->current_sprite) != SpriteType::Normal) {
			this->current_sprite = (this->current_sprite + 1) % GetMaxSpriteID();
		}
		this->SetDirty();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (data == 1) {
			/* Sprite picker finished */
			this->RaiseWidget(WID_SA_PICKER);
			this->vscroll->SetCount((uint)_newgrf_debug_sprite_picker.sprites.size());
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_SA_LIST);
	}
};

bool SpriteAlignerWindow::centre = true;
bool SpriteAlignerWindow::crosshair = true;

static const NWidgetPart _nested_sprite_aligner_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_SA_CAPTION), SetDataTip(STR_SPRITE_ALIGNER_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL), SetPIP(0, 0, 10),
			NWidget(NWID_VERTICAL), SetPIP(10, 5, 10),
				NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 5, 10),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SA_PREVIOUS), SetDataTip(STR_SPRITE_ALIGNER_PREVIOUS_BUTTON, STR_SPRITE_ALIGNER_PREVIOUS_TOOLTIP), SetFill(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SA_GOTO), SetDataTip(STR_SPRITE_ALIGNER_GOTO_BUTTON, STR_SPRITE_ALIGNER_GOTO_TOOLTIP), SetFill(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SA_NEXT), SetDataTip(STR_SPRITE_ALIGNER_NEXT_BUTTON, STR_SPRITE_ALIGNER_NEXT_TOOLTIP), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_HORIZONTAL), SetPIP(10, 5, 10),
					NWidget(NWID_SPACER), SetFill(1, 1),
					NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SA_UP), SetDataTip(SPR_ARROW_UP, STR_SPRITE_ALIGNER_MOVE_TOOLTIP), SetResize(0, 0), SetMinimalSize(11, 11),
					NWidget(NWID_SPACER), SetFill(1, 1),
				EndContainer(),
				NWidget(NWID_HORIZONTAL_LTR), SetPIP(10, 5, 10),
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetFill(1, 1),
						NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SA_LEFT), SetDataTip(SPR_ARROW_LEFT, STR_SPRITE_ALIGNER_MOVE_TOOLTIP), SetResize(0, 0), SetMinimalSize(11, 11),
						NWidget(NWID_SPACER), SetFill(1, 1),
					EndContainer(),
					NWidget(WWT_PANEL, COLOUR_DARK_BLUE, WID_SA_SPRITE), SetDataTip(STR_NULL, STR_SPRITE_ALIGNER_SPRITE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetFill(1, 1),
						NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SA_RIGHT), SetDataTip(SPR_ARROW_RIGHT, STR_SPRITE_ALIGNER_MOVE_TOOLTIP), SetResize(0, 0), SetMinimalSize(11, 11),
						NWidget(NWID_SPACER), SetFill(1, 1),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_HORIZONTAL), SetPIP(10, 5, 10),
					NWidget(NWID_SPACER), SetFill(1, 1),
					NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SA_DOWN), SetDataTip(SPR_ARROW_DOWN, STR_SPRITE_ALIGNER_MOVE_TOOLTIP), SetResize(0, 0), SetMinimalSize(11, 11),
					NWidget(NWID_SPACER), SetFill(1, 1),
				EndContainer(),
				NWidget(WWT_LABEL, COLOUR_GREY, WID_SA_OFFSETS_ABS), SetDataTip(STR_SPRITE_ALIGNER_OFFSETS_ABS, STR_NULL), SetFill(1, 0), SetPadding(0, 10, 0, 10),
				NWidget(WWT_LABEL, COLOUR_GREY, WID_SA_OFFSETS_REL), SetDataTip(STR_SPRITE_ALIGNER_OFFSETS_REL, STR_NULL), SetFill(1, 0), SetPadding(0, 10, 0, 10),
				NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(10, 5, 10),
					NWidget(WWT_TEXTBTN_2, COLOUR_GREY, WID_SA_CENTRE), SetDataTip(STR_SPRITE_ALIGNER_CENTRE_OFFSET, STR_NULL), SetFill(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SA_RESET_REL), SetDataTip(STR_SPRITE_ALIGNER_RESET_BUTTON, STR_SPRITE_ALIGNER_RESET_TOOLTIP), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_CROSSHAIR), SetDataTip(STR_SPRITE_ALIGNER_CROSSHAIR, STR_NULL), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_VERTICAL), SetPIP(10, 5, 10),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SA_PICKER), SetDataTip(STR_SPRITE_ALIGNER_PICKER_BUTTON, STR_SPRITE_ALIGNER_PICKER_TOOLTIP), SetFill(1, 0),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_MATRIX, COLOUR_GREY, WID_SA_LIST), SetResize(1, 1), SetMatrixDataTip(1, 0, STR_NULL), SetFill(1, 1), SetScrollbar(WID_SA_SCROLLBAR),
					NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_SA_SCROLLBAR),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _sprite_aligner_desc(
	WDP_AUTO, "sprite_aligner", 400, 300,
	WC_SPRITE_ALIGNER, WC_NONE,
	0,
	_nested_sprite_aligner_widgets, lengthof(_nested_sprite_aligner_widgets)
);

/**
 * Show the window for aligning sprites.
 */
void ShowSpriteAlignerWindow()
{
	AllocateWindowDescFront<SpriteAlignerWindow>(&_sprite_aligner_desc, 0);
}

const char *GetNewGRFCallbackName(CallbackID cbid)
{
	#define CBID(c) case c: return #c;
	switch (cbid) {
		CBID(CBID_RANDOM_TRIGGER)
		CBID(CBID_VEHICLE_VISUAL_EFFECT)
		CBID(CBID_VEHICLE_LENGTH)
		CBID(CBID_VEHICLE_LOAD_AMOUNT)
		CBID(CBID_STATION_AVAILABILITY)
		CBID(CBID_STATION_SPRITE_LAYOUT)
		CBID(CBID_VEHICLE_REFIT_CAPACITY)
		CBID(CBID_VEHICLE_ARTIC_ENGINE)
		CBID(CBID_HOUSE_ALLOW_CONSTRUCTION)
		CBID(CBID_GENERIC_AI_PURCHASE_SELECTION)
		CBID(CBID_VEHICLE_CARGO_SUFFIX)
		CBID(CBID_HOUSE_ANIMATION_NEXT_FRAME)
		CBID(CBID_HOUSE_ANIMATION_START_STOP)
		CBID(CBID_HOUSE_CONSTRUCTION_STATE_CHANGE)
		CBID(CBID_TRAIN_ALLOW_WAGON_ATTACH)
		CBID(CBID_HOUSE_COLOUR)
		CBID(CBID_HOUSE_CARGO_ACCEPTANCE)
		CBID(CBID_HOUSE_ANIMATION_SPEED)
		CBID(CBID_HOUSE_DESTRUCTION)
		CBID(CBID_INDUSTRY_PROBABILITY)
		CBID(CBID_VEHICLE_ADDITIONAL_TEXT)
		CBID(CBID_STATION_TILE_LAYOUT)
		CBID(CBID_INDTILE_ANIM_START_STOP)
		CBID(CBID_INDTILE_ANIM_NEXT_FRAME)
		CBID(CBID_INDTILE_ANIMATION_SPEED)
		CBID(CBID_INDUSTRY_LOCATION)
		CBID(CBID_INDUSTRY_PRODUCTION_CHANGE)
		CBID(CBID_HOUSE_ACCEPT_CARGO)
		CBID(CBID_INDTILE_CARGO_ACCEPTANCE)
		CBID(CBID_INDTILE_ACCEPT_CARGO)
		CBID(CBID_VEHICLE_COLOUR_MAPPING)
		CBID(CBID_HOUSE_PRODUCE_CARGO)
		CBID(CBID_INDTILE_SHAPE_CHECK)
		CBID(CBID_INDTILE_DRAW_FOUNDATIONS)
		CBID(CBID_VEHICLE_START_STOP_CHECK)
		CBID(CBID_VEHICLE_32DAY_CALLBACK)
		CBID(CBID_VEHICLE_SOUND_EFFECT)
		CBID(CBID_VEHICLE_AUTOREPLACE_SELECTION)
		CBID(CBID_INDUSTRY_MONTHLYPROD_CHANGE)
		CBID(CBID_VEHICLE_MODIFY_PROPERTY)
		CBID(CBID_INDUSTRY_CARGO_SUFFIX)
		CBID(CBID_INDUSTRY_FUND_MORE_TEXT)
		CBID(CBID_CARGO_PROFIT_CALC)
		CBID(CBID_INDUSTRY_WINDOW_MORE_TEXT)
		CBID(CBID_INDUSTRY_SPECIAL_EFFECT)
		CBID(CBID_INDTILE_AUTOSLOPE)
		CBID(CBID_INDUSTRY_REFUSE_CARGO)
		CBID(CBID_STATION_ANIM_START_STOP)
		CBID(CBID_STATION_ANIM_NEXT_FRAME)
		CBID(CBID_STATION_ANIMATION_SPEED)
		CBID(CBID_HOUSE_DENY_DESTRUCTION)
		CBID(CBID_SOUNDS_AMBIENT_EFFECT)
		CBID(CBID_CARGO_STATION_RATING_CALC)
		CBID(CBID_NEW_SIGNALS_SPRITE_DRAW)
		CBID(CBID_CANALS_SPRITE_OFFSET)
		CBID(CBID_HOUSE_WATCHED_CARGO_ACCEPTED)
		CBID(CBID_STATION_LAND_SLOPE_CHECK)
		CBID(CBID_INDUSTRY_DECIDE_COLOUR)
		CBID(CBID_INDUSTRY_INPUT_CARGO_TYPES)
		CBID(CBID_INDUSTRY_OUTPUT_CARGO_TYPES)
		CBID(CBID_HOUSE_CUSTOM_NAME)
		CBID(CBID_HOUSE_DRAW_FOUNDATIONS)
		CBID(CBID_HOUSE_AUTOSLOPE)
		CBID(CBID_AIRPTILE_DRAW_FOUNDATIONS)
		CBID(CBID_AIRPTILE_ANIM_START_STOP)
		CBID(CBID_AIRPTILE_ANIM_NEXT_FRAME)
		CBID(CBID_AIRPTILE_ANIMATION_SPEED)
		CBID(CBID_AIRPORT_ADDITIONAL_TEXT)
		CBID(CBID_AIRPORT_LAYOUT_NAME)
		CBID(CBID_OBJECT_LAND_SLOPE_CHECK)
		CBID(CBID_OBJECT_ANIMATION_NEXT_FRAME)
		CBID(CBID_OBJECT_ANIMATION_START_STOP)
		CBID(CBID_OBJECT_ANIMATION_SPEED)
		CBID(CBID_OBJECT_COLOUR)
		CBID(CBID_OBJECT_FUND_MORE_TEXT)
		CBID(CBID_OBJECT_AUTOSLOPE)
		CBID(CBID_VEHICLE_REFIT_COST)
		CBID(CBID_INDUSTRY_PROD_CHANGE_BUILD)
		CBID(CBID_VEHICLE_SPAWN_VISUAL_EFFECT)
		CBID(CBID_VEHICLE_NAME)
		CBID(XCBID_TOWN_ZONES)
		CBID(XCBID_SHIP_REFIT_PART_NAME)
		default: return nullptr;
	}
}
