// template_vehicle_func.h
#ifndef TEMPLATE_VEHICLE_FUNC_H
#define TEMPLATE_VEHICLE_FUNC_H

#include "stdafx.h"
#include "window_gui.h"

#include "tbtr_template_vehicle.h"

//void DrawTemplateVehicle(TemplateVehicle*, int, const Rect&);
void DrawTemplateVehicle(const TemplateVehicle*, int, int, int, VehicleID, int, VehicleID);

void BuildTemplateGuiList(GUITemplateList*, Scrollbar*, Owner, RailType);

Money CalculateOverallTemplateCost(const TemplateVehicle*);

void DrawTemplateTrain(const TemplateVehicle*, int, int, int);

SpriteID GetSpriteID(EngineID, bool);

void DrawTemplate(const TemplateVehicle*, int, int, int);

int GetTemplateDisplayImageWidth(EngineID);

TemplateVehicle *CreateNewTemplateVehicle(EngineID);

void setupVirtTrain(const TemplateVehicle*, Train*);

TemplateVehicle* TemplateVehicleFromVirtualTrain(Train*);

Train* VirtualTrainFromTemplateVehicle(TemplateVehicle*);

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

CommandCost TestBuyAllTemplateVehiclesInChain(Train*);
CommandCost CalculateTemplateReplacementCost(Train*);

short CountEnginesInChain(Train*);

bool TemplateVehicleContainsEngineOfRailtype(const TemplateVehicle*, RailType);

Train* CloneVirtualTrainFromTrain(const Train *);
TemplateVehicle* CloneTemplateVehicleFromTrain(const Train *);

void TransferCargoForTrain(Train*, Train*);

#endif
