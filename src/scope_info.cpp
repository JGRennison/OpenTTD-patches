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
#include "map_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "table/strings.h"

#include "safeguards.h"

#if !defined(DISABLE_SCOPE_INFO)

ScopeStackRecord *_scope_stack_head = nullptr;

int WriteScopeLog(char *buf, const char *last)
{
	char *b = buf;
	if (_scope_stack_head != nullptr) {
		b += seprintf(b, last, "Within context:");
		int depth = 0;
		for (ScopeStackRecord *record = _scope_stack_head; record != nullptr; record = record->next, depth++) {
			b += seprintf(b, last, "\n    %2d: ", depth);
			b += record->functor(record, b, last);
		}
		b += seprintf(b, last, "\n\n");
	}
	return b - buf;
}

#endif

// helper functions
const char *scope_dumper::CompanyInfo(int company_id)
{
	char *b = this->buffer;
	const char *last = lastof(this->buffer);
	b += seprintf(b, last, "%d (", company_id);
	SetDParam(0, company_id);
	b = strecpy(b, GetString(STR_COMPANY_NAME).c_str(), last);
	b += seprintf(b, last, ")");
	return buffer;
}

const char *scope_dumper::VehicleInfo(const Vehicle *v)
{
	char *b = this->buffer;
	const char *last = lastof(this->buffer);
	auto dump_flags = [&](const Vehicle *u) {
		b = u->DumpVehicleFlags(b, last, true);
	};
	auto dump_name = [&](const Vehicle *u) {
		if (u->type < VEH_COMPANY_END) {
			const char *veh_type[] = {
				"Train",
				"Road Vehicle",
				"Ship",
				"Aircraft",
			};
			b = strecpy(b, veh_type[u->type], last, true);
			if (u->unitnumber > 0) {
				b += seprintf(b, last, " %u", u->unitnumber);
			} else {
				b += seprintf(b, last, " [N/A]");
			}
			if (!u->name.empty()) {
				b += seprintf(b, last, " (%s)", u->name.c_str());
			}
		} else if (u->type == VEH_EFFECT) {
			b += seprintf(b, last, "Effect Vehicle: subtype: %u", u->subtype);
		} else if (u->type == VEH_DISASTER) {
			b += seprintf(b, last, "Disaster Vehicle: subtype: %u", u->subtype);
		}
	};
	if (v) {
		b += seprintf(b, last, "veh: %u: (", v->index);
		if (Vehicle::GetIfValid(v->index) != v) {
			b += seprintf(b, last, "INVALID PTR: %p)", v);
			return this->buffer;
		}
		dump_name(v);
		b += seprintf(b, last, ", c:%d, ", (int) v->owner);
		dump_flags(v);
		if (v->First() && v->First() != v) {
			b += seprintf(b, last, ", front: %u: (", v->First()->index);
			if (Vehicle::GetIfValid(v->First()->index) != v->First()) {
				b += seprintf(b, last, "INVALID PTR: %p)", v->First());
				return this->buffer;
			}
			dump_name(v->First());
			b += seprintf(b, last, ", ");
			dump_flags(v->First());
			b += seprintf(b, last, ")");
		}
		b += seprintf(b, last, ")");
	} else {
		b += seprintf(b, last, "veh: nullptr");
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
		b = strecpy(b, GetString(waypoint ? STR_WAYPOINT_NAME : STR_STATION_NAME).c_str(), last);
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
		b += seprintf(b, last, "station/waypoint: nullptr");
	}
	return this->buffer;
}

const char *scope_dumper::TileInfo(TileIndex tile)
{
	DumpTileInfo(this->buffer, lastof(this->buffer), tile);
	return this->buffer;
}

const char *scope_dumper::WindowInfo(const Window *w)
{
	DumpWindowInfo(this->buffer, lastof(this->buffer), w);
	return this->buffer;
}
