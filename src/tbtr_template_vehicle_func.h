/* $Id$ */

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

Train* VirtualTrainFromTemplateVehicle(TemplateVehicle* tv);

void DrawTemplateVehicle(const TemplateVehicle*, int, int, int, VehicleID, int, VehicleID);

void BuildTemplateGuiList(GUITemplateList*, Scrollbar*, Owner, RailType);

Money CalculateOverallTemplateCost(const TemplateVehicle*);

void DrawTemplateTrain(const TemplateVehicle*, int, int, int);

SpriteID GetSpriteID(EngineID, bool);

void DrawTemplate(const TemplateVehicle*, int, int, int);

int GetTemplateDisplayImageWidth(EngineID);

TemplateVehicle *CreateNewTemplateVehicle(EngineID);

void setupVirtTrain(const TemplateVehicle*, Train*);

TemplateVehicle* TemplateVehicleFromVirtualTrain(Train *virt);

inline TemplateVehicle* Last(TemplateVehicle*);

TemplateVehicle *DeleteTemplateVehicle(TemplateVehicle*);

Train* DeleteVirtualTrainPart(Train*, Train*);
Train* DeleteVirtualTrain(Train*, Train *);

CommandCost CmdTemplateReplaceVehicle(Train*, bool, DoCommandFlag);

void pat();
void pav();
void ptv(TemplateVehicle*);
void pvt(const Train*);
// for testing
TemplateVehicle* GetTemplateVehicleByGroupID(GroupID);
bool ChainContainsVehicle(Train*, Train*);
Train* ChainContainsEngine(EngineID, Train*);
Train* DepotContainsEngine(TileIndex, EngineID, Train*);

int NumTrainsNeedTemplateReplacement(GroupID, TemplateVehicle*);

CommandCost CalculateTemplateReplacementCost(Train*);
CommandCost TestBuyAllTemplateVehiclesInChain(TemplateVehicle *tv, TileIndex tile);

void CmdRefitTrainFromTemplate(Train *t, TemplateVehicle *tv, DoCommandFlag);
void BreakUpRemainders(Train *t);

short CountEnginesInChain(Train*);

bool TemplateVehicleContainsEngineOfRailtype(const TemplateVehicle*, RailType);

void TransferCargoForTrain(Train*, Train*);

void NeutralizeStatus(Train *t);

bool TrainMatchesTemplate(const Train *t, TemplateVehicle *tv);
bool TrainMatchesTemplateRefit(const Train *t, TemplateVehicle *tv);

#endif
