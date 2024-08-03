#include "../stdafx.h"

#include "../tbtr_template_vehicle.h"

#include "saveload.h"

static const NamedSaveLoad _template_replacement_desc[] = {
	NSL("sel_template", SLE_VAR(TemplateReplacement, sel_template, SLE_UINT16)),
	NSL("group",        SLE_VAR(TemplateReplacement, group, SLE_UINT16)),
};

static void Save_TMPL_RPLS()
{
	SaveLoadTableData slt = SlTableHeader(_template_replacement_desc);

	for (TemplateReplacement *tr : TemplateReplacement::Iterate()) {
		SlSetArrayIndex(tr->index);
		SlObjectSaveFiltered(tr, slt);
	}
}

static void Load_TMPL_RPLS()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_template_replacement_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		TemplateReplacement *tr = new (index) TemplateReplacement();
		SlObjectLoadFiltered(tr, slt);
	}
	ReindexTemplateReplacements();
}

extern const ChunkHandler template_replacement_chunk_handlers[] = {
	{ 'TRPL', Save_TMPL_RPLS, Load_TMPL_RPLS, nullptr, nullptr, CH_TABLE },
};

extern const ChunkHandlerTable _template_replacement_chunk_handlers(template_replacement_chunk_handlers);
