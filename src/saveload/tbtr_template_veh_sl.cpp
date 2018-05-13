#include "../stdafx.h"

#include "../tbtr_template_vehicle.h"
#include "../tbtr_template_vehicle_func.h"
#include "../train.h"
#include "../company_base.h"
#include "../core/backup_type.hpp"
#include "../core/random_func.hpp"

#include "saveload.h"

const SaveLoad* GTD() {

	static const SaveLoad _template_veh_desc[] = {
		SLE_REF(TemplateVehicle, next, REF_TEMPLATE_VEHICLE),

		SLE_VAR(TemplateVehicle, reuse_depot_vehicles, SLE_UINT8),
		SLE_VAR(TemplateVehicle, keep_remaining_vehicles, SLE_UINT8),
		SLE_VAR(TemplateVehicle, refit_as_template, SLE_UINT8),

		SLE_CONDVAR_X(TemplateVehicle, owner, SLE_VAR_U8 | SLE_FILE_U32, 0, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 3)),
		SLE_CONDVAR_X(TemplateVehicle, owner, SLE_UINT8, 0, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 4)),
		SLE_CONDNULL_X(1, 0, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 3)),

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
		SLE_VAR(TemplateVehicle, weight, SLE_UINT32),
		SLE_VAR(TemplateVehicle, max_te, SLE_UINT32),

		SLE_CONDNULL_X(1, 0, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 3)),
		SLE_CONDNULL_X(4, 0, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 1)),
		SLE_CONDNULL_X(36, 0, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 2, 3)),
		SLE_CONDNULL_X(4, 0, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TEMPLATE_REPLACEMENT, 0, 3)),

		SLE_END()
	};

	static const SaveLoad * const _ret[] = {
		_template_veh_desc,
	};

	return _ret[0];
}

static void Save_TMPLS()
{
	TemplateVehicle *tv;

	FOR_ALL_TEMPLATES(tv) {
		SlSetArrayIndex(tv->index);
		SlObject(tv, GTD());
	}
}

static void Load_TMPLS()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		TemplateVehicle *tv = new (index) TemplateVehicle(); //TODO:check with veh sl code
		SlObject(tv, GTD());
	}
}

static void Ptrs_TMPLS()
{
	TemplateVehicle *tv;
	FOR_ALL_TEMPLATES(tv) {
		SlObject(tv, GTD());
	}
}

void AfterLoadTemplateVehicles()
{
	TemplateVehicle *tv;

	FOR_ALL_TEMPLATES(tv) {
		/* Reinstate the previous pointer */
		if (tv->next != NULL) tv->next->previous = tv;
		tv->first =NULL;
	}
	FOR_ALL_TEMPLATES(tv) {
		/* Fill the first pointers */
		if (tv->previous == NULL) {
			for (TemplateVehicle *u = tv; u != NULL; u = u->Next()) {
				u->first = tv;
			}
		}
	}
}

void AfterLoadTemplateVehiclesUpdateImage()
{
	TemplateVehicle *tv;

	SavedRandomSeeds saved_seeds;
	SaveRandomSeeds(&saved_seeds);

	if (!SlXvIsFeaturePresent(XSLFI_TEMPLATE_REPLACEMENT, 3)) {
		FOR_ALL_TEMPLATES(tv) {
			if (tv->Prev() == NULL && !Company::IsValidID(tv->owner)) {
				// clean up leftover template vehicles which no longer have a valid owner
				delete tv;
			}
		}
	}

	FOR_ALL_TEMPLATES(tv) {
		if (tv->Prev() == NULL) {
			Backup<CompanyByte> cur_company(_current_company, tv->owner, FILE_LINE);
			StringID err;
			Train* t = VirtualTrainFromTemplateVehicle(tv, err);
			if (t != NULL) {
				int tv_len = 0;
				for (TemplateVehicle *u = tv; u != NULL; u = u->Next()) {
					tv_len++;
				}
				int t_len = 0;
				for (Train *u = t; u != NULL; u = u->Next()) {
					t_len++;
				}
				if (t_len == tv_len) {
					Train *v = t;
					for (TemplateVehicle *u = tv; u != NULL; u = u->Next(), v = v->Next()) {
						v->GetImage(DIR_W, EIT_IN_DEPOT, &u->sprite_seq);
						u->image_dimensions.SetFromTrain(v);
					}
				} else {
					DEBUG(misc, 0, "AfterLoadTemplateVehiclesUpdateImage: vehicle count mismatch: %u, %u", t_len, tv_len);
				}
				delete t;
			}
			cur_company.Restore();
		}
	}

	RestoreRandomSeeds(saved_seeds);
}

extern const ChunkHandler _template_vehicle_chunk_handlers[] = {
	{'TMPL', Save_TMPLS, Load_TMPLS, Ptrs_TMPLS, NULL, CH_ARRAY | CH_LAST},
};
