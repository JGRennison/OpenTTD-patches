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

Train* VirtualTrainFromTemplateVehicle(TemplateVehicle* tv, StringID &err);

void BuildTemplateGuiList(GUITemplateList*, Scrollbar*, Owner, RailType);

Money CalculateOverallTemplateCost(const TemplateVehicle*);

void DrawTemplate(const TemplateVehicle*, int, int, int);

TemplateVehicle* TemplateVehicleFromVirtualTrain(Train *virt);
Train* DeleteVirtualTrain(Train*, Train *);
void SetupTemplateVehicleFromVirtual(TemplateVehicle *tmp, TemplateVehicle *prev, Train *virt);

CommandCost CmdTemplateReplaceVehicle(Train*, bool, DoCommandFlag);

#ifdef _DEBUG
// for testing
void tbtr_debug_pat();
void tbtr_debug_pav();
void tbtr_debug_ptv(TemplateVehicle*);
void tbtr_debug_pvt(const Train*);
#endif

TemplateVehicle* GetTemplateVehicleByGroupID(GroupID);
TemplateVehicle* GetTemplateVehicleByGroupIDRecursive(GroupID);
bool ChainContainsVehicle(Train*, Train*);
Train* ChainContainsEngine(EngineID, Train*);
Train* DepotContainsEngine(TileIndex, EngineID, Train*);

int NumTrainsNeedTemplateReplacement(GroupID, const TemplateVehicle*);

CommandCost TestBuyAllTemplateVehiclesInChain(TemplateVehicle *tv, TileIndex tile);

CommandCost CmdRefitTrainFromTemplate(Train *t, TemplateVehicle *tv, DoCommandFlag);
void BreakUpRemainders(Train *t);

bool TemplateVehicleContainsEngineOfRailtype(const TemplateVehicle*, RailType);

void TransferCargoForTrain(Train*, Train*);

void NeutralizeStatus(Train *t);

bool TrainMatchesTemplate(const Train *t, const TemplateVehicle *tv);
bool TrainMatchesTemplateRefit(const Train *t, const TemplateVehicle *tv);

#endif
