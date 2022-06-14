/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail_gui_type.h Types etc. related to the rail GUI. */

#ifndef RAIL_GUI_TYPE_H
#define RAIL_GUI_TYPE_H

/** Settings for which signals are shown by the signal GUI. */
enum SignalGUISettings : uint8 {
	SIGNAL_GUI_PATH = 0, ///< Show path signals only.
	SIGNAL_GUI_ALL = 1,  ///< Show all signals, including block and presignals.
};

/** Settings for which signals are cycled through by control-clicking on the signal with the signal tool. */
enum SignalCycleSettings : uint8 {
	SIGNAL_CYCLE_PATH = 0, ///< Cycle through path signals only.
	SIGNAL_CYCLE_ALL = 1,  ///< Cycle through all signals visible.
};

#endif /* RAIL_GUI_TYPE_H */
