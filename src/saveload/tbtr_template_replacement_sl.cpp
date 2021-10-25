#include "../stdafx.h"

#include "../tbtr_template_vehicle.h"

#include "saveload.h"

static const SaveLoad _template_replacement_desc[] = {
	SLE_VAR(TemplateReplacement, sel_template, SLE_UINT16),
	SLE_VAR(TemplateReplacement, group, SLE_UINT16),
};

static void Save_TMPL_RPLS()
{
	for (TemplateReplacement *tr : TemplateReplacement::Iterate()) {
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
	ReindexTemplateReplacements();
}

extern const ChunkHandler template_replacement_chunk_handlers[] = {
	{ 'TRPL', Save_TMPL_RPLS, Load_TMPL_RPLS, nullptr, nullptr, CH_ARRAY },
};

extern const ChunkHandlerTable _template_replacement_chunk_handlers(template_replacement_chunk_handlers);
