/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_extension.cpp NewGRF extension support. */

#include "stdafx.h"
#include "newgrf.h"
#include "newgrf_extension.h"
#include "table/sprites.h"

#include "safeguards.h"

/** Action14 feature list */
extern const GRFFeatureInfo _grf_feature_list[] = {
	GRFFeatureInfo("feature_test", 1),
	GRFFeatureInfo("property_mapping", 1),
	GRFFeatureInfo("action5_type_id_mapping", 1),
	GRFFeatureInfo("action0_station_prop1B", 1),
	GRFFeatureInfo("action0_station_disallowed_bridge_pillars", 1),
	GRFFeatureInfo("varaction2_station_var42", 1),
	GRFFeatureInfo("more_bridge_types", 1),
	GRFFeatureInfo("action0_bridge_prop14", 1),
	GRFFeatureInfo("action0_bridge_pillar_flags", 1),
	GRFFeatureInfo("action0_bridge_availability_flags", 1),
	GRFFeatureInfo("action5_programmable_signals", 1),
	GRFFeatureInfo("action5_no_entry_signals", 1),
	GRFFeatureInfo("action0_railtype_programmable_signals", 1),
	GRFFeatureInfo("action0_railtype_no_entry_signals", 1),
	GRFFeatureInfo("action0_railtype_restricted_signals", 1),
	GRFFeatureInfo("action0_railtype_disable_realistic_braking", 1),
	GRFFeatureInfo("action0_railtype_recolour", 1),
	GRFFeatureInfo("action0_railtype_extra_aspects", 1),
	GRFFeatureInfo("action0_roadtype_extra_flags", 1),
	GRFFeatureInfo("action0_global_extra_station_names", 1),
	GRFFeatureInfo("action0_signals_programmable_signals", 1),
	GRFFeatureInfo("action0_signals_no_entry_signals", 1),
	GRFFeatureInfo("action0_signals_restricted_signals", 1),
	GRFFeatureInfo("action0_signals_recolour", 1),
	GRFFeatureInfo("action0_signals_extra_aspects", 1),
	GRFFeatureInfo("action3_signals_custom_signal_sprites", 1),
	GRFFeatureInfo("action0_object_use_land_ground", 1),
	GRFFeatureInfo("action0_object_edge_foundation_mode", 1),
	GRFFeatureInfo("action0_object_flood_resistant", 1),
	GRFFeatureInfo(),
};

/** Action14 Action0 remappable property list */
extern const GRFPropertyMapDefinition _grf_action0_remappable_properties[] = {
	GRFPropertyMapDefinition(GSF_STATIONS, A0RPI_STATION_MIN_BRIDGE_HEIGHT, "station_min_bridge_height"),
	GRFPropertyMapDefinition(GSF_STATIONS, A0RPI_STATION_DISALLOWED_BRIDGE_PILLARS, "station_disallowed_bridge_pillars"),
	GRFPropertyMapDefinition(GSF_BRIDGES, A0RPI_BRIDGE_MENU_ICON, "bridge_menu_icon"),
	GRFPropertyMapDefinition(GSF_BRIDGES, A0RPI_BRIDGE_PILLAR_FLAGS, "bridge_pillar_flags"),
	GRFPropertyMapDefinition(GSF_BRIDGES, A0RPI_BRIDGE_AVAILABILITY_FLAGS, "bridge_availability_flags"),
	GRFPropertyMapDefinition(GSF_RAILTYPES, A0RPI_RAILTYPE_ENABLE_PROGRAMMABLE_SIGNALS, "railtype_enable_programmable_signals"),
	GRFPropertyMapDefinition(GSF_RAILTYPES, A0RPI_RAILTYPE_ENABLE_NO_ENTRY_SIGNALS, "railtype_enable_no_entry_signals"),
	GRFPropertyMapDefinition(GSF_RAILTYPES, A0RPI_RAILTYPE_ENABLE_RESTRICTED_SIGNALS, "railtype_enable_restricted_signals"),
	GRFPropertyMapDefinition(GSF_RAILTYPES, A0RPI_RAILTYPE_DISABLE_REALISTIC_BRAKING, "railtype_disable_realistic_braking"),
	GRFPropertyMapDefinition(GSF_RAILTYPES, A0RPI_RAILTYPE_ENABLE_SIGNAL_RECOLOUR, "railtype_enable_signal_recolour"),
	GRFPropertyMapDefinition(GSF_RAILTYPES, A0RPI_RAILTYPE_EXTRA_ASPECTS, "railtype_extra_aspects"),
	GRFPropertyMapDefinition(GSF_ROADTYPES, A0RPI_ROADTYPE_EXTRA_FLAGS, "roadtype_extra_flags"),
	GRFPropertyMapDefinition(GSF_TRAMTYPES, A0RPI_ROADTYPE_EXTRA_FLAGS, "roadtype_extra_flags"),
	GRFPropertyMapDefinition(GSF_GLOBALVAR, A0RPI_GLOBALVAR_EXTRA_STATION_NAMES, "global_extra_station_names"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_ENABLE_PROGRAMMABLE_SIGNALS, "signals_enable_programmable_signals"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_ENABLE_NO_ENTRY_SIGNALS, "signals_enable_no_entry_signals"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_ENABLE_RESTRICTED_SIGNALS, "signals_enable_restricted_signals"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_ENABLE_SIGNAL_RECOLOUR, "signals_enable_signal_recolour"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_EXTRA_ASPECTS, "signals_extra_aspects"),
	GRFPropertyMapDefinition(GSF_OBJECTS, A0RPI_OBJECT_USE_LAND_GROUND, "object_use_land_ground"),
	GRFPropertyMapDefinition(GSF_OBJECTS, A0RPI_OBJECT_EDGE_FOUNDATION_MODE, "object_edge_foundation_mode"),
	GRFPropertyMapDefinition(GSF_OBJECTS, A0RPI_OBJECT_FLOOD_RESISTANT, "object_flood_resistant"),
	GRFPropertyMapDefinition(),
};

/** Action14 Action5 remappable type list */
extern const Action5TypeRemapDefinition _grf_action5_remappable_types[] = {
	Action5TypeRemapDefinition("programmable_signals", A5BLOCK_ALLOW_OFFSET, SPR_PROGSIGNAL_BASE, 1, 32, "Programmable pre-signal graphics"),
	Action5TypeRemapDefinition("no_entry_signals", A5BLOCK_ALLOW_OFFSET, SPR_EXTRASIGNAL_BASE, 1, 16, "No-entry signal graphics"),
	Action5TypeRemapDefinition(),
};
