/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_settings_type.h Settings types related to commands. */

#ifndef COMMAND_SETTINGS_TYPE_H
#define COMMAND_SETTINGS_TYPE_H

enum class CommandPauseLevel : uint8_t {
	NoActions, ///< No user actions may be executed.
	NoConstruction, ///< No construction actions may be executed.
	NoLandscaping, ///< No landscaping actions may be executed.
	AllActions, ///< All actions may be executed.
};

#endif /* COMMAND_SETTINGS_TYPE_H */
