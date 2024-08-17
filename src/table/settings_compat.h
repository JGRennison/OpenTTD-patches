/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file settings_compat.h Tables for loading non-table format settings chunks. */

#ifndef SETTINGS_COMPAT_H
#define SETTINGS_COMPAT_H

#define SLCX_VAR(name) {name, SettingsCompatType::Setting, 0, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(), nullptr}
#define SLCX_NULL_X(length, from, to, extver) {{}, SettingsCompatType::Null, length, from, to, extver, nullptr}
#define SLCX_NULL(length, from, to) SLCX_NULL_X(length, from, to, SlXvFeatureTest())
#define SLCX_XREF(name, from, to, extver) {name, SettingsCompatType::Xref, 0, from, to, extver, nullptr}
#define SLCX_XREFCVT(name, from, to, extver, cvt) {name, SettingsCompatType::Xref, 0, from, to, extver, cvt}

static std::initializer_list<SettingsCompat> _gameopt_compat{
	SLCX_VAR("diff_custom"),
	SLCX_VAR("diff_level"),
	SLCX_VAR("locale.currency"),
	SLCX_VAR("units"),
	SLCX_VAR("game_creation.town_name"),
	SLCX_VAR("game_creation.landscape"),
	SLCX_VAR("game_creation.snow_line_height"),
	SLCX_NULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)), // game_creation.desert_amount
	SLCX_NULL(1, SLV_22, SLV_165),
	SLCX_NULL(1, SL_MIN_VERSION, SLV_23),
	SLCX_VAR("vehicle.road_side"),
};

static std::initializer_list<SettingsCompat> _settings_compat{
	SLCX_VAR("difficulty.max_no_competitors"),
	SLCX_NULL(1, SLV_97, SLV_110), // difficulty.competitor_start_time
	SLCX_VAR("difficulty.number_towns"),
	SLCX_VAR("difficulty.industry_density"),
	SLCX_VAR("difficulty.max_loan"),
	SLCX_VAR("difficulty.initial_interest"),
	SLCX_VAR("difficulty.vehicle_costs"),
	SLCX_VAR("difficulty.competitor_speed"),
	SLCX_NULL(1, SLV_97, SLV_110), // difficulty.competitor_intelligence
	SLCX_VAR("difficulty.vehicle_breakdowns"),
	SLCX_VAR("difficulty.subsidy_multiplier"),
	SLCX_VAR("difficulty.subsidy_duration"),
	SLCX_VAR("difficulty.construction_cost"),
	SLCX_VAR("difficulty.terrain_type"),
	SLCX_VAR("difficulty.quantity_sea_lakes"),
	SLCX_VAR("difficulty.economy"),
	SLCX_VAR("difficulty.line_reverse_mode"),
	SLCX_VAR("difficulty.disasters"),
	SLCX_VAR("difficulty.town_council_tolerance"),
	SLCX_VAR("diff_level"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("order.old_timetable_separation", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_VAR("game_creation.town_name"),
	SLCX_VAR("game_creation.landscape"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)), // snow line upper byte
	SLCX_NULL(1, SLV_97, SLV_164), // snow line
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // game_creation.desert_amount
	SLCX_NULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)), // game_creation.tree_line
	SLCX_VAR("vehicle.road_side"),
	SLCX_VAR("construction.map_height_limit"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)), // construction.allow_more_heightlevels
	SLCX_VAR("game_creation.heightmap_height"),
	SLCX_VAR("construction.build_on_slopes"),
	SLCX_VAR("construction.command_pause_level"),
	SLCX_VAR("construction.terraform_per_64k_frames"),
	SLCX_VAR("construction.terraform_frame_burst"),
	SLCX_VAR("construction.clear_per_64k_frames"),
	SLCX_VAR("construction.clear_frame_burst"),
	SLCX_VAR("construction.tree_per_64k_frames"),
	SLCX_VAR("construction.tree_frame_burst"),
	SLCX_VAR("construction.autoslope"),
	SLCX_VAR("construction.extra_dynamite"),
	SLCX_VAR("construction.max_bridge_length"),
	SLCX_XREF("construction.old_simulated_wormhole_signals", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 2)),
	SLCX_XREF("construction.old_simulated_wormhole_signals", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_VAR("construction.max_bridge_height"),
	SLCX_VAR("construction.max_tunnel_length"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_233)), // construction.max_chunnel_exit_length
	SLCX_XREF("construction.maximum_signal_evaluations", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("construction.chunnel", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_NULL(1, SL_MIN_VERSION, SLV_159), // construction.longbridges
	SLCX_VAR("construction.train_signal_side"),
	SLCX_VAR("station.never_expire_airports"),
	SLCX_VAR("economy.town_layout"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.town_construction_cost
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.station_rating_type
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 7)), // economy.scale_industry_production
	SLCX_VAR("economy.allow_town_roads"),
	SLCX_XREF("economy.old_town_cargo_factor", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.day_length_factor", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
	SLCX_VAR("economy.found_town"),
	SLCX_VAR("economy.allow_town_level_crossings"),
	SLCX_XREF("economy.old_town_cargo_factor", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
	SLCX_VAR("economy.town_cargogen_mode"),
	SLCX_XREF("economy.max_town_heightlevel", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_VAR("linkgraph.recalc_interval"),
	SLCX_VAR("linkgraph.recalc_time"),
	SLCX_VAR("linkgraph.distribution_pax"),
	SLCX_VAR("linkgraph.distribution_mail"),
	SLCX_VAR("linkgraph.distribution_armoured"),
	SLCX_VAR("linkgraph.distribution_default"),
	SLCX_VAR("linkgraph.distribution_per_cargo[0]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[1]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[2]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[3]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[4]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[5]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[6]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[7]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[8]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[9]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[10]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[11]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[12]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[13]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[14]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[15]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[16]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[17]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[18]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[19]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[20]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[21]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[22]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[23]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[24]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[25]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[26]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[27]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[28]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[29]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[30]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[31]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[32]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[33]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[34]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[35]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[36]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[37]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[38]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[39]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[40]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[41]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[42]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[43]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[44]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[45]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[46]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[47]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[48]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[49]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[50]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[51]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[52]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[53]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[54]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[55]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[56]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[57]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[58]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[59]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[60]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[61]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[62]"),
	SLCX_VAR("linkgraph.distribution_per_cargo[63]"),
	SLCX_VAR("linkgraph.accuracy"),
	SLCX_VAR("linkgraph.demand_distance"),
	SLCX_VAR("linkgraph.demand_size"),
	SLCX_VAR("linkgraph.short_path_saturation"),
	SLCX_VAR("linkgraph.aircraft_link_scale"),
	SLCX_XREF("economy.old_town_cargo_factor", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_VAR("vehicle.train_acceleration_model"),
	SLCX_VAR("vehicle.roadveh_acceleration_model"),
	SLCX_VAR("vehicle.train_slope_steepness"),
	SLCX_VAR("vehicle.roadveh_slope_steepness"),
	SLCX_VAR("pf.forbid_90_deg"),
	SLCX_XREF("pf.back_of_one_way_pbs_waiting_point", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_XREF("pf.back_of_one_way_pbs_waiting_point", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_VAR("vehicle.max_train_length"),
	SLCX_NULL(1, SL_MIN_VERSION, SLV_159), // vehicle.mammoth_trains
	SLCX_VAR("vehicle.smoke_amount"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SLV_159, SlXvFeatureTest(XSLFTO_OR, XSLFI_CHILLPP, SL_CHILLPP_232)), // order.gotodepot
	SLCX_VAR("pf.roadveh_queue"),
	SLCX_NULL(1, SL_MIN_VERSION, SLV_87), // pf.new_pathfinding_all
	SLCX_NULL(3, SLV_28, SLV_87), // pf.yapf.*_use_yapf
	SLCX_NULL(3, SLV_87, SLV_TABLE_CHUNKS), // pf.pathfinder_for_*
	SLCX_VAR("vehicle.never_expire_vehicles"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // vehicle.exact_intro_date
	SLCX_VAR("vehicle.max_trains"),
	SLCX_VAR("vehicle.max_roadveh"),
	SLCX_VAR("vehicle.max_aircraft"),
	SLCX_VAR("vehicle.max_ships"),
	SLCX_VAR("vehicle.servint_ispercent"),
	SLCX_VAR("vehicle.servint_trains"),
	SLCX_VAR("vehicle.servint_roadveh"),
	SLCX_VAR("vehicle.servint_ships"),
	SLCX_VAR("vehicle.servint_aircraft"),
	SLCX_VAR("order.no_servicing_if_no_breakdowns"),
	SLCX_VAR("vehicle.wagon_speed_limits"),
	SLCX_XREF("vehicle.slow_road_vehicles_in_curves", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_25)),
	SLCX_XREF("vehicle.train_speed_adaptation", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_VAR("vehicle.disable_elrails"),
	SLCX_VAR("vehicle.freight_trains"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // vehicle.freight_mult_to_passengers
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // ticks_per_minute
	SLCX_NULL_X(1, SLV_67, SLV_159, SlXvFeatureTest(XSLFTO_OR, XSLFI_CHILLPP, SL_CHILLPP_232)), // order.timetabling
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)), // order.timetable_automated
	SLCX_XREF("order.old_timetable_separation", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
	SLCX_VAR("vehicle.plane_speed"),
	SLCX_VAR("vehicle.dynamic_engines"),
	SLCX_VAR("vehicle.plane_crashes"),
	SLCX_XREF("vehicle.improved_breakdowns", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("vehicle.improved_breakdowns", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_NULL(1, SL_MIN_VERSION, SLV_159), // station.join_stations
	SLCX_VAR("gui.sg_full_load_any"),
	SLCX_VAR("order.improved_load"),
	SLCX_VAR("order.selectgoods"),
	SLCX_NULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.deliver_goods, vehicle.cargo_wait_time
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)), // order.automatic_timetable_separation
	SLCX_NULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_24)), // order.timetable_auto_{travel_buffer, load_buffer, travel_rounding, load_rounding}
	SLCX_VAR("gui.sg_new_nonstop"),
	SLCX_NULL(1, SL_MIN_VERSION, SLV_159), // station.nonuniform_stations
	SLCX_VAR("station.station_spread"),
	SLCX_VAR("order.serviceathelipad"),
	SLCX_VAR("station.modified_catchment"),
	SLCX_VAR("station.serve_neutral_industries"),
	SLCX_VAR("order.gradual_loading"),
	SLCX_VAR("construction.road_stop_on_town_road"),
	SLCX_VAR("construction.road_stop_on_competitor_road"),
	SLCX_XREF("construction.road_custom_bridge_heads", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_VAR("station.adjacent_stations"),
	SLCX_VAR("economy.station_noise_level"),
	SLCX_VAR("station.distant_join_stations"),
	SLCX_NULL_X(6, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // construction.{traffic_lights, towns_build_traffic_lights, allow_building_tls_in_towns, traffic_lights_green_phase, max_tlc_size, max_tlc_distance}
	SLCX_VAR("economy.inflation"),
	SLCX_VAR("construction.raw_industry_construction"),
	SLCX_VAR("construction.industry_platform"),
	SLCX_VAR("economy.multiple_industry_per_town"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 4)), // economy.allow_automatic_industries
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // construction.extra_industry_placement_logic
	SLCX_NULL(1, SL_MIN_VERSION, SLV_141),
	SLCX_NULL_X(6, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.minimum_distance_{town, industry, ind_town}
	SLCX_VAR("economy.bribe"),
	SLCX_VAR("economy.exclusive_rights"),
	SLCX_VAR("economy.fund_buildings"),
	SLCX_VAR("economy.fund_roads"),
	SLCX_VAR("economy.give_money"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)), // game_creation.tree_line_height
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)), // snow line upper byte
	SLCX_VAR("game_creation.snow_line_height"),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // game_creation.desert_amount
	SLCX_NULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)), // game_creation.tree_line
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)), // game_creation.desert_amount
	SLCX_VAR("game_creation.snow_coverage"),
	SLCX_VAR("game_creation.desert_coverage"),
	SLCX_NULL_X(4, SL_MIN_VERSION, SLV_144, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, 0, 0)),
	SLCX_VAR("game_creation.starting_year"),
	SLCX_NULL(4, SL_MIN_VERSION, SLV_105),
	SLCX_VAR("game_creation.ending_year"),
	SLCX_VAR("economy.type"),
	SLCX_VAR("economy.allow_shares"),
	SLCX_VAR("economy.min_years_for_shares"),
	SLCX_VAR("economy.feeder_payment_share"),
	SLCX_XREF("economy.day_length_factor", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_NULL_X(71, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.price_mult[0-70]
	SLCX_NULL_X(16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.price_rails[0-15]
	SLCX_NULL_X(16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.rail_maintenance[0-15]
	SLCX_XREF("vehicle.pay_for_repair", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // note that this has changed format in SpringPP 2.1.147
	SLCX_XREF("vehicle.repair_cost", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_NULL_X(7, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.town_consumption_rate, economy.town_pop_*
	SLCX_NULL_X(18, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.town_consumption_rates[0-2][0-2]
	SLCX_NULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // economy.town_effects[0-2], economy.grow_if_one_delivered
	SLCX_VAR("economy.town_growth_rate"),
	SLCX_VAR("economy.larger_towns"),
	SLCX_VAR("economy.initial_city_size"),
	SLCX_NULL_X(10, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // economy.{town_growth_cargo, town_pop_need_goods, larger_town_growth_cargo, larger_town_pop_need_goods}
	SLCX_VAR("economy.mod_road_rebuild"),
	SLCX_XREF("construction.maximum_signal_evaluations", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
	SLCX_XREF("economy.town_min_distance", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_XREF("economy.infrastructure_sharing[0]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.infrastructure_sharing[1]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.infrastructure_sharing[2]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.infrastructure_sharing[3]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.sharing_fee[0]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.sharing_fee[1]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.sharing_fee[2]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.sharing_fee[3]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.sharing_payment_in_debt", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	SLCX_XREF("economy.infrastructure_sharing[0]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_XREF("economy.infrastructure_sharing[1]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_XREF("economy.infrastructure_sharing[2]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_XREF("economy.infrastructure_sharing[3]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_XREF("economy.sharing_fee[0]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_XREF("economy.sharing_fee[1]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_XREF("economy.sharing_fee[2]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_XREF("economy.sharing_fee[3]", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_XREF("economy.sharing_payment_in_debt", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_XREF("economy.day_length_factor", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_NULL(1, SL_MIN_VERSION, SLV_107), // previously ai-new setting
	SLCX_NULL(1, SLV_178, SLV_TABLE_CHUNKS), // previously script.settings_profile
	SLCX_VAR("ai.ai_in_multiplayer"),
	SLCX_VAR("ai.ai_disable_veh_train"),
	SLCX_VAR("ai.ai_disable_veh_roadveh"),
	SLCX_VAR("ai.ai_disable_veh_aircraft"),
	SLCX_VAR("ai.ai_disable_veh_ship"),
	SLCX_VAR("script.script_max_opcode_till_suspend"),
	SLCX_VAR("script.script_max_memory_megabytes"),
	SLCX_VAR("vehicle.extend_vehicle_life"),
	SLCX_VAR("economy.dist_local_authority"),
	SLCX_VAR("pf.reverse_at_signals"),
	SLCX_VAR("pf.wait_oneway_signal"),
	SLCX_VAR("pf.wait_twoway_signal"),
	SLCX_VAR("economy.town_noise_population[0]"),
	SLCX_VAR("economy.town_noise_population[1]"),
	SLCX_VAR("economy.town_noise_population[2]"),
	SLCX_VAR("economy.infrastructure_maintenance"),
	SLCX_XREF("economy.infrastructure_maintenance", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLCX_NULL_X(6, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // construction.traffic_lights...
	SLCX_XREF("linkgraph.recalc_interval", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
	SLCX_XREFCVT("linkgraph.distribution_pax", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP), LinkGraphDistModeXrefChillPP),
	SLCX_XREFCVT("linkgraph.distribution_mail", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP), LinkGraphDistModeXrefChillPP),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)), // linkgraph.distribution_express
	SLCX_XREFCVT("linkgraph.distribution_armoured", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP), LinkGraphDistModeXrefChillPP),
	SLCX_XREFCVT("linkgraph.distribution_default", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP), LinkGraphDistModeXrefChillPP),
	SLCX_XREF("linkgraph.accuracy", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
	SLCX_XREF("linkgraph.demand_size", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
	SLCX_XREF("linkgraph.demand_distance", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
	SLCX_XREF("linkgraph.short_path_saturation", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
	SLCX_NULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // linkgraph.no_overload_links
	SLCX_VAR("pf.wait_for_pbs_path"),
	SLCX_VAR("pf.reserve_paths"),
	SLCX_VAR("pf.path_backoff_interval"),
	SLCX_NULL(3, SL_MIN_VERSION, SLV_REMOVE_OPF), // pf.opf.pf_maxlength & pf.opf.pf_maxdepth
	SLCX_NULL(32, SL_MIN_VERSION, SLV_TABLE_CHUNKS), // pf.npf.npf_max_search_nodes, 7 pf.npf.npf_rail_*
	SLCX_NULL(8, SLV_100, SLV_TABLE_CHUNKS), // pf.npf.npf_rail_pbs_cross_penalty, pf.npf.npf_rail_pbs_signal_back_penalty
	SLCX_NULL(16, SL_MIN_VERSION, SLV_TABLE_CHUNKS), // pf.npf.npf_buoy_penalty, pf.npf.npf_water_curve_penalty, pf.npf.npf_road_curve_penalty, pf.npf.npf_crossing_penalty
	SLCX_NULL(4, SLV_47, SLV_TABLE_CHUNKS), // pf.npf.npf_road_drive_through_penalty
	SLCX_NULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // pf.npf.npf_road_trafficlight_penalty
	SLCX_NULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // pf.npf.npf_road_trafficlight_penalty
	SLCX_NULL(8, SLV_130, SLV_TABLE_CHUNKS), // pf.npf.npf_road_dt_occupied_penalty, pf.npf.npf_road_bay_occupied_penalty
	SLCX_NULL(4, SLV_131, SLV_TABLE_CHUNKS), // pf.npf.maximum_go_to_depot_penalty
	SLCX_VAR("pf.yapf.disable_node_optimization"),
	SLCX_VAR("pf.yapf.max_search_nodes"),
	SLCX_VAR("pf.yapf.rail_firstred_twoway_eol"),
	SLCX_VAR("pf.yapf.rail_firstred_penalty"),
	SLCX_VAR("pf.yapf.rail_firstred_exit_penalty"),
	SLCX_VAR("pf.yapf.rail_lastred_penalty"),
	SLCX_VAR("pf.yapf.rail_lastred_exit_penalty"),
	SLCX_VAR("pf.yapf.rail_station_penalty"),
	SLCX_VAR("pf.yapf.rail_slope_penalty"),
	SLCX_VAR("pf.yapf.rail_curve45_penalty"),
	SLCX_VAR("pf.yapf.rail_curve90_penalty"),
	SLCX_VAR("pf.yapf.rail_depot_reverse_penalty"),
	SLCX_VAR("pf.yapf.rail_crossing_penalty"),
	SLCX_VAR("pf.yapf.rail_look_ahead_max_signals"),
	SLCX_VAR("pf.yapf.rail_look_ahead_signal_p0"),
	SLCX_VAR("pf.yapf.rail_look_ahead_signal_p1"),
	SLCX_VAR("pf.yapf.rail_look_ahead_signal_p2"),
	SLCX_VAR("pf.yapf.rail_pbs_cross_penalty"),
	SLCX_VAR("pf.yapf.rail_pbs_station_penalty"),
	SLCX_VAR("pf.yapf.rail_pbs_signal_back_penalty"),
	SLCX_VAR("pf.yapf.rail_doubleslip_penalty"),
	SLCX_VAR("pf.yapf.rail_longer_platform_penalty"),
	SLCX_VAR("pf.yapf.rail_longer_platform_per_tile_penalty"),
	SLCX_VAR("pf.yapf.rail_shorter_platform_penalty"),
	SLCX_VAR("pf.yapf.rail_shorter_platform_per_tile_penalty"),
	SLCX_VAR("pf.yapf.road_slope_penalty"),
	SLCX_VAR("pf.yapf.road_curve_penalty"),
	SLCX_VAR("pf.yapf.road_crossing_penalty"),
	SLCX_NULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)), // pf.yapf.road_trafficlight_penalty
	SLCX_NULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // pf.yapf.road_trafficlight_penalty
	SLCX_VAR("pf.yapf.road_stop_penalty"),
	SLCX_VAR("pf.yapf.road_stop_occupied_penalty"),
	SLCX_VAR("pf.yapf.road_stop_bay_occupied_penalty"),
	SLCX_VAR("pf.yapf.maximum_go_to_depot_penalty"),
	SLCX_VAR("pf.yapf.ship_curve45_penalty"),
	SLCX_VAR("pf.yapf.ship_curve90_penalty"),
	SLCX_VAR("game_creation.land_generator"),
	SLCX_VAR("game_creation.oil_refinery_limit"),
	SLCX_VAR("game_creation.tgen_smoothness"),
	SLCX_VAR("game_creation.variety"),
	SLCX_VAR("game_creation.generation_seed"),
	SLCX_VAR("game_creation.tree_placer"),
	SLCX_VAR("construction.freeform_edges"),
	SLCX_VAR("game_creation.water_borders"),
	SLCX_VAR("game_creation.custom_town_number"),
	SLCX_VAR("construction.extra_tree_placement"),
	SLCX_NULL_X(3, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)), // construction.{tree_placement_drag_limit, ingame_tree_line_height, tree_growth_rate}
	SLCX_XREF("construction.tree_growth_rate", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_XREF("construction.trees_around_snow_line_range", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_VAR("game_creation.custom_terrain_type"),
	SLCX_VAR("game_creation.custom_sea_level"),
	SLCX_VAR("game_creation.min_river_length"),
	SLCX_VAR("game_creation.river_route_random"),
	SLCX_VAR("game_creation.amount_of_rivers"),
	SLCX_XREF("game_creation.build_public_roads", SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLCX_VAR("locale.currency"),
	SLCX_VAR("units"),
	SLCX_VAR("locale.units_velocity"),
	SLCX_VAR("locale.units_power"),
	SLCX_VAR("locale.units_weight"),
	SLCX_VAR("locale.units_volume"),
	SLCX_VAR("locale.units_force"),
	SLCX_VAR("locale.units_height"),
	SLCX_VAR("locale.digit_group_separator"),
	SLCX_VAR("locale.digit_group_separator_currency"),
	SLCX_VAR("locale.digit_decimal_separator"),
	SLCX_NULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 7)), // gui.time_in_minutes, gui.ticks_per_minute
};

#undef SLCX_VAR
#undef SLCX_NULL_X
#undef SLCX_NULL
#undef SLCX_NULL_X
#undef SLCX_XREF
#undef SLCX_XREFCVT

#endif /* SETTINGS_COMPAT_H */
