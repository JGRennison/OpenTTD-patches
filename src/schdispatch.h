/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file schdispatch.h Functions related to scheduled dispatch. */

#ifndef SCHDISPATCH_H
#define SCHDISPATCH_H

#include "date_type.h"
#include "vehicle_type.h"
#include "settings_type.h"

void ShowSchdispatchWindow(const Vehicle *v);

/**
 * Convert date and full date fraction to DateTicksScaled
 * @param date  Current date
 * @param full_date_fract  full date fraction, the number of scaled tick in current day
 * @return DateTicksScaled for ths specified date/faction
 */
inline DateTicksScaled SchdispatchConvertToScaledTick(Date date, uint16 full_date_fract)
{
    return ((DateTicksScaled)date * DAY_TICKS) * _settings_game.economy.day_length_factor + full_date_fract;
}

/**
 * Convert DateTicksScaled to date and full date fraction format
 * @param tick DateTicksScaled to convert
 * @param date  Point to date, for ourput
 * @param full_date_fract  Pointer to uint16, for output
 */
inline void SchdispatchConvertToFullDateFract(DateTicksScaled tick, Date* date, uint16* full_date_fract)
{
    const int full_date = _settings_game.economy.day_length_factor * DAY_TICKS;
    *date = tick / full_date;
    *full_date_fract = tick % full_date;
}

#endif /* SCHDISPATCH_H */
