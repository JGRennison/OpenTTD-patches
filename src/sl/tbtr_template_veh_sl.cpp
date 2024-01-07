#include "../stdafx.h"

#include "../tbtr_template_vehicle.h"
#include "../tbtr_template_vehicle_func.h"
#include "../train.h"
#include "../company_base.h"
#include "../core/backup_type.hpp"
#include "../core/random_func.hpp"

#include "saveload.h"

const SaveLoadTable GTD() {

	static const SaveLoad _template_veh_desc[] = {
		SLE_REF(TemplateVehicle, next, REF_TEMPLATE_VEHICLE),

		SLE_VAR(TemplateVehicle, reuse_depot_vehicles, SLE_UINT8),
		SLE_VAR(TemplateVehicle, keep_remaining_vehicles, SLE_UINT8),
		SLE_VAR(TemplateVehicle, refit_as_template, SLE_UINT8),
		SLE_CONDVAR_X(TemplateVehicle, replace_old_only, SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 5)),

		SLE_CONDVAR_X(TemplateVehicle, owner, SLE_VAR_U8 | SLE_FILE_U32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 3)),
		SLE_CONDVAR_X(TemplateVehicle, owner, SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 4)),
		SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 3)),

		SLE_VAR(TemplateVehicle, engine_type, SLE_UINT16),
		SLE_VAR(TemplateVehicle, cargo_type, SLE_UINT8),
		SLE_VAR(TemplateVehicle, cargo_cap, SLE_UINT16),
		SLE_VAR(TemplateVehicle, cargo_subtype, SLE_UINT8),

		SLE_VAR(TemplateVehicle, subtype, SLE_UINT8),
		SLE_VAR(TemplateVehicle, railtype, SLE_UINT8),

		SLE_VAR(TemplateVehicle, index, SLE_UINT32),

		SLE_VAR(TemplateVehicle, real_consist_length, SLE_UINT16),

		SLE_VAR(TemplateVehicle, max_speed, SLE_UINT16),
		SLE_VAR(TemplateVehicle, power, SLE_UINT32),
		SLE_VAR(TemplateVehicle, empty_weight, SLE_UINT32),
		SLE_CONDVAR_X(TemplateVehicle, full_weight, SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 6)),
		SLE_VAR(TemplateVehicle, max_te, SLE_UINT32),
		SLE_CONDVAR_X(TemplateVehicle, air_drag, SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 8)),

		SLE_CONDVAR_X(TemplateVehicle, ctrl_flags, SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 7)),
		SLE_CONDSSTR_X(TemplateVehicle, name, SLE_STR | SLF_ALLOW_CONTROL, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 9)),

		SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 3)),
		SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 1)),
		SLE_CONDNULL_X(36, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 2, 3)),
		SLE_CONDNULL_X(36, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
		SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 3)),
	};

	return _template_veh_desc;
}

static void Save_TMPLS()
{
	for (TemplateVehicle *tv : TemplateVehicle::Iterate()) {
		SlSetArrayIndex(tv->index);
		SlObject(tv, GTD());
	}
}

static void Load_TMPLS()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		TemplateVehicle *tv = new (index) TemplateVehicle();
		SlObject(tv, GTD());
	}
}

static void Ptrs_TMPLS()
{
	for (TemplateVehicle *tv : TemplateVehicle::Iterate()) {
		SlObject(tv, GTD());
	}
}

void AfterLoadTemplateVehicles()
{
	for (TemplateVehicle *tv : TemplateVehicle::Iterate()) {
		/* Reinstate the previous pointer */
		if (tv->next != nullptr) tv->next->previous = tv;
		tv->first = nullptr;
	}
	for (TemplateVehicle *tv : TemplateVehicle::Iterate()) {
		/* Fill the first pointers */
		if (tv->previous == nullptr) {
			for (TemplateVehicle *u = tv; u != nullptr; u = u->Next()) {
				u->first = tv;
			}
		}
	}
}

void AfterLoadTemplateVehiclesUpdate()
{
	SavedRandomSeeds saved_seeds;
	SaveRandomSeeds(&saved_seeds);

	if (!SlXvIsFeaturePresent(XSLFI_TEMPLATE_REPLACEMENT, 3)) {
		for (TemplateVehicle *tv : TemplateVehicle::Iterate()) {
			if (tv->Prev() == nullptr && !Company::IsValidID(tv->owner)) {
				// clean up leftover template vehicles which no longer have a valid owner
				delete tv;
			}
		}
	}

	RestoreRandomSeeds(saved_seeds);

	InvalidateTemplateReplacementImages();
}

void AfterLoadTemplateVehiclesUpdateImages()
{
	InvalidateTemplateReplacementImages();
}

void AfterLoadTemplateVehiclesUpdateProperties()
{
	SavedRandomSeeds saved_seeds;
	SaveRandomSeeds(&saved_seeds);

	for (TemplateVehicle *tv : TemplateVehicle::Iterate()) {
		if (tv->Prev() == nullptr) {
			Backup<CompanyID> cur_company(_current_company, tv->owner, FILE_LINE);
			StringID err;
			Train* t = VirtualTrainFromTemplateVehicle(tv, err, 0);
			if (t != nullptr) {
				uint32_t full_cargo_weight = 0;
				for (Train *u = t; u != nullptr; u = u->Next()) {
					full_cargo_weight += u->GetCargoWeight(u->cargo_cap);
				}
				const GroundVehicleCache *gcache = t->GetGroundVehicleCache();
				tv->max_speed = t->GetDisplayMaxSpeed();
				tv->power = gcache->cached_power;
				tv->empty_weight = gcache->cached_weight;
				tv->full_weight = gcache->cached_weight + full_cargo_weight;
				tv->max_te = gcache->cached_max_te;
				tv->air_drag = gcache->cached_air_drag;
				delete t;
			}
			cur_company.Restore();
		}
	}

	RestoreRandomSeeds(saved_seeds);
}

extern const ChunkHandler template_vehicle_chunk_handlers[] = {
	{ 'TMPL', Save_TMPLS, Load_TMPLS, Ptrs_TMPLS, nullptr, CH_ARRAY },
};

extern const ChunkHandlerTable _template_vehicle_chunk_handlers(template_vehicle_chunk_handlers);
