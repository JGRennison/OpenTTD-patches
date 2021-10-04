/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file event_logs.cpp Functions related to event logging. */

#include "stdafx.h"
#include "event_logs.h"
#include "string_func.h"

#include "safeguards.h"

GameEventFlags _game_events_since_load;
GameEventFlags _game_events_overall;

time_t _game_load_time;

char *DumpGameEventFlags(GameEventFlags events, char *b, const char *last)
{
	if (b <= last) *b = 0;
	auto dump = [&](char c, GameEventFlags ev) {
		if (events & ev) b += seprintf(b, last, "%c", c);
	};
	dump('d', GEF_COMPANY_DELETE);
	dump('m', GEF_COMPANY_MERGE);
	dump('n', GEF_RELOAD_NEWGRF);
	dump('t', GEF_TBTR_REPLACEMENT);
	dump('D', GEF_DISASTER_VEH);
	dump('c', GEF_TRAIN_CRASH);
	dump('i', GEF_INDUSTRY_CREATE);
	dump('j', GEF_INDUSTRY_DELETE);
	dump('v', GEF_VIRT_TRAIN);
	return b;
}
