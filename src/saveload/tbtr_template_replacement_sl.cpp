#include "../stdafx.h"

#include "../tbtr_template_vehicle.h"

#include "saveload.h"

static const SaveLoad _template_replacement_desc[] = {
	SLE_VAR(TemplateReplacement, sel_template, SLE_UINT16),
	SLE_VAR(TemplateReplacement, group, SLE_UINT16),
	SLE_END()
};

static void Save_TMPL_RPLS()
{
	TemplateReplacement *tr;

	FOR_ALL_TEMPLATE_REPLACEMENTS(tr) {
		SlSetArrayIndex(tr->index);
		SlObject(tr, _template_replacement_desc);
	}
}

static void Load_TMPL_RPLS()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		TemplateReplacement *tr = new (index) TemplateReplacement();
		SlObject(tr, _template_replacement_desc);
	}
}

extern const ChunkHandler _template_replacement_chunk_handlers[] = {
	{'TRPL', Save_TMPL_RPLS, Load_TMPL_RPLS, nullptr, nullptr, CH_ARRAY | CH_LAST},
};
