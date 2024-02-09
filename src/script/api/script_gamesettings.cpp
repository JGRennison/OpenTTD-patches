/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_gamesettings.cpp Implementation of ScriptGameSettings. */

#include "../../stdafx.h"
#include "script_gamesettings.hpp"
#include "../../settings_internal.h"
#include "../../settings_type.h"
#include "../../command_type.h"
#include "../../command_func.h"
#include "../../economy_func.h"
#include "../../date_func.h"

#include <optional>

#include "../../safeguards.h"

struct CargoScalingProxy {
	bool is_industry;

	CargoScalingProxy(bool is_industry) : is_industry(is_industry) {}

	SQInteger ReadValue() const
	{
		uint64_t scale = this->is_industry ? _settings_game.economy.industry_cargo_scale : _settings_game.economy.town_cargo_scale;
		CargoScalingMode mode = this->is_industry ? _settings_game.economy.industry_cargo_scale_mode : _settings_game.economy.town_cargo_scale_mode;
		if (mode == CSM_DAYLENGTH) {
			scale *= DayLengthFactor();
		}
		return PercentageToScaleQuantityFactor(scale);
	}

	bool SetValue(SQInteger value) const
	{
		CargoScalingMode mode = this->is_industry ? _settings_game.economy.industry_cargo_scale_mode : _settings_game.economy.town_cargo_scale_mode;
		if (mode == CSM_DAYLENGTH) {
			/* Asynchronous free command, don't bother halting the script or saving the result */
			::DoCommandPScript(0, 0, (uint32_t)CSM_MONTHLY, 0, CMD_CHANGE_SETTING, nullptr,
					this->is_industry ? "economy.industry_cargo_scale_mode" : "economy.town_cargo_scale_mode", false, false, true, nullptr);
		}

		return ScriptGameSettings::SetValue(this->is_industry ? "economy.industry_cargo_scale" : "economy.town_cargo_scale", ScaleQuantity(100, (int)value));
	}

	static std::optional<CargoScalingProxy> Get(const std::string &setting)
	{
		std::string_view prefix = "economy.";
		std::string_view str = setting;
		if (str.starts_with(prefix)) str.remove_prefix(prefix.size());
		if (str == "town_cargo_scale_factor") return CargoScalingProxy(false);
		if (str == "industry_cargo_scale_factor") return CargoScalingProxy(true);
		return std::nullopt;
	}
};

/* static */ bool ScriptGameSettings::IsValid(const std::string &setting)
{
	std::optional<CargoScalingProxy> csproxy = CargoScalingProxy::Get(setting);
	if (csproxy.has_value()) return true;

	const SettingDesc *sd = GetSettingFromName(setting);
	return sd != nullptr && sd->IsIntSetting();
}

/* static */ SQInteger ScriptGameSettings::GetValue(const std::string &setting)
{
	std::optional<CargoScalingProxy> csproxy = CargoScalingProxy::Get(setting);
	if (csproxy.has_value()) {
		return csproxy->ReadValue();
	}

	const SettingDesc *sd = GetSettingFromName(setting);
	if (sd == nullptr || !sd->IsIntSetting()) return -1;
	return sd->AsIntSetting()->Read(&_settings_game);
}

/* static */ bool ScriptGameSettings::SetValue(const std::string &setting, SQInteger value)
{
	EnforceDeityOrCompanyModeValid(false);

	std::optional<CargoScalingProxy> csproxy = CargoScalingProxy::Get(setting);
	if (csproxy.has_value()) {
		return csproxy->SetValue(value);
	}

	const SettingDesc *sd = GetSettingFromName(setting);
	if (sd == nullptr || !sd->IsIntSetting()) return false;

	if ((sd->flags & SF_NO_NETWORK_SYNC) != 0) return false;

	value = Clamp<SQInteger>(value, INT32_MIN, INT32_MAX);

	return ScriptObject::DoCommand(0, 0, value, CMD_CHANGE_SETTING, sd->name);
}

/* static */ bool ScriptGameSettings::IsDisabledVehicleType(ScriptVehicle::VehicleType vehicle_type)
{
	switch (vehicle_type) {
		case ScriptVehicle::VT_RAIL:  return _settings_game.ai.ai_disable_veh_train;
		case ScriptVehicle::VT_ROAD:  return _settings_game.ai.ai_disable_veh_roadveh;
		case ScriptVehicle::VT_WATER: return _settings_game.ai.ai_disable_veh_ship;
		case ScriptVehicle::VT_AIR:   return _settings_game.ai.ai_disable_veh_aircraft;
		default:                       return true;
	}
}
