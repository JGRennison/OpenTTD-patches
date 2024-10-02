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
#include "core/format.hpp"
#include "table/strings.h"

#include "safeguards.h"

#if !defined(DISABLE_SCOPE_INFO)

ScopeStackRecord *_scope_stack_head = nullptr;

void WriteScopeLog(struct format_target &buffer)
{
	if (_scope_stack_head != nullptr) {
		buffer.append("Within context:");
		int depth = 0;
		for (ScopeStackRecord *record = _scope_stack_head; record != nullptr; record = record->next, depth++) {
			buffer.format("\n    {:2}: ", depth);
			record->functor(record, buffer);
		}
		buffer.append("\n\n");
	}
}

#endif

const char *scope_dumper::CompanyInfo(int company_id)
{
	format_to_fixed_z buf(this->buffer, lastof(this->buffer));
	buf.format("{} (", company_id);
	SetDParam(0, company_id);
	buf.append(GetString(STR_COMPANY_NAME));
	buf.push_back(')');
	buf.finalise();
	return buffer;
}

const char *scope_dumper::VehicleInfo(const Vehicle *v)
{
	format_to_fixed_z buf(this->buffer, lastof(this->buffer));

	auto dump_flags = [&](const Vehicle *u) {
		u->DumpVehicleFlags(buf, true);
	};
	auto dump_name = [&](const Vehicle *u) {
		if (u->type < VEH_COMPANY_END) {
			const char *veh_type[] = {
				"Train",
				"Road Vehicle",
				"Ship",
				"Aircraft",
			};
			buf.append(veh_type[u->type]);
			if (u->unitnumber > 0) {
				buf.format(" {}", u->unitnumber);
			} else {
				buf.append(" [N/A]");
			}
			if (!u->name.empty()) {
				buf.format(" ({})", u->name.c_str());
			}
		} else if (u->type == VEH_EFFECT) {
			buf.format("Effect Vehicle: subtype: {}", u->subtype);
		} else if (u->type == VEH_DISASTER) {
			buf.format("Disaster Vehicle: subtype: {}", u->subtype);
		}
	};
	if (v) {
		buf.format("veh: {}: (", v->index);
		if (Vehicle::GetIfValid(v->index) != v) {
			buf.format("INVALID PTR: {})", fmt::ptr(v));
			return this->buffer;
		}
		dump_name(v);
		buf.format(", c:{}, ", (int)v->owner);
		dump_flags(v);
		if (v->First() && v->First() != v) {
			buf.format(", front: {}: (", v->First()->index);
			if (Vehicle::GetIfValid(v->First()->index) != v->First()) {
				buf.format("INVALID PTR: {})", fmt::ptr(v->First()));
				return this->buffer;
			}
			dump_name(v->First());
			buf.append(", ");
			dump_flags(v->First());
			buf.push_back(')');
		}
		buf.push_back(')');
	} else {
		buf.append("veh: nullptr");
	}
	buf.finalise();
	return this->buffer;
}

const char *scope_dumper::StationInfo(const BaseStation *st)
{
	format_to_fixed_z buf(this->buffer, lastof(this->buffer));

	if (st) {
		const bool waypoint = Waypoint::IsExpected(st);
		buf.format("{}: {}: (", waypoint ? "waypoint" : "station", st->index);
		SetDParam(0, st->index);
		buf.append(GetString(waypoint ? STR_WAYPOINT_NAME : STR_STATION_NAME));
		buf.format(", c:{}, facil: ", (int)st->owner);
		auto dump_facil = [&](char c, StationFacility flag) {
			if (st->facilities & flag) buf.push_back(c);
		};
		dump_facil('R', FACIL_TRAIN);
		dump_facil('T', FACIL_TRUCK_STOP);
		dump_facil('B', FACIL_BUS_STOP);
		dump_facil('A', FACIL_AIRPORT);
		dump_facil('D', FACIL_DOCK);
		dump_facil('W', FACIL_WAYPOINT);
		buf.push_back(')');
	} else {
		buf.append("station/waypoint: nullptr");
	}
	buf.finalise();
	return this->buffer;
}

const char *scope_dumper::TileInfo(TileIndex tile)
{
	format_to_fixed_z tileinfobuf(this->buffer, lastof(this->buffer));
	DumpTileInfo(tileinfobuf, tile);
	tileinfobuf.finalise();
	return this->buffer;
}

const char *scope_dumper::WindowInfo(const Window *w)
{
	format_to_fixed_z buf(this->buffer, lastof(this->buffer));
	DumpWindowInfo(buf, w);
	buf.finalise();
	return this->buffer;
}
