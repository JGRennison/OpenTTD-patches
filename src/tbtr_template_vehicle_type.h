/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_vehicle_type.h Template-based train replacement: template vehicle types. */

#ifndef TBTR_TEMPLATE_VEHICLE_TYPE_H
#define TBTR_TEMPLATE_VEHICLE_TYPE_H

#include "core/pool_id_type.hpp"

struct TemplateVehicle;

struct TemplateIDTag : public PoolIDTraits<uint16_t, 64000, 0xFFFF> {};
using TemplateID = PoolID<TemplateIDTag>;
static constexpr TemplateID INVALID_TEMPLATE = TemplateID::Invalid();

#endif /* TBTR_TEMPLATE_VEHICLE_TYPE_H */
