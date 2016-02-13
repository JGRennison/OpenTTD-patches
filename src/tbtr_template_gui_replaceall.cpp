// replace all gui impl

#include "tbtr_template_gui_replaceall.h"

#include <stdio.h>

/*
 * A wrapper which contains a virtual train and additional info of the template vehicle it is replacing
 * We will restore this additional info when creating a new template from the changed virtual train
 */
struct VirtTrainInfo {
	// the virtual train
	Train *vt;

	// additional info from the template
	VehicleID original_index;

	bool	reuse_depot_vehicles,
			keep_remaining_vehicles,
			refit_as_template;

	CargoID cargo_type;
	byte cargo_subtype;

	// a fancy constructor
	VirtTrainInfo(Train *t) { this->vt = t; }
};

typedef AutoFreeSmallVector<VirtTrainInfo*, 64> VirtTrainList;
enum Widgets {
	RPLALL_GUI_CAPTION,

	RPLALL_GUI_INSET_1,
	RPLALL_GUI_INSET_1_1,
	RPLALL_GUI_INSET_1_2,
	RPLALL_GUI_MATRIX_TOPLEFT,
	RPLALL_GUI_MATRIX_TOPRIGHT,
	RPLALL_GUI_SCROLL_TL,
	RPLALL_GUI_SCROLL_TR,

	RPLALL_GUI_INSET_2,
	RPLALL_GUI_MATRIX_BOTTOM,
	RPLALL_GUI_SCROLL_BO,

	RPLALL_GUI_INSET_3,
	RPLALL_GUI_BUTTON_RPLALL,
	RPLALL_GUI_PANEL_BUTTONFLUFF_1,
	RPLALL_GUI_PANEL_BUTTONFLUFF_2,
	RPLALL_GUI_BUTTON_APPLY,
	RPLALL_GUI_PANEL_BUTTONFLUFF_3,
	RPLALL_GUI_BUTTON_CANCEL,

	RPLALL_GUI_PANEL_RESIZEFLUFF
};

static const NWidgetPart widgets[] = {
	// title bar
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, RPLALL_GUI_CAPTION), SetDataTip(STR_TMPL_RPLALLGUI_TITLE, STR_TMPL_RPLALLGUI_TITLE),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	// top matrices
		NWidget(WWT_INSET, COLOUR_GREY, RPLALL_GUI_INSET_1), SetMinimalSize(100,12), SetResize(1,0), SetDataTip(STR_TMPL_RPLALLGUI_INSET_TOP, STR_TMPL_RPLALLGUI_INSET_TOP), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_INSET, COLOUR_GREY, RPLALL_GUI_INSET_1_1), SetMinimalSize(100,12), SetResize(1,0), SetDataTip(STR_TMPL_RPLALLGUI_INSET_TOP_1, STR_TMPL_RPLALLGUI_INSET_TOP_1), EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, RPLALL_GUI_MATRIX_TOPLEFT), SetMinimalSize(100, 16), SetFill(1, 1), SetResize(1, 1), SetScrollbar(RPLALL_GUI_SCROLL_TL),// SetDataTip(0x1, STR_REPLACE_HELP_LEFT_ARRAY),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, RPLALL_GUI_SCROLL_TL),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_INSET, COLOUR_GREY, RPLALL_GUI_INSET_1_2), SetMinimalSize(100,12), SetResize(1,0), SetDataTip(STR_TMPL_RPLALLGUI_INSET_TOP_2, STR_TMPL_RPLALLGUI_INSET_TOP_2), EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, RPLALL_GUI_MATRIX_TOPRIGHT), SetMinimalSize(100, 16), SetFill(1, 1), SetResize(1, 1), SetScrollbar(RPLALL_GUI_SCROLL_TR),// SetDataTip(0x1, STR_REPLACE_HELP_LEFT_ARRAY),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, RPLALL_GUI_SCROLL_TR),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	// bottom matrix
	NWidget(WWT_INSET, COLOUR_GREY, RPLALL_GUI_INSET_2), SetMinimalSize(200,12), SetResize(1,0), SetDataTip(STR_TMPL_RPLALLGUI_INSET_BOTTOM, STR_TMPL_RPLALLGUI_INSET_BOTTOM), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, RPLALL_GUI_MATRIX_BOTTOM), SetMinimalSize(200, 16), SetFill(1, 1), SetResize(1, 1), SetScrollbar(RPLALL_GUI_SCROLL_BO),// SetDataTip(0x1, STR_REPLACE_HELP_LEFT_ARRAY),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, RPLALL_GUI_SCROLL_BO),
	EndContainer(),
	// control area
	NWidget(WWT_INSET, COLOUR_GREY, RPLALL_GUI_INSET_3), SetMinimalSize(200,12), SetResize(1,0), EndContainer(),// SetDataTip(STR_TMPL_MAINGUI_DEFINEDGROUPS, STR_TMPL_MAINGUI_DEFINEDGROUPS),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, RPLALL_GUI_PANEL_BUTTONFLUFF_1), SetMinimalSize(75,12), SetResize(1,0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, RPLALL_GUI_BUTTON_RPLALL), SetMinimalSize(150,12), SetResize(0,0), SetDataTip(STR_TMPL_RPLALLGUI_BUTTON_RPLALL, STR_TMPL_RPLALLGUI_BUTTON_RPLALL),
		NWidget(WWT_PANEL, COLOUR_GREY, RPLALL_GUI_PANEL_BUTTONFLUFF_2), SetMinimalSize(75,12), SetResize(1,0), EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, RPLALL_GUI_BUTTON_APPLY), SetMinimalSize(75,12), SetResize(1,0), SetDataTip(STR_TMPL_RPLALLGUI_BUTTON_APPLY, STR_TMPL_RPLALLGUI_BUTTON_APPLY),
		NWidget(WWT_PANEL, COLOUR_GREY, RPLALL_GUI_PANEL_BUTTONFLUFF_3), SetMinimalSize(150,12), SetResize(0,0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, RPLALL_GUI_BUTTON_CANCEL), SetMinimalSize(75,12), SetResize(1,0), SetDataTip(STR_TMPL_RPLALLGUI_BUTTON_CANCEL, STR_TMPL_RPLALLGUI_BUTTON_CANCEL),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, RPLALL_GUI_PANEL_RESIZEFLUFF), SetMinimalSize(100,12), SetResize(1,0), EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _template_replace_replaceall_desc(
	WDP_AUTO,
	"template replace window",
	400, 200,
	WC_TEMPLATEGUI_RPLALL, WC_NONE,
	WDF_CONSTRUCTION,
	widgets, lengthof(widgets)
);

static int CDECL EngineNumberSorter(const EngineID *a, const EngineID *b)
{
	int r = Engine::Get(*a)->list_position - Engine::Get(*b)->list_position;

	return r;
}
static int CDECL TrainEnginesThenWagonsSorter(const EngineID *a, const EngineID *b)
{
	int val_a = (RailVehInfo(*a)->railveh_type == RAILVEH_WAGON ? 1 : 0);
	int val_b = (RailVehInfo(*b)->railveh_type == RAILVEH_WAGON ? 1 : 0);
	int r = val_a - val_b;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return r;
}


class TemplateReplacementReplaceAllWindow : public Window {
private:
	uint16 line_height;
	Scrollbar	*vscroll_tl,
				*vscroll_tr,
				*vscroll_bo;
	GUIEngineList *engines_left,
				  *engines_right;
	short	selected_left,
			selected_right;
	VirtTrainList *virtualTrains;

public:
	TemplateReplacementReplaceAllWindow(WindowDesc *wdesc) : Window(wdesc)
	{

		this->CreateNestedTree(wdesc);

		this->vscroll_tl = this->GetScrollbar(RPLALL_GUI_SCROLL_TL);
		this->vscroll_tr = this->GetScrollbar(RPLALL_GUI_SCROLL_TR);
		this->vscroll_bo = this->GetScrollbar(RPLALL_GUI_SCROLL_BO);
		this->vscroll_tl->SetStepSize(16);
		this->vscroll_tr->SetStepSize(16);
		this->vscroll_bo->SetStepSize(16);

		this->FinishInitNested(VEH_TRAIN);

		this->owner = _local_company;

		engines_left = new GUIEngineList();
		engines_right = new GUIEngineList();
		virtualTrains = new VirtTrainList();

		this->GenerateBuyableEnginesList();
		this->GenerateIncludedTemplateList();

		this->line_height = 16;
		this->selected_left = -1;
		this->selected_right = -1;
	}

	~TemplateReplacementReplaceAllWindow()
	{
		for ( uint i=0; i<this->virtualTrains->Length(); ++i )
			delete (*this->virtualTrains)[i]->vt;
		SetWindowClassesDirty(WC_TEMPLATEGUI_MAIN);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch ( widget ) {
			case RPLALL_GUI_MATRIX_TOPLEFT:
			case RPLALL_GUI_MATRIX_TOPRIGHT:
			case RPLALL_GUI_MATRIX_BOTTOM: {
				resize->height = 16;
				size->height = 16;
				break;
			}
		}
	}

	virtual void OnPaint()
	{
 		this->GetWidget<NWidgetCore>(RPLALL_GUI_PANEL_BUTTONFLUFF_3)->colour  = _company_colours[_local_company];

		this->DrawWidgets();
	}

	virtual void OnResize()
	{
		NWidgetCore *nwi_tl = this->GetWidget<NWidgetCore>(RPLALL_GUI_MATRIX_TOPLEFT);
		this->vscroll_tl->SetCapacityFromWidget(this, RPLALL_GUI_MATRIX_TOPLEFT);
		nwi_tl->widget_data = (this->vscroll_tl->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);

		NWidgetCore *nwi_tr = this->GetWidget<NWidgetCore>(RPLALL_GUI_MATRIX_TOPRIGHT);
		this->vscroll_tr->SetCapacityFromWidget(this, RPLALL_GUI_MATRIX_TOPRIGHT);
		nwi_tr->widget_data = (this->vscroll_tr->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);

		NWidgetCore *nwi_bo = this->GetWidget<NWidgetCore>(RPLALL_GUI_MATRIX_BOTTOM);
		this->vscroll_bo->SetCapacityFromWidget(this, RPLALL_GUI_MATRIX_BOTTOM);
		nwi_bo->widget_data = (this->vscroll_bo->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case RPLALL_GUI_MATRIX_TOPLEFT: {
				this->DrawEngineList(r, true);
				break;
			}
			case RPLALL_GUI_MATRIX_TOPRIGHT: {
				this->DrawEngineList(r, false);
				break;
			}
			case RPLALL_GUI_MATRIX_BOTTOM: {
				this->DrawVirtualTrains(r);
				break;
			}
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch(widget) {
			case RPLALL_GUI_MATRIX_TOPLEFT: {
				uint16 newindex = (uint16)((pt.y - this->nested_array[RPLALL_GUI_MATRIX_TOPLEFT]->pos_y) / this->line_height) + this->vscroll_tl->GetPosition();
				if ( newindex >= this->engines_left->Length() || newindex==this->selected_left )
					this->selected_left = -1;
				else
					this->selected_left = newindex;
				this->SetDirty();
				break;
			}
			case RPLALL_GUI_MATRIX_TOPRIGHT: {
				uint16 newindex = (uint16)((pt.y - this->nested_array[RPLALL_GUI_MATRIX_TOPRIGHT]->pos_y) / this->line_height) + this->vscroll_tr->GetPosition();
				if ( newindex > this->engines_right->Length() || newindex==this->selected_right )
					this->selected_right = -1;
				else
					this->selected_right = newindex;
				this->SetDirty();
				break;
			}
			case RPLALL_GUI_BUTTON_RPLALL: {
				this->ReplaceAll();
				break;
			}
			case RPLALL_GUI_BUTTON_APPLY: {
				// check if we actually did anything so far, if not, applying is forbidden
				if ( this->virtualTrains->Length() == 0 )
					return;
				// first delete all current templates
				this->DeleteAllTemplateTrains();
				// then build a new list from the current virtual trains
				for ( uint i=0; i<this->virtualTrains->Length(); ++i ) {
					// the relevant info struct
					VirtTrainInfo *vti = (*this->virtualTrains)[i];
					// setup template from contained train
					Train *t = vti->vt;
					TemplateVehicle *tv = TemplateVehicleFromVirtualTrain(t);
					// restore template specific stuff
					tv->reuse_depot_vehicles		= vti->reuse_depot_vehicles;
					tv->keep_remaining_vehicles		= vti->keep_remaining_vehicles;
					tv->refit_as_template			= vti->refit_as_template;
					tv->cargo_type					= vti->cargo_type;
					tv->cargo_subtype				= vti->cargo_subtype;
					// use the original_index information to repoint the relevant TemplateReplacement if existing
					TemplateReplacement *tr = GetTemplateReplacementByTemplateID(vti->original_index);
					if ( tr )
						tr->sel_template = tv->index;
				}
				// then close this window and return to parent
				delete this;
				break;
			}
			case RPLALL_GUI_BUTTON_CANCEL: {
				delete this;
				break;
			}
		}
	}

	bool HasTemplateWithEngine(EngineID eid) const
	{
		const TemplateVehicle *tv;
		FOR_ALL_TEMPLATES(tv) {
			if ( tv->Prev() || tv->owner != _local_company ) continue;
			for ( const TemplateVehicle *tmp=tv; tmp; tmp=tmp->GetNextUnit() ) {
				if ( tmp->engine_type == eid )
					return true;
			}
		}
		return false;
	}

	void GenerateVirtualTrains()
	{
		this->virtualTrains->Clear();

		TemplateVehicle *tv;
		FOR_ALL_TEMPLATES(tv) {
			if ( !tv->Prev() && tv->owner==this->owner ) {
				// setup template train
				Train *newtrain = VirtualTrainFromTemplateVehicle(tv);
				VirtTrainInfo *vti = new VirtTrainInfo(newtrain);
				// store template specific stuff
				vti->original_index				= tv->index;
				vti->reuse_depot_vehicles		= tv->reuse_depot_vehicles;
				vti->keep_remaining_vehicles	= tv->keep_remaining_vehicles;
				vti->refit_as_template			= tv->refit_as_template;
				vti->cargo_type					= tv->cargo_type;
				vti->cargo_subtype				= tv->cargo_subtype;
				// add new info struct
				*this->virtualTrains->Append() = vti;
			}
		}

		this->vscroll_bo->SetCount(this->virtualTrains->Length());
	}

	void DeleteAllTemplateTrains()
	{
		TemplateVehicle *tv, *tmp;
		FOR_ALL_TEMPLATES(tv) {
			tmp = tv;
			if ( tmp->Prev()==0 && tmp->owner==this->owner )
				delete tmp;
		}
	}

	void GenerateIncludedTemplateList()
	{
		int num_engines = 0;
		int num_wagons  = 0;

		this->engines_left->Clear();

		const Engine *e;
		FOR_ALL_ENGINES_OF_TYPE(e, VEH_TRAIN) {
			EngineID eid = e->index;
			const RailVehicleInfo*rvi = &e->u.rail;

			if ( !HasTemplateWithEngine(eid) ) continue;

			*this->engines_left->Append() = eid;

			if (rvi->railveh_type != RAILVEH_WAGON) {
				num_engines++;
			} else {
				num_wagons++;
			}
		}
		this->vscroll_tl->SetCount(this->engines_left->Length());
	}

	bool VirtualTrainHasEngineID(EngineID eid)
	{

		for ( uint i=0; i<this->virtualTrains->Length(); ++i ) {
			const Train *tmp = (*this->virtualTrains)[i]->vt;
			for ( ; tmp; tmp=tmp->Next() )
				if ( tmp->engine_type == eid )
					return true;
		}
		return false;
	}

	// after 'replace all' we need to replace the currently used templates as well
	void RebuildIncludedTemplateList() {
		// first remove all engine ids
		for ( uint i=0; i<this->engines_left->Length(); ++i ) {
			EngineID entry = (*this->engines_left)[i];
			if ( !VirtualTrainHasEngineID(entry) )
				this->engines_left->Erase(&((*this->engines_left)[i]));
		}
	}

	void ReplaceAll()
	{

		if ( this->selected_left==-1 || this->selected_right==-1 )
			return;

		EngineID eid_orig = (*this->engines_left)[this->selected_left];
		EngineID eid_repl = (*this->engines_right)[this->selected_right];

		if ( eid_orig == eid_repl )
			return;

		if ( this->virtualTrains->Length() == 0 )
			this->GenerateVirtualTrains();

		for ( uint i=0; i<this->virtualTrains->Length(); ++i ) {
			Train *tmp = (*this->virtualTrains)[i]->vt;
			while ( tmp ) {
				if ( tmp->engine_type == eid_orig ) {
					// build a new virtual rail vehicle and test for success
					Train *nt = CmdBuildVirtualRailVehicle(eid_repl);
					if ( !nt ) continue;
					// include the (probably) new engine into the 'included'-list
					this->engines_left->Include( nt->engine_type );
					// advance the tmp pointer in the chain, otherwise it would get deleted later on
					Train *to_del = tmp;
					tmp = tmp->GetNextUnit();
					// first move the new virtual rail vehicle behind to_del
					CommandCost move = CmdMoveRailVehicle(INVALID_TILE, DC_EXEC, nt->index|(1<<21), to_del->index, 0);
					// then move to_del away from the chain and delete it
					move = CmdMoveRailVehicle(INVALID_TILE, DC_EXEC, to_del->index|(1<<21), INVALID_VEHICLE, 0);
					(*this->virtualTrains)[i]->vt = nt->First();
					delete to_del;
				} else {
					tmp = tmp->GetNextUnit();
				}
			}
		}
		this->selected_left = -1;
		// rebuild the left engines list as some engines might not be there anymore
		this->RebuildIncludedTemplateList();
		this->SetDirty();
	}

	void GenerateBuyableEnginesList()
	{
		int num_engines = 0;
		int num_wagons  = 0;

		this->engines_right->Clear();

		const Engine *e;
		FOR_ALL_ENGINES_OF_TYPE(e, VEH_TRAIN) {
			EngineID eid = e->index;
			const RailVehicleInfo *rvi = &e->u.rail;

			if (!IsEngineBuildable(eid, VEH_TRAIN, _local_company)) continue;

			*this->engines_right->Append() = eid;

			if (rvi->railveh_type != RAILVEH_WAGON) {
				num_engines++;
			} else {
				num_wagons++;
			}
		}

		/* make engines first, and then wagons, sorted by ListPositionOfEngine() */
		EngList_Sort(this->engines_right, TrainEnginesThenWagonsSorter);

		this->vscroll_tr->SetCount(this->engines_right->Length());
	}

	void DrawEngineList(const Rect &r, bool left) const//, GUIEngineList el, Scrollbar* sb) const
	{
		uint16 y = r.top;
		uint32 eid;

		Scrollbar *sb;
		const GUIEngineList *el;

		if ( left ) {
			sb = this->vscroll_tl;
			el = this->engines_left;
		} else {
			sb = this->vscroll_tr;
			el = this->engines_right;
		}

		int maximum = min((int)sb->GetCapacity(), (int)el->Length()) + sb->GetPosition();

		for ( int i=sb->GetPosition(); i<maximum; ++i ) {

			eid = (*el)[i];

			/* Draw a grey background rectangle if the current line is the selected one */
			if ( (left && this->selected_left == i) || (!left && this->selected_right == i) )
				GfxFillRect(r.left, y, r.right, y+this->line_height, _colour_gradient[COLOUR_GREY][3]);

			/* Draw a description string of the current engine */
			SetDParam(0, eid);
			DrawString(r.left+100, r.right, y+4, STR_ENGINE_NAME, TC_BLACK);

			/* Draw the engine */
			DrawVehicleEngine( r.left, r.right, r.left+29, y+8, eid, GetEnginePalette(eid, _local_company), EIT_PURCHASE );

			y += this->line_height;
		}
	}

	void DrawVirtualTrains(const Rect &r) const
	{
		uint16 y = r.top;

		uint16 max = min(virtualTrains->Length(), this->vscroll_bo->GetCapacity());

		for ( uint16 i=vscroll_bo->GetPosition(); i<max+vscroll_bo->GetPosition(); ++i ) {
			/* Draw a virtual train*/
			DrawTrainImage( (*this->virtualTrains)[i]->vt, r.left+32, r.right, y, INVALID_VEHICLE, EIT_PURCHASE, 0, -1 );

			y+= this->line_height;
		}
	}
};

void ShowTemplateReplaceAllGui()
{
	new TemplateReplacementReplaceAllWindow(&_template_replace_replaceall_desc);
}
