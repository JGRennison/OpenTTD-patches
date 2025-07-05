#include "../stdafx.h"

#include "../tbtr_template_vehicle.h"

#include "saveload.h"

struct TemplateReplacement {
	GroupID group;
	TemplateID sel_template;
};

static const NamedSaveLoad _template_replacement_desc[] = {
	NSL("sel_template", SLE_VAR(TemplateReplacement, sel_template, SLE_UINT16)),
	NSL("group",        SLE_VAR(TemplateReplacement, group, SLE_UINT16)),
};

static void Save_TMPL_RPLS()
{
	SaveLoadTableData slt = SlTableHeader(_template_replacement_desc);

	size_t index = 0;
	TemplateReplacement tr{};
	for (const auto &it : _template_replacements) {
		tr.group = it.first;
		tr.sel_template = it.second;
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&tr, slt);
	}
}

static void Load_TMPL_RPLS()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_template_replacement_desc);

	int index;
	TemplateReplacement tr{};
	while ((index = SlIterateArray()) != -1) {
		SlObjectLoadFiltered(&tr, slt);
		_template_replacements[tr.group] = tr.sel_template;
	}
	ReindexTemplateReplacements();
}

extern const ChunkHandler template_replacement_chunk_handlers[] = {
	{ 'TRPL', Save_TMPL_RPLS, Load_TMPL_RPLS, nullptr, nullptr, CH_TABLE },
};

extern const ChunkHandlerTable _template_replacement_chunk_handlers(template_replacement_chunk_handlers);
