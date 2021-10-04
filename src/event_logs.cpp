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
#include "date_func.h"
#include "company_func.h"
#include <array>
#include <string>

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

struct SpecialEventLogEntry {
	std::string msg;
	Date date;
	DateFract date_fract;
	uint8 tick_skip_counter;
	CompanyID current_company;
	CompanyID local_company;

	SpecialEventLogEntry() { }

	SpecialEventLogEntry(std::string msg)
			: msg(std::move(msg)), date(_date), date_fract(_date_fract), tick_skip_counter(_tick_skip_counter),
			current_company(_current_company), local_company(_local_company) { }
};

struct SpecialEventLog {
	std::array<SpecialEventLogEntry, 64> log;
	unsigned int count = 0;
	unsigned int next = 0;

	void Reset()
	{
		this->count = 0;
		this->next = 0;
	}
};

static SpecialEventLog _special_event_log;

void AppendSpecialEventsLogEntry(std::string message)
{
	_special_event_log.log[_special_event_log.next] = SpecialEventLogEntry(std::move(message));
	_special_event_log.next = (_special_event_log.next + 1) % _special_event_log.log.size();
	_special_event_log.count++;
}

char *DumpSpecialEventsLog(char *buffer, const char *last)
{
	const unsigned int count = std::min<unsigned int>(_special_event_log.count, 64);
	buffer += seprintf(buffer, last, "Special Events Log:\n Showing most recent %u of %u events\n", count, _special_event_log.count);

	unsigned int log_index = _special_event_log.next;
	for (unsigned int i = 0 ; i < count; i++) {
		if (log_index > 0) {
			log_index--;
		} else {
			log_index = (uint)_special_event_log.log.size() - 1;
		}
		const SpecialEventLogEntry &entry = _special_event_log.log[log_index];

		YearMonthDay ymd;
		ConvertDateToYMD(entry.date, &ymd);
		buffer += seprintf(buffer, last, " %3u | %4i-%02i-%02i, %2i, %3i | cc: %3u, lc: %3u | %s\n",
				i, ymd.year, ymd.month + 1, ymd.day, entry.date_fract, entry.tick_skip_counter, (uint) entry.current_company, (uint) entry.local_company, entry.msg.c_str());
	}
	return buffer;
}

void ClearSpecialEventsLog()
{
	_special_event_log.Reset();
}
