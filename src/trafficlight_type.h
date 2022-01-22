/* $Id: trafficlight_type.h $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file trafficlight_type.h Types related to the states of trafficlights.*/

/* Which state a traffic light is in. */
enum TrafficLightState
{
    TLS_OFF,               ///< all lights are off (during roadworks; always in scedit)
    TLS_X_GREEN_Y_RED,     ///< SW and NE are green, NW and SE are red
    TLS_X_YELLOW_Y_RED,    ///< SW and NE are yellow, NW and SE are red
    TLS_X_RED_Y_REDYELLOW, ///< SW and NE are red, NW and SE are red-yellow
    TLS_X_RED_Y_GREEN,     ///< SW and NE are red, NW and SE are green
    TLS_X_RED_Y_YELLOW,    ///< SW and NE are red, NW and SE are yellow
    TLS_X_REDYELLOW_Y_RED, ///< SW and NE are red-yellow, NW and SE are red
};