/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file scope_info.cpp Scope info debug functions. */

#include "stdafx.h"
#include "scope_info.h"
#include "string_func.h"
#include "strings_func.h"
#include "company_base.h"
#include "vehicle_base.h"
#include "table/strings.h"

#include "safeguards.h"

#ifdef USE_SCOPE_INFO

std::vector<std::function<int(char *, const char *)>> _scope_stack;

int WriteScopeLog(char *buf, const char *last)
{
	char *b = buf;
	if (!_scope_stack.empty()) {
		b += seprintf(b, last, "Within context:");
		int depth = 0;
		for (auto it = _scope_stack.rbegin(); it != _scope_stack.rend(); ++it, depth++) {
			b += seprintf(b, last, "\n    %2d: ", depth);
			b += (*it)(b, last);
		}
		b += seprintf(b, last, "\n\n");
	}
	return b - buf;
}

// helper functions
char *DumpCompanyInfo(int company_id)
{
	char buf[256];
	char *b = buf + seprintf(buf, lastof(buf), "%d (", company_id);
	SetDParam(0, company_id);
	b = GetString(b, STR_COMPANY_NAME, lastof(buf));
	b += seprintf(b, lastof(buf), ")");
	return stredup(buf, lastof(buf));
}

char *DumpVehicleInfo(const Vehicle *v)
{
	char buf[256];
	char *b = buf;
	if (v) {
		b += seprintf(b, lastof(buf), "veh: %u: (", v->index);
		SetDParam(0, v->index);
		b = GetString(b, STR_VEHICLE_NAME, lastof(buf));
		if (v->First() && v->First() != v) {
			b += seprintf(b, lastof(buf), "), front: %u: (", v->First()->index);
			SetDParam(0, v->First()->index);
			b = GetString(b, STR_VEHICLE_NAME, lastof(buf));
		}
		b += seprintf(b, lastof(buf), ")");
	} else {
		b += seprintf(b, lastof(buf), "veh: NULL");
	}
	return stredup(buf, lastof(buf));
}

#endif
