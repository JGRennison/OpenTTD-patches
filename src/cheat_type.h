/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cheat_type.h Types related to cheating. */

#ifndef CHEAT_TYPE_H
#define CHEAT_TYPE_H

/**
 * Info about each of the cheats.
 */
struct Cheat {
	bool been_used; ///< has this cheat been used before?
	bool value;     ///< tells if the bool cheat is active or not
};

struct Cheats {
	Cheat magic_bulldozer;  ///< dynamite industries, objects
	Cheat switch_company;   ///< change to another company
	Cheat money;            ///< get rich or poor
	Cheat crossing_tunnels; ///< allow tunnels that cross each other
	Cheat no_jetcrash;      ///< no jet will crash on small airports anymore
	Cheat change_date;      ///< changes date ingame
	Cheat setup_prod;       ///< setup raw-material production in game
	Cheat edit_max_hl;      ///< edit the maximum heightlevel; this is a cheat because of the fact that it needs to reset NewGRF game state and doing so as a simple configuration breaks the expectation of many
	Cheat station_rating;   ///< Fix station ratings at 100%
	/* non-trunk cheats follow */
	Cheat inflation_cost;   ///< inflation cost factor
	Cheat inflation_income; ///< inflation income factor
	Cheat town_rating;      ///< 100% town local authority rating
};

/** Available cheats. */
enum CheatNumbers {
	CHT_MONEY,           ///< Change amount of money.
	CHT_CHANGE_COMPANY,  ///< Switch company.
	CHT_EXTRA_DYNAMITE,  ///< Dynamite anything.
	CHT_CROSSINGTUNNELS, ///< Allow tunnels to cross each other.
	CHT_NO_JETCRASH,     ///< Disable jet-airplane crashes.
	CHT_SETUP_PROD,      ///< Allow manually editing of industry production.
	CHT_EDIT_MAX_HL,     ///< Edit maximum allowed heightlevel
	CHT_CHANGE_DATE,     ///< Do time traveling.
	CHT_INFLATION_COST,  ///< Change inflation cost factor
	CHT_INFLATION_INCOME,///< Change inflation income factor
	CHT_STATION_RATING,  ///< 100% station ratings
	CHT_TOWN_RATING,     ///< 100% town local authority ratings

	CHT_NUM_CHEATS,      ///< Number of cheats.
};

extern Cheats _cheats;

#endif /* CHEAT_TYPE_H */
