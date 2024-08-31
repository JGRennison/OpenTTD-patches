/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_vehicle_func.h Template-based train replacement: template vehicle functions headers. */

#ifndef TEMPLATE_VEHICLE_FUNC_H
#define TEMPLATE_VEHICLE_FUNC_H

#include "stdafx.h"
#include "window_gui.h"
#include "tbtr_template_vehicle.h"
#include "3rdparty/cpp-btree/btree_set.h"

Train *VirtualTrainFromTemplateVehicle(const TemplateVehicle *tv, StringID &err, uint32_t user);

void BuildTemplateGuiList(GUITemplateList *, Scrollbar *, Owner, RailType);

Money CalculateOverallTemplateCost(const TemplateVehicle *);
Money CalculateOverallTemplateDisplayRunningCost(const TemplateVehicle *);

void DrawTemplate(const TemplateVehicle *, int, int, int, int);

TemplateVehicle *TemplateVehicleFromVirtualTrain(Train *virt);
Train* DeleteVirtualTrain(Train *, Train *);
void SetupTemplateVehicleFromVirtual(TemplateVehicle *tmp, TemplateVehicle *prev, Train *virt);

CommandCost CmdTemplateReplaceVehicle(Train *, bool, DoCommandFlag);

TemplateVehicle *GetTemplateVehicleByGroupID(GroupID gid);
TemplateVehicle *GetTemplateVehicleByGroupIDRecursive(GroupID gid);
Train *ChainContainsEngine(EngineID eid, Train *chain);

struct TemplateDepotVehicles {
	btree::btree_set<VehicleID> vehicles;

	void Init(TileIndex tile);
	void RemoveVehicle(VehicleID id);
	Train* ContainsEngine(EngineID eid, Train *not_in);
};

uint CountTrainsNeedingTemplateReplacement(GroupID g_id, const TemplateVehicle *tv);

CommandCost TestBuyAllTemplateVehiclesInChain(const TemplateVehicle *tv, TileIndex tile);

CommandCost CmdRefitTrainFromTemplate(Train *t, const TemplateVehicle *tv, DoCommandFlag flags);
CommandCost CmdSetTrainUnitDirectionFromTemplate(Train *t, const TemplateVehicle *tv, DoCommandFlag flags);
void BreakUpRemainders(Train *t);

bool TemplateVehicleContainsEngineOfRailtype(const TemplateVehicle *tv, RailType type);

void TransferCargoForTrain(Train *old_veh, Train *new_head);

void NeutralizeStatus(Train *t);

enum TBTRDiffFlags {
	TBTRDF_NONE    = 0,      ///< no difference between train and template
	TBTRDF_CONSIST = 1 << 0, ///< consist (vehicle units) differs between train and template
	TBTRDF_REFIT   = 1 << 1, ///< refit differs between train and template
	TBTRDF_DIR     = 1 << 2, ///< unit direction differs between train and template
	TBTRDF_ALL     = TBTRDF_CONSIST | TBTRDF_REFIT | TBTRDF_DIR,
};
DECLARE_ENUM_AS_BIT_SET(TBTRDiffFlags)

TBTRDiffFlags TrainTemplateDifference(const Train *t, const TemplateVehicle *tv);

void UpdateAllTemplateVehicleImages();

inline void InvalidateTemplateReplacementImages()
{
	_template_vehicle_images_valid = false;
}

#endif
