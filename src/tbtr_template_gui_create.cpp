#include "stdafx.h"

#include "gfx_func.h"
#include "direction_type.h"

#include "strings_func.h"
#include "window_func.h"
#include "company_func.h"
#include "window_gui.h"
#include "settings_func.h"
#include "core/geometry_func.hpp"
#include "table/sprites.h"
#include "table/strings.h"
#include "viewport_func.h"
#include "window_func.h"
#include "gui.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "depot_base.h"
#include "vehicle_gui.h"
#include "spritecache.h"
#include "strings_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "company_func.h"
#include "tilehighlight_func.h"
#include "window_gui.h"
#include "vehiclelist.h"
#include "order_backup.h"
#include "group.h"
#include "company_base.h"

#include "tbtr_template_gui_create.h"
#include "tbtr_template_vehicle.h"
#include "tbtr_template_vehicle_func.h"

#include "train.h"

class TemplateReplaceWindow;

// some space in front of the virtual train in the matrix
uint16 TRAIN_FRONT_SPACE = 16;

enum TemplateReplaceWindowWidgets {
	TCW_CAPTION,
	TCW_NEW_TMPL_PANEL,
	TCW_INFO_PANEL,
	TCW_SCROLLBAR_H_NEW_TMPL,
	TCW_SCROLLBAR_V_NEW_TMPL,
	TCW_SELL_TMPL,
	TCW_NEW,
	TCW_OK,
	TCW_CANCEL,
	TCW_REFIT,
	TCW_CLONE,
};

static const NWidgetPart _widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, TCW_CAPTION), SetDataTip(STR_TMPL_CREATEGUI_TITLE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_PANEL, COLOUR_GREY, TCW_NEW_TMPL_PANEL), SetMinimalSize(250, 30), SetResize(1, 0), SetScrollbar(TCW_SCROLLBAR_H_NEW_TMPL), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, TCW_INFO_PANEL), SetMinimalSize(250, 100), SetResize(1, 1), SetScrollbar(TCW_SCROLLBAR_V_NEW_TMPL), EndContainer(),
			NWidget(NWID_HSCROLLBAR, COLOUR_GREY, TCW_SCROLLBAR_H_NEW_TMPL),
		EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_GREY, TCW_SELL_TMPL), SetMinimalSize(40, 40), SetDataTip(0x0, STR_NULL), SetResize(0, 1), SetFill(0, 1),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, TCW_SCROLLBAR_V_NEW_TMPL),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TCW_OK),     SetMinimalSize(52, 12), SetResize(1, 0), SetDataTip(STR_TMPL_CONFIRM,          STR_TMPL_CONFIRM),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TCW_NEW),    SetMinimalSize(52, 12), SetResize(1, 0), SetDataTip(STR_TMPL_NEW,              STR_TMPL_NEW),
		NWidget(WWT_TEXTBTN,    COLOUR_GREY, TCW_CLONE),  SetMinimalSize(52, 12), SetResize(1, 0), SetDataTip(STR_TMPL_CREATE_CLONE_VEH, STR_TMPL_CREATE_CLONE_VEH),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TCW_REFIT),  SetMinimalSize(52, 12), SetResize(1, 0), SetDataTip(STR_TMPL_REFIT,            STR_TMPL_REFIT),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TCW_CANCEL), SetMinimalSize(52, 12), SetResize(1, 0), SetDataTip(STR_TMPL_CANCEL,           STR_TMPL_CANCEL),
		NWidget(WWT_RESIZEBOX,  COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _template_create_window_desc(
	WDP_AUTO,						// window position
	"template create window",		// const char* ini_key
	456, 100,						// window size
	WC_CREATE_TEMPLATE,				// window class
	WC_NONE,			            // parent window class
	WDF_CONSTRUCTION,				// window flags
	_widgets, lengthof(_widgets)	// widgets + num widgets
);

static void TrainDepotMoveVehicle(const Vehicle *wagon, VehicleID sel, const Vehicle *head)
{
	const Vehicle *v = Vehicle::Get(sel);

	if (v == wagon) return;

	if (wagon == NULL) {
		if (head != NULL) wagon = head->Last();
	} else {
		wagon = wagon->Previous();
		if (wagon == NULL) return;
	}

	if (wagon == v) return;

	DoCommandP(v->tile, v->index | (_ctrl_pressed ? 1 : 0) << 20 | 1 << 21, wagon == NULL ? INVALID_VEHICLE : wagon->index, CMD_MOVE_RAIL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_MOVE_VEHICLE), CcVirtualTrainWaggonsMoved);
}

class TemplateCreateWindow : public Window {
private:
	Scrollbar *hscroll;
	Scrollbar *vscroll;
	int line_height;
	Train* virtual_train;
	bool editMode;
	bool *noticeParent;
	bool *createWindowOpen;			/// used to notify main window of progress (dummy way of disabling 'delete' while editing a template)
	bool virtualTrainChangedNotice;
	VehicleID sel;
	VehicleID vehicle_over;
	TemplateVehicle *editTemplate;

public:
	TemplateCreateWindow(WindowDesc* _wdesc, TemplateVehicle *to_edit, bool *notice, bool *windowOpen, int step_h) : Window(_wdesc)
	{
		this->line_height = step_h;
		this->CreateNestedTree(_wdesc != NULL);
		this->hscroll = this->GetScrollbar(TCW_SCROLLBAR_H_NEW_TMPL);
		this->vscroll = this->GetScrollbar(TCW_SCROLLBAR_V_NEW_TMPL);
		this->FinishInitNested(VEH_TRAIN);
		/* a sprite */
		this->GetWidget<NWidgetCore>(TCW_SELL_TMPL)->widget_data = SPR_SELL_TRAIN;

		this->owner = _local_company;

		noticeParent = notice;
		createWindowOpen = windowOpen;
		virtualTrainChangedNotice = false;
		this->editTemplate = to_edit;

		if (to_edit) editMode = true;
		else editMode = false;

		this->sel = INVALID_VEHICLE;
		this->vehicle_over = INVALID_VEHICLE;

		if (to_edit) {
			DoCommandP(0, to_edit->index, 0, CMD_VIRTUAL_TRAIN_FROM_TEMPLATE_VEHICLE, CcSetVirtualTrain);
		}

		this->resize.step_height = 1;
	}

	~TemplateCreateWindow()
	{
		if (virtual_train != nullptr) {
			DoCommandP(0, virtual_train->index, 0, CMD_DELETE_VIRTUAL_TRAIN);
			virtual_train = nullptr;
		}

		SetWindowClassesDirty(WC_TRAINS_LIST);

		/* more cleanup */
		*createWindowOpen = false;
		DeleteWindowById(WC_BUILD_VIRTUAL_TRAIN, this->window_number);

	}

	void SetVirtualTrain(Train* const train)
	{
		if (virtual_train != nullptr) {
			DoCommandP(0, virtual_train->index, 0, CMD_DELETE_VIRTUAL_TRAIN);
		}

		virtual_train = train;
	}

	virtual void OnResize()
	{
		NWidgetCore *template_panel = this->GetWidget<NWidgetCore>(TCW_NEW_TMPL_PANEL);
		this->hscroll->SetCapacity(template_panel->current_x);

		NWidgetCore *info_panel = this->GetWidget<NWidgetCore>(TCW_INFO_PANEL);
		this->vscroll->SetCapacity(info_panel->current_y);
	}


	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		virtualTrainChangedNotice = true;
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch(widget) {
			case TCW_NEW_TMPL_PANEL: {
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(TCW_NEW_TMPL_PANEL);
				ClickedOnVehiclePanel(pt.x - nwi->pos_x, pt.y - nwi->pos_y);
				break;
			}
			case TCW_NEW: {
				ShowBuildVirtualTrainWindow(&virtual_train, &virtualTrainChangedNotice);
				break;
			}
			case TCW_CLONE: {
				this->SetWidgetDirty(TCW_CLONE);
				this->ToggleWidgetLoweredState(TCW_CLONE);
				if (this->IsWidgetLowered(TCW_CLONE)) {
					static const CursorID clone_icon =	SPR_CURSOR_CLONE_TRAIN;
					SetObjectToPlaceWnd(clone_icon, PAL_NONE, HT_VEHICLE, this);
				} else {
					ResetObjectToPlace();
				}
				break;
			}
			case TCW_OK: {
				uint32 templateIndex = (editTemplate != nullptr) ? editTemplate->index : INVALID_VEHICLE;
				
				if (virtual_train != nullptr) {
					DoCommandP(0, templateIndex, virtual_train->index, CMD_REPLACE_TEMPLATE_VEHICLE);
					virtual_train = nullptr;
				} else if (templateIndex != INVALID_VEHICLE) {
					DoCommandP(0, templateIndex, 0, CMD_DELETE_TEMPLATE_VEHICLE);
				}
				delete this;
				break;
			}
			case TCW_CANCEL: {
				delete this;
				break;
			}
			case TCW_REFIT: {
				ShowVehicleRefitWindow(virtual_train, INVALID_VEH_ORDER_ID, this, false, true);
				break;
			}
		}
	}

	virtual bool OnVehicleSelect(const Vehicle *v)
	{
		// throw away the current virtual train
		if (virtual_train != nullptr) {
			DoCommandP(0, virtual_train->index, 0, CMD_DELETE_VIRTUAL_TRAIN);
			virtual_train = nullptr;
		}

		// create a new one
		DoCommandP(0, v->index, 0, CMD_VIRTUAL_TRAIN_FROM_TRAIN, CcSetVirtualTrain);
		this->ToggleWidgetLoweredState(TCW_CLONE);
		ResetObjectToPlace();
		this->SetDirty();

		return true;
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch(widget) {
			case TCW_NEW_TMPL_PANEL: {
				if ( this->virtual_train ) {
					DrawTrainImage(virtual_train, r.left+TRAIN_FRONT_SPACE, r.right-25, r.top+2, this->sel, EIT_PURCHASE, this->hscroll->GetPosition(), this->vehicle_over);
					SetDParam(0, CeilDiv(virtual_train->gcache.cached_total_length * 10, TILE_SIZE));
					SetDParam(1, 1);
					DrawString(r.left, r.right, r.top, STR_TINY_BLACK_DECIMAL, TC_BLACK, SA_RIGHT);
				}
				break;
			}
			case TCW_INFO_PANEL: {
				if ( this->virtual_train ) {
					DrawPixelInfo tmp_dpi, *old_dpi;

					if (!FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left, r.bottom - r.top)) break;

					old_dpi = _cur_dpi;
					_cur_dpi = &tmp_dpi;

					/* Draw vehicle performance info */
					const GroundVehicleCache *gcache = this->virtual_train->GetGroundVehicleCache();
					SetDParam(2, this->virtual_train->GetDisplayMaxSpeed());
					SetDParam(1, gcache->cached_power);
					SetDParam(0, gcache->cached_weight);
					SetDParam(3, gcache->cached_max_te / 1000);
					DrawString(8, r.right, 4 - this->vscroll->GetPosition(), STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED_MAX_TE);
					/* Draw cargo summary */
					CargoArray cargo_caps;
					for ( const Train *tmp=this->virtual_train; tmp; tmp=tmp->Next() )
						cargo_caps[tmp->cargo_type] += tmp->cargo_cap;
					int y = 30 - this->vscroll->GetPosition();
					for (CargoID i = 0; i < NUM_CARGO; ++i) {
						if ( cargo_caps[i] > 0 ) {
							SetDParam(0, i);
							SetDParam(1, cargo_caps[i]);
							SetDParam(2, _settings_game.vehicle.freight_trains);
							DrawString(8, r.right, y, STR_TMPL_CARGO_SUMMARY, TC_LIGHT_BLUE, SA_LEFT);
							y += this->line_height/3;
						}
					}

					_cur_dpi = old_dpi;
				}
				break;
			}
			default:
				break;
		}
	}
	virtual void OnTick()
	{
		if (virtualTrainChangedNotice) {
			this->SetDirty();
			virtualTrainChangedNotice = false;
		}
	}
	virtual void OnDragDrop(Point pt, int widget)
	{
		switch (widget) {
			case TCW_NEW_TMPL_PANEL: {
				const Vehicle *v = NULL;
				VehicleID sel = this->sel;

				this->sel = INVALID_VEHICLE;
				this->SetDirty();

				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(TCW_NEW_TMPL_PANEL);
				GetDepotVehiclePtData gdvp = { NULL, NULL };

				if (this->GetVehicleFromDepotWndPt(pt.x - nwi->pos_x, pt.y - nwi->pos_y, &v, &gdvp) == MODE_DRAG_VEHICLE && sel != INVALID_VEHICLE) {
					if (gdvp.wagon == NULL || gdvp.wagon->index != sel) {
						this->vehicle_over = INVALID_VEHICLE;
						TrainDepotMoveVehicle(gdvp.wagon, sel, gdvp.head);
					}
				}
				break;
			}
			case TCW_SELL_TMPL: {
				if (this->IsWidgetDisabled(widget)) return;
				if (this->sel == INVALID_VEHICLE) return;

				int sell_cmd = (_ctrl_pressed) ? 1 : 0;

				Train* train_to_delete = Train::Get(this->sel);

				if (virtual_train == train_to_delete)
					virtual_train = (_ctrl_pressed) ? nullptr : virtual_train->GetNextUnit();

				DoCommandP(0, this->sel | sell_cmd << 20 | 1 << 21, 0, GetCmdSellVeh(VEH_TRAIN));

				this->sel = INVALID_VEHICLE;

				this->SetDirty();
				break;
			}
			default:
				this->sel = INVALID_VEHICLE;
				this->SetDirty();
		}
		_cursor.vehchain = false;
		this->sel = INVALID_VEHICLE;
		this->SetDirty();
	}

	virtual void OnMouseDrag(Point pt, int widget)
	{
		if (this->sel == INVALID_VEHICLE) return;
		/* A rail vehicle is dragged.. */
		if (widget != TCW_NEW_TMPL_PANEL) { // ..outside of the depot matrix.
			if (this->vehicle_over != INVALID_VEHICLE) {
				this->vehicle_over = INVALID_VEHICLE;
				this->SetWidgetDirty(TCW_NEW_TMPL_PANEL);
			}
			return;
		}

		NWidgetBase *matrix = this->GetWidget<NWidgetBase>(widget);
		const Vehicle *v = NULL;
		GetDepotVehiclePtData gdvp = {NULL, NULL};

		if (this->GetVehicleFromDepotWndPt(pt.x - matrix->pos_x, pt.y - matrix->pos_y, &v, &gdvp) != MODE_DRAG_VEHICLE) return;
		VehicleID new_vehicle_over = INVALID_VEHICLE;
		if (gdvp.head != NULL) {
			if (gdvp.wagon == NULL && gdvp.head->Last()->index != this->sel) { // ..at the end of the train.
				/* NOTE: As a wagon can't be moved at the begin of a train, head index isn't used to mark a drag-and-drop
				 * destination inside a train. This head index is then used to indicate that a wagon is inserted at
				 * the end of the train.
				 */
				new_vehicle_over = gdvp.head->index;
			} else if (gdvp.wagon != NULL && gdvp.head != gdvp.wagon &&
					gdvp.wagon->index != this->sel &&
					gdvp.wagon->Previous()->index != this->sel) { // ..over an existing wagon.
				new_vehicle_over = gdvp.wagon->index;
			}
		}
		if (this->vehicle_over == new_vehicle_over) return;

		this->vehicle_over = new_vehicle_over;
		this->SetWidgetDirty(widget);
	}

	virtual void OnPaint()
	{
		uint min_width = 32;
		uint min_height = 30;
		uint width = 0;
		uint height = 30;
		CargoArray cargo_caps;

		if (virtual_train != nullptr) {
			for (Train *train = virtual_train; train != nullptr; train = train->Next()) {
				width += train->GetDisplayImageWidth();
				cargo_caps[train->cargo_type] += train->cargo_cap;
			}

			for (CargoID i = 0; i < NUM_CARGO; ++i) {
				if ( cargo_caps[i] > 0 ) {
					height += this->line_height/3;
				}
			}
		}

		min_width = max(min_width, width);
		this->hscroll->SetCount(min_width + 50);

		min_height = max(min_height, height);
		this->vscroll->SetCount(min_height);

		this->DrawWidgets();
	}
	struct GetDepotVehiclePtData {
		const Vehicle *head;
		const Vehicle *wagon;
	};

	enum DepotGUIAction {
		MODE_ERROR,
		MODE_DRAG_VEHICLE,
		MODE_SHOW_VEHICLE,
		MODE_START_STOP,
	};

	uint count_width;
	uint header_width;

	DepotGUIAction GetVehicleFromDepotWndPt(int x, int y, const Vehicle **veh, GetDepotVehiclePtData *d) const
	{
		const NWidgetCore *matrix_widget = this->GetWidget<NWidgetCore>(TCW_NEW_TMPL_PANEL);
		/* In case of RTL the widgets are swapped as a whole */
		if (_current_text_dir == TD_RTL) x = matrix_widget->current_x - x;
		
		x -= TRAIN_FRONT_SPACE;

		uint xm = x;

		bool wagon = false;

		x += this->hscroll->GetPosition();
		const Train *v = virtual_train;
		d->head = d->wagon = v;

		if (xm <= this->header_width) {

			if (wagon) return MODE_ERROR;

			return MODE_SHOW_VEHICLE;
		}

		/* Account for the header */
		x -= this->header_width;

		/* find the vehicle in this row that was clicked */
		for (; v != NULL; v = v->Next()) {
			x -= v->GetDisplayImageWidth();
			if (x < 0) break;
		}

		d->wagon = (v != NULL ? v->GetFirstEnginePart() : NULL);

		return MODE_DRAG_VEHICLE;
	}

	void ClickedOnVehiclePanel(int x, int y)
	{
		GetDepotVehiclePtData gdvp = { NULL, NULL };
		const Vehicle *v = NULL;
		this->GetVehicleFromDepotWndPt(x, y, &v, &gdvp);

		v = gdvp.wagon;

		if (v != NULL && VehicleClicked(v)) return;
		VehicleID sel = this->sel;

		if (sel != INVALID_VEHICLE) {
			this->sel = INVALID_VEHICLE;
			TrainDepotMoveVehicle(v, sel, gdvp.head);			
		} else if (v != NULL) {
			int image = v->GetImage(_current_text_dir == TD_RTL ? DIR_E : DIR_W, EIT_PURCHASE);
			SetObjectToPlaceWnd(image, GetVehiclePalette(v), HT_DRAG, this);

			this->sel = v->index;
			this->SetDirty();

			_cursor.short_vehicle_offset = v->IsGroundVehicle() ? 16 - v->GetGroundVehicleCache()->cached_veh_length * 2 : 0;
			_cursor.vehchain = _ctrl_pressed;
		}
	}

	void RearrangeVirtualTrain()
	{
		virtual_train = virtual_train->First();
	}
};

void ShowTemplateCreateWindow(TemplateVehicle *to_edit, bool *noticeParent, bool *createWindowOpen, int step_h)
{
	if ( BringWindowToFrontById(WC_CREATE_TEMPLATE, VEH_TRAIN) != NULL ) return;
	new TemplateCreateWindow(&_template_create_window_desc, to_edit, noticeParent, createWindowOpen, step_h);
}

void CcSetVirtualTrain(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;

	Window* window = FindWindowById(WC_CREATE_TEMPLATE, 0);
	if (window) {
		Train* train = Train::From(Vehicle::Get(_new_vehicle_id));
		((TemplateCreateWindow*)window)->SetVirtualTrain(train);
		window->InvalidateData();
	}
}

void CcVirtualTrainWaggonsMoved(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;

	Window* window = FindWindowById(WC_CREATE_TEMPLATE, 0);
	if (window) {
		((TemplateCreateWindow*)window)->RearrangeVirtualTrain();
		window->InvalidateData();
	}
}

void CcDeleteVirtualTrain(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	VehicleID virtual_train_id = p2;
	DoCommandP(0, virtual_train_id, 0, CMD_DELETE_VIRTUAL_TRAIN);
}