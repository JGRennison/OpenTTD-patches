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
#include "station_base.h"
#include "waypoint_base.h"
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
const char *scope_dumper::CompanyInfo(int company_id)
{
	char *b = this->buffer;
	const char *last = lastof(this->buffer);
	b += seprintf(b, last, "%d (", company_id);
	SetDParam(0, company_id);
	b = GetString(b, STR_COMPANY_NAME, last);
	b += seprintf(b, last, ")");
	return buffer;
}

const char *scope_dumper::VehicleInfo(const Vehicle *v)
{
	char *b = this->buffer;
	const char *last = lastof(this->buffer);
	auto dump_flags = [&](const Vehicle *u) {
		auto dump = [&](char c, bool flag) {
			if (flag) b += seprintf(b, last, "%c", c);
		};
		b += seprintf(b, last, "st:");
		dump('F', HasBit(u->subtype, GVSF_FRONT));
		dump('A', HasBit(u->subtype, GVSF_ARTICULATED_PART));
		dump('W', HasBit(u->subtype, GVSF_WAGON));
		dump('E', HasBit(u->subtype, GVSF_ENGINE));
		dump('f', HasBit(u->subtype, GVSF_FREE_WAGON));
		dump('M', HasBit(u->subtype, GVSF_MULTIHEADED));
		dump('V', HasBit(u->subtype, GVSF_VIRTUAL));
		b += seprintf(b, last, ", vs:");
		dump('H', u->vehstatus & VS_HIDDEN);
		dump('S', u->vehstatus & VS_STOPPED);
		dump('U', u->vehstatus & VS_UNCLICKABLE);
		dump('D', u->vehstatus & VS_DEFPAL);
		dump('s', u->vehstatus & VS_TRAIN_SLOWING);
		dump('X', u->vehstatus & VS_SHADOW);
		dump('B', u->vehstatus & VS_AIRCRAFT_BROKEN);
		dump('C', u->vehstatus & VS_CRASHED);
		b += seprintf(b, last, ", t:%X", u->tile);
	};
	if (v) {
		b += seprintf(b, last, "veh: %u: (", v->index);
		if (Vehicle::GetIfValid(v->index) != v) {
			b += seprintf(b, last, "INVALID PTR: %p)", v);
			return this->buffer;
		}
		SetDParam(0, v->index);
		b = GetString(b, STR_VEHICLE_NAME, last);
		b += seprintf(b, last, ", c:%d, ", (int) v->owner);
		dump_flags(v);
		if (v->First() && v->First() != v) {
			b += seprintf(b, last, ", front: %u: (", v->First()->index);
			if (Vehicle::GetIfValid(v->First()->index) != v->First()) {
				b += seprintf(b, last, "INVALID PTR: %p)", v->First());
				return this->buffer;
			}
			SetDParam(0, v->First()->index);
			b = GetString(b, STR_VEHICLE_NAME, last);
			b += seprintf(b, last, ", ");
			dump_flags(v->First());
			b += seprintf(b, last, ")");
		}
		b += seprintf(b, last, ")");
	} else {
		b += seprintf(b, last, "veh: NULL");
	}
	return this->buffer;
}

const char *scope_dumper::StationInfo(const BaseStation *st)
{
	char *b = this->buffer;
	const char *last = lastof(this->buffer);

	if (st) {
		const bool waypoint = Waypoint::IsExpected(st);
		b += seprintf(b, last, "%s: %u: (", waypoint ? "waypoint" : "station", st->index);
		SetDParam(0, st->index);
		b = GetString(b, waypoint ? STR_WAYPOINT_NAME : STR_STATION_NAME, last);
		b += seprintf(b, last, ", c:%d, facil: ", (int) st->owner);
		auto dump_facil = [&](char c, StationFacility flag) {
			if (st->facilities & flag) b += seprintf(b, last, "%c", c);
		};
		dump_facil('R', FACIL_TRAIN);
		dump_facil('T', FACIL_TRUCK_STOP);
		dump_facil('B', FACIL_BUS_STOP);
		dump_facil('A', FACIL_AIRPORT);
		dump_facil('D', FACIL_DOCK);
		dump_facil('W', FACIL_WAYPOINT);
		b += seprintf(b, last, ")");
	} else {
		b += seprintf(b, last, "station/waypoint: NULL");
	}
	return this->buffer;
}

#endif
