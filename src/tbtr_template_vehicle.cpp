#include "stdafx.h"
#include "company_func.h"
#include "train.h"
#include "command_func.h"
#include "engine_func.h"
#include "vehicle_func.h"
#include "autoreplace_func.h"
#include "autoreplace_gui.h"
#include "group.h"
#include "articulated_vehicles.h"
#include "core/random_func.hpp"
#include "core/pool_type.hpp"
#include "engine_type.h"
#include "group_type.h"
#include "core/pool_func.hpp"

#include "table/strings.h"

#include "newgrf.h"

#include "vehicle_type.h"
#include "vehicle_base.h"
#include "vehicle_func.h"

#include "table/train_cmd.h"


#include "tbtr_template_vehicle.h"

// since doing stuff with sprites
#include "newgrf_spritegroup.h"
#include "newgrf_engine.h"
#include "newgrf_cargo.h"

TemplatePool _template_pool("TemplatePool");
INSTANTIATE_POOL_METHODS(Template)

TemplateReplacementPool _template_replacement_pool("TemplateReplacementPool");
INSTANTIATE_POOL_METHODS(TemplateReplacement)


TemplateVehicle::TemplateVehicle(VehicleType ty, EngineID eid, byte subtypeflag, Owner current_owner)
{
	this->type = ty;
	this->engine_type = eid;

	this->reuse_depot_vehicles = true;
	this->keep_remaining_vehicles = true;

	this->first = this;
	this->next = 0x0;
	this->previous = 0x0;
	this->owner_b = _current_company;

	this->cur_image = SPR_IMG_QUERY;

	this->owner = current_owner;

	this->real_consist_length = 0;
}

TemplateVehicle::~TemplateVehicle() {
	TemplateVehicle *v = this->Next();
	this->SetNext(NULL);

	delete v;
}

/** getting */
void TemplateVehicle::SetNext(TemplateVehicle *v) { this->next = v; }
void TemplateVehicle::SetPrev(TemplateVehicle *v) { this->previous = v; }
void TemplateVehicle::SetFirst(TemplateVehicle *v) { this->first = v; }

TemplateVehicle* TemplateVehicle::GetNextUnit() const
{
		TemplateVehicle *tv = this->Next();
		while ( tv && HasBit(tv->subtype, GVSF_ARTICULATED_PART) ) tv = tv->Next();
		if ( tv && HasBit(tv->subtype, GVSF_MULTIHEADED) && !HasBit(tv->subtype, GVSF_ENGINE) ) tv = tv->Next();
		return tv;
}

TemplateVehicle* TemplateVehicle::GetPrevUnit()
{
	TemplateVehicle *tv = this->Prev();
	while ( tv && HasBit(tv->subtype, GVSF_ARTICULATED_PART|GVSF_ENGINE) ) tv = tv->Prev();
	if ( tv && HasBit(tv->subtype, GVSF_MULTIHEADED|GVSF_ENGINE) ) tv = tv->Prev();
	return tv;
}

/** setting */
void appendTemplateVehicle(TemplateVehicle *orig, TemplateVehicle *newv)
{
	if ( !orig ) return;
	while ( orig->Next() ) orig=orig->Next();
	orig->SetNext(newv);
	newv->SetPrev(orig);
	newv->SetFirst(orig->First());
}

void insertTemplateVehicle(TemplateVehicle *orig, TemplateVehicle *newv, TemplateVehicle *insert_after)
{
	if ( !orig || !insert_after ) return;
	TemplateVehicle *insert_before = insert_after->Next();
	insert_after->SetNext(newv);
	insert_before->SetPrev(newv);
	newv->SetPrev(insert_after);
	newv->SetNext(insert_before);
	newv->SetFirst(insert_after);
}

/** Length()
 * @return: length of vehicle, including current part
 */
int TemplateVehicle::Length() const
{
	int l=1;
	const TemplateVehicle *tmp=this;
	while ( tmp->Next() ) { tmp=tmp->Next(); l++; }
	return l;
}

TemplateReplacement* GetTemplateReplacementByGroupID(GroupID gid)
{
	TemplateReplacement *tr;
	FOR_ALL_TEMPLATE_REPLACEMENTS(tr) {
		if ( tr->Group() == gid )
			return tr;
	}
	return 0;
}

TemplateReplacement* GetTemplateReplacementByTemplateID(TemplateID tid) {
	TemplateReplacement *tr;
	FOR_ALL_TEMPLATE_REPLACEMENTS(tr) {
		if ( tr->Template() == tid )
			return tr;
	}
	return 0;
}

bool IssueTemplateReplacement(GroupID gid, TemplateID tid) {

	TemplateReplacement *tr = GetTemplateReplacementByGroupID(gid);

	if ( tr ) {
		/* Then set the new TemplateVehicle and return */
		tr->SetTemplate(tid);
		return true;
	}

	else if ( TemplateReplacement::CanAllocateItem() ) {
		tr = new TemplateReplacement(gid, tid);
		return true;
	}

	else return false;
}

short TemplateVehicle::NumGroupsUsingTemplate() const
{
	short amount = 0;
	const TemplateReplacement *tr;
	FOR_ALL_TEMPLATE_REPLACEMENTS(tr) {
		if ( tr->sel_template == this->index )
			amount++;
	}
	return amount;
}

short TemplateVehicle::CountEnginesInChain()
{
	TemplateVehicle *tv = this->first;
	short count = 0;
	for ( ; tv; tv=tv->GetNextUnit() )
		if ( HasBit(tv->subtype, GVSF_ENGINE ) )
			count++;
	return count;
}

short deleteIllegalTemplateReplacements(GroupID g_id)
{
	short del_amount = 0;
	const TemplateReplacement *tr;
	FOR_ALL_TEMPLATE_REPLACEMENTS(tr) {
		if ( tr->group == g_id ) {
			delete tr;
			del_amount++;
		}
	}
	return del_amount;
}




































