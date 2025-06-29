/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargomonitor.cpp Implementation of the cargo transport monitoring. */

#include "stdafx.h"
#include "cargomonitor.h"
#include "station_base.h"

#include "safeguards.h"

CargoMonitorMap _cargo_pickups;    ///< Map of monitored pick-ups   to the amount since last query/activation.
CargoMonitorMap _cargo_deliveries; ///< Map of monitored deliveries to the amount since last query/activation.

/**
 * Helper method for #ClearCargoPickupMonitoring and #ClearCargoDeliveryMonitoring.
 * Clears all monitors that belong to the specified company or all if #INVALID_OWNER
 * is specified as company.
 * @param cargo_monitor_map reference to the cargo monitor map to operate on.
 * @param company company to clear cargo monitors for or #INVALID_OWNER if all cargo monitors should be cleared.
 */
static void ClearCargoMonitoring(CargoMonitorMap &cargo_monitor_map, CompanyID company = INVALID_OWNER)
{
	if (company == INVALID_OWNER) {
		cargo_monitor_map.clear();
		return;
	}

	for (CargoMonitorMap::iterator it = cargo_monitor_map.begin(); it != cargo_monitor_map.end();) {
		if (DecodeMonitorCompany(it->first) == company) {
			it = cargo_monitor_map.erase(it);
		} else {
			++it;
		}
	}
}

/**
 * Clear all pick-up cargo monitors.
 * @param company clear all pick-up monitors for this company or if #INVALID_OWNER
 * is passed, all pick-up monitors are cleared regardless of company.
 */
void ClearCargoPickupMonitoring(CompanyID company)
{
	ClearCargoMonitoring(_cargo_pickups, company);
}

/**
 * Clear all delivery cargo monitors.
 * @param company clear all delivery monitors for this company or if #INVALID_OWNER
 * is passed, all delivery monitors are cleared regardless of company.
 */
void ClearCargoDeliveryMonitoring(CompanyID company)
{
	ClearCargoMonitoring(_cargo_deliveries, company);
}

/**
 * Get and reset the amount associated with a cargo monitor.
 * @param[in,out] monitor_map Monitoring map to search (and reset for the queried entry).
 * @param monitor Cargo monitor to query/reset.
 * @param keep_monitoring After returning from this call, continue monitoring.
 * @return Amount collected since last query/activation for the monitored combination.
 */
static int32_t GetAmount(CargoMonitorMap &monitor_map, CargoMonitorID monitor, bool keep_monitoring)
{
	CargoMonitorMap::iterator iter = monitor_map.find(monitor);
	if (iter == monitor_map.end()) {
		if (keep_monitoring) {
			std::pair<CargoMonitorID, uint32_t> p(monitor, 0);
			monitor_map.insert(p);
		}
		return 0;
	} else {
		int32_t result = iter->second;
		iter->second = 0;
		if (!keep_monitoring) monitor_map.erase(iter);
		return result;
	}
}

/**
 * Get the amount of cargo delivered for the given cargo monitor since activation or last query.
 * @param monitor Cargo monitor to query.
 * @param keep_monitoring After returning from this call, continue monitoring.
 * @return Amount of delivered cargo for the monitored combination.
 */
int32_t GetDeliveryAmount(CargoMonitorID monitor, bool keep_monitoring)
{
	return GetAmount(_cargo_deliveries, monitor, keep_monitoring);
}

/**
 * Get the amount of cargo picked up for the given cargo monitor since activation or last query.
 * @param monitor Monitoring number to query.
 * @param keep_monitoring After returning from this call, continue monitoring.
 * @return Amount of picked up cargo for the monitored combination.
 * @note Cargo pick up is counted on final delivery, to prevent users getting credit for picking up cargo without delivering it.
 */
int32_t GetPickupAmount(CargoMonitorID monitor, bool keep_monitoring)
{
	return GetAmount(_cargo_pickups, monitor, keep_monitoring);
}

/**
 * Cargo was delivered to its final destination, update the pickup and delivery maps.
 * @param cargo_type type of cargo.
 * @param company company delivering the cargo.
 * @param amount Amount of cargo delivered.
 * @param src source of cargo.
 * @param st station where the cargo is delivered to.
 * @param dest industry index where the cargo is delivered to.
 */
void AddCargoDelivery(CargoType cargo_type, CompanyID company, uint32_t amount, Source src, const Station *st, IndustryID dest)
{
	if (amount == 0) return;

	if (src.id != INVALID_SOURCE) {
		/* Handle pickup update. */
		switch (src.type) {
			case SourceType::Industry: {
				CargoMonitorID num = EncodeCargoIndustryMonitor(company, cargo_type, src.id);
				CargoMonitorMap::iterator iter = _cargo_pickups.find(num);
				if (iter != _cargo_pickups.end()) iter->second += amount;
				break;
			}
			case SourceType::Town: {
				CargoMonitorID num = EncodeCargoTownMonitor(company, cargo_type, src.id);
				CargoMonitorMap::iterator iter = _cargo_pickups.find(num);
				if (iter != _cargo_pickups.end()) iter->second += amount;
				break;
			}
			default: break;
		}
	}

	/* Handle delivery.
	 * Note that delivery in the right area is sufficient to prevent trouble with neighbouring industries or houses. */

	/* Town delivery. */
	CargoMonitorID num = EncodeCargoTownMonitor(company, cargo_type, st->town->index);
	CargoMonitorMap::iterator iter = _cargo_deliveries.find(num);
	if (iter != _cargo_deliveries.end()) iter->second += amount;

	/* Industry delivery. */
	for (const auto &i : st->industries_near) {
		if (i.industry->index != dest) continue;
		CargoMonitorID num = EncodeCargoIndustryMonitor(company, cargo_type, i.industry->index);
		CargoMonitorMap::iterator iter = _cargo_deliveries.find(num);
		if (iter != _cargo_deliveries.end()) iter->second += amount;
	}
}

