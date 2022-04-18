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
	GRFFeatureInfo("variable_mapping", 2),
	GRFFeatureInfo("feature_id_mapping", 1),
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
	GRFFeatureInfo("action5_misc_gui", 1),
	GRFFeatureInfo("action5_road_waypoints", 1),
	GRFFeatureInfo("action0_railtype_programmable_signals", 1),
	GRFFeatureInfo("action0_railtype_no_entry_signals", 1),
	GRFFeatureInfo("action0_railtype_restricted_signals", 1),
	GRFFeatureInfo("action0_railtype_disable_realistic_braking", 1),
	GRFFeatureInfo("action0_railtype_recolour", 1),
	GRFFeatureInfo("action0_railtype_extra_aspects", 1),
	GRFFeatureInfo("action0_roadtype_extra_flags", 1),
	GRFFeatureInfo("action0_global_extra_station_names", 2),
	GRFFeatureInfo("action0_global_default_object_generate_amount", 1),
	GRFFeatureInfo("action0_signals_programmable_signals", 1),
	GRFFeatureInfo("action0_signals_no_entry_signals", 1),
	GRFFeatureInfo("action0_signals_restricted_signals", 1),
	GRFFeatureInfo("action0_signals_recolour", 1),
	GRFFeatureInfo("action0_signals_extra_aspects", 1),
	GRFFeatureInfo("action3_signals_custom_signal_sprites", 1),
	GRFFeatureInfo("action0_object_use_land_ground", 1),
	GRFFeatureInfo("action0_object_edge_foundation_mode", 2),
	GRFFeatureInfo("action0_object_flood_resistant", 1),
	GRFFeatureInfo("action0_object_viewport_map_tile_type", 1),
	GRFFeatureInfo("road_stops", 2),
	GRFFeatureInfo(),
};

/** Action14 remappable feature list */
extern const GRFFeatureMapDefinition _grf_remappable_features[] = {
	GRFFeatureMapDefinition(GSF_ROADSTOPS, "road_stops"),
	GRFFeatureMapDefinition(),
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
	GRFPropertyMapDefinition(GSF_GLOBALVAR, A0RPI_GLOBALVAR_EXTRA_STATION_NAMES_PROBABILITY, "global_extra_station_names_probability"),
	GRFPropertyMapDefinition(GSF_GLOBALVAR, A0RPI_GLOBALVAR_LIGHTHOUSE_GENERATE_AMOUNT, "global_lighthouse_generate_amount"),
	GRFPropertyMapDefinition(GSF_GLOBALVAR, A0RPI_GLOBALVAR_TRANSMITTER_GENERATE_AMOUNT, "global_transmitter_generate_amount"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_ENABLE_PROGRAMMABLE_SIGNALS, "signals_enable_programmable_signals"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_ENABLE_NO_ENTRY_SIGNALS, "signals_enable_no_entry_signals"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_ENABLE_RESTRICTED_SIGNALS, "signals_enable_restricted_signals"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_ENABLE_SIGNAL_RECOLOUR, "signals_enable_signal_recolour"),
	GRFPropertyMapDefinition(GSF_SIGNALS, A0RPI_SIGNALS_EXTRA_ASPECTS, "signals_extra_aspects"),
	GRFPropertyMapDefinition(GSF_OBJECTS, A0RPI_OBJECT_USE_LAND_GROUND, "object_use_land_ground"),
	GRFPropertyMapDefinition(GSF_OBJECTS, A0RPI_OBJECT_EDGE_FOUNDATION_MODE, "object_edge_foundation_mode"),
	GRFPropertyMapDefinition(GSF_OBJECTS, A0RPI_OBJECT_FLOOD_RESISTANT, "object_flood_resistant"),
	GRFPropertyMapDefinition(GSF_OBJECTS, A0RPI_OBJECT_VIEWPORT_MAP_TYPE, "object_viewport_map_tile_type"),
	GRFPropertyMapDefinition(GSF_OBJECTS, A0RPI_OBJECT_VIEWPORT_MAP_SUBTYPE, "object_viewport_map_tile_subtype"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_CLASS_ID, "roadstop_class_id"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_STOP_TYPE, "roadstop_stop_type"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_STOP_NAME, "roadstop_stop_name"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_CLASS_NAME, "roadstop_class_name"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_DRAW_MODE, "roadstop_draw_mode"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_TRIGGER_CARGOES, "roadstop_random_trigger_cargoes"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_ANIMATION_INFO, "roadstop_animation_info"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_ANIMATION_SPEED, "roadstop_animation_speed"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_ANIMATION_TRIGGERS, "roadstop_animation_triggers"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_CALLBACK_MASK, "roadstop_callback_mask"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_GENERAL_FLAGS, "roadstop_general_flags"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_MIN_BRIDGE_HEIGHT, "roadstop_min_bridge_height"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_DISALLOWED_BRIDGE_PILLARS, "roadstop_disallowed_bridge_pillars"),
	GRFPropertyMapDefinition(GSF_ROADSTOPS, A0RPI_ROADSTOP_COST_MULTIPLIERS, "roadstop_cost_multipliers"),
	GRFPropertyMapDefinition(),
};

/** Action14 Action2 remappable variable list */
extern const GRFVariableMapDefinition _grf_action2_remappable_variables[] = {
	GRFVariableMapDefinition(GSF_OBJECTS, A2VRI_OBJECT_FOUNDATION_SLOPE, "object_foundation_tile_slope"),
	GRFVariableMapDefinition(GSF_OBJECTS, A2VRI_OBJECT_FOUNDATION_SLOPE_CHANGE, "object_foundation_change_tile_slope"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x40, "roadstop_view"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x41, "roadstop_type"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x42, "roadstop_terrain_type"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x43, "roadstop_road_type"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x44, "roadstop_tram_type"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x45, "roadstop_town_zone"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x46, "roadstop_town_distance_squared"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x47, "roadstop_company_info"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x49, "roadstop_animation_frame"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x50, "roadstop_misc_info"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x66, "roadstop_animation_frame_nearby_tiles"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x67, "roadstop_land_info_nearby_tiles"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x68, "roadstop_road_stop_info_nearby_tiles"),
	GRFVariableMapDefinition(GSF_ROADSTOPS, 0x6A, "roadstop_road_stop_grfid_nearby_tiles"),
	GRFVariableMapDefinition(),
};

/** Action14 Action5 remappable type list */
extern const Action5TypeRemapDefinition _grf_action5_remappable_types[] = {
	Action5TypeRemapDefinition("programmable_signals", A5BLOCK_ALLOW_OFFSET, SPR_PROGSIGNAL_BASE, 1, 32, "Programmable pre-signal graphics"),
	Action5TypeRemapDefinition("no_entry_signals", A5BLOCK_ALLOW_OFFSET, SPR_EXTRASIGNAL_BASE, 1, 16, "No-entry signal graphics"),
	Action5TypeRemapDefinition("misc_gui", A5BLOCK_ALLOW_OFFSET, SPR_MISC_GUI_BASE, 1, 1, "Miscellaneous GUI graphics"),
	Action5TypeRemapDefinition("road_waypoints", A5BLOCK_ALLOW_OFFSET, SPR_ROAD_WAYPOINTS_BASE, 1, 4, "Road waypoints"),
	Action5TypeRemapDefinition(),
};
