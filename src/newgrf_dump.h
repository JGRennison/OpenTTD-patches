/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_dump.h Functions/types related to NewGRF sprite group dumping. */

#ifndef NEWGRF_DUMP_H
#define NEWGRF_DUMP_H

#include "3rdparty/robin_hood/robin_hood.h"
#include <functional>

enum DumpSpriteGroupPrintOp {
	DSGPO_PRINT,
	DSGPO_START,
	DSGPO_END,
	DSGPO_NFO_LINE,
};

using DumpSpriteGroupPrinter = std::function<void(const struct SpriteGroup *, DumpSpriteGroupPrintOp, uint32_t, std::string_view)>;

struct SpriteGroupDumper {
	bool use_shadows = false;
	bool more_details = false;

private:
	DumpSpriteGroupPrinter print_fn;

	const SpriteGroup *top_default_group = nullptr;
	const SpriteGroup *top_graphics_group = nullptr;
	robin_hood::unordered_flat_set<const struct DeterministicSpriteGroup *> seen_dsgs;

	enum SpriteGroupDumperFlags {
		SGDF_DEFAULT          = 1 << 0,
		SGDF_RANGE            = 1 << 1,
	};

	void DumpSpriteGroupAdjust(struct format_buffer &buffer, const struct DeterministicSpriteGroupAdjust &adjust, uint32_t &highlight_tag, uint &conditional_indent);
	void DumpSpriteGroup(struct format_buffer &buffer, const struct SpriteGroup *sg, const char *prefix, uint flags);

public:
	SpriteGroupDumper(DumpSpriteGroupPrinter print) : print_fn(print) {}

	void DumpSpriteGroup(const SpriteGroup *sg, uint flags);

	void Print(std::string_view msg)
	{
		this->print_fn(nullptr, DSGPO_PRINT, 0, msg);
	}
};

#endif /* NEWGRF_DEBUG_H */
