/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file event_logs.h Functions related to event logging. */

#ifndef EVENT_LOGS_H
#define EVENT_LOGS_H

#include "core/enum_type.hpp"
#include "date_type.h"
#include <time.h>

enum GameEventFlags : uint32_t {
	GEF_COMPANY_DELETE       = 1 << 0, ///< (d) A company has been deleted
	GEF_COMPANY_MERGE        = 1 << 1, ///< (m) A company has been bought by another
	GEF_RELOAD_NEWGRF        = 1 << 2, ///< (n) ReloadNewGRFData() has been called
	GEF_TBTR_REPLACEMENT     = 1 << 3, ///< (t) CMD_TEMPLATE_REPLACE_VEHICLE has been called
	GEF_DISASTER_VEH         = 1 << 4, ///< (D) A disaster vehicle exists or has been created
	GEF_TRAIN_CRASH          = 1 << 5, ///< (c) A train crash has occurred
	GEF_INDUSTRY_CREATE      = 1 << 6, ///< (i) An industry has been created (in game)
	GEF_INDUSTRY_DELETE      = 1 << 7, ///< (j) An industry has been deleted (in game)
	GEF_VIRT_TRAIN           = 1 << 8, ///< (v) A virtual train has been created
};
DECLARE_ENUM_AS_BIT_SET(GameEventFlags)

extern GameEventFlags _game_events_since_load;
extern GameEventFlags _game_events_overall;

extern time_t _game_load_time;
extern YearMonthDay _game_load_cur_date_ymd;
extern DateFract _game_load_date_fract;
extern uint8_t _game_load_tick_skip_counter;
extern StateTicks _game_load_state_ticks;

inline void RegisterGameEvents(GameEventFlags events)
{
	_game_events_since_load |= events;
	_game_events_overall |= events;
}

char *DumpGameEventFlags(GameEventFlags events, char *b, const char *last);

void AppendSpecialEventsLogEntry(std::string message);
char *DumpSpecialEventsLog(char *buffer, const char *last);
void ClearSpecialEventsLog();

void LogGameLoadDateTimes(char *buffer, const char *last);

#endif /* EVENT_LOGS_H */
