/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_extension.h NewGRF extension support. */

#ifndef NEWGRF_EXTENSION_H
#define NEWGRF_EXTENSION_H

enum Action0RemapPropertyIds {
	A0RPI_UNKNOWN_IGNORE = 0x200,
	A0RPI_UNKNOWN_ERROR,

	A0RPI_STATION_MIN_BRIDGE_HEIGHT,
	A0RPI_STATION_DISALLOWED_BRIDGE_PILLARS,
	A0RPI_BRIDGE_MENU_ICON,
	A0RPI_BRIDGE_PILLAR_FLAGS,
	A0RPI_BRIDGE_AVAILABILITY_FLAGS,
	A0RPI_RAILTYPE_ENABLE_PROGRAMMABLE_SIGNALS,
	A0RPI_RAILTYPE_ENABLE_NO_ENTRY_SIGNALS,
	A0RPI_RAILTYPE_ENABLE_RESTRICTED_SIGNALS,
	A0RPI_RAILTYPE_DISABLE_REALISTIC_BRAKING,
	A0RPI_RAILTYPE_ENABLE_SIGNAL_RECOLOUR,
	A0RPI_RAILTYPE_EXTRA_ASPECTS,
	A0RPI_ROADTYPE_EXTRA_FLAGS,
	A0RPI_GLOBALVAR_EXTRA_STATION_NAMES,
	A0RPI_SIGNALS_ENABLE_PROGRAMMABLE_SIGNALS,
	A0RPI_SIGNALS_ENABLE_NO_ENTRY_SIGNALS,
	A0RPI_SIGNALS_ENABLE_RESTRICTED_SIGNALS,
	A0RPI_SIGNALS_ENABLE_SIGNAL_RECOLOUR,
	A0RPI_SIGNALS_EXTRA_ASPECTS,
	A0RPI_OBJECT_USE_LAND_GROUND,
	A0RPI_OBJECT_EDGE_FOUNDATION_MODE,
	A0RPI_OBJECT_FLOOD_RESISTANT,
};


/** Action14 feature definition */
struct GRFFeatureInfo {
	const char *name; // nullptr indicates the end of the list
	uint16 version;

	/** Create empty object used to identify the end of a list. */
	GRFFeatureInfo() :
		name(nullptr),
		version(0)
	{}

	GRFFeatureInfo(const char *name, uint16 version) :
		name(name),
		version(version)
	{}
};

#endif
