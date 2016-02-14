// template_vehicle_func.h
#ifndef TEMPLATE_VEHICLE_FUNC_H
#define TEMPLATE_VEHICLE_FUNC_H

#include "stdafx.h"
#include "window_gui.h"

#include "tbtr_template_vehicle.h"

//void DrawTemplateVehicle(TemplateVehicle*, int, const Rect&);


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

//Train* VirtualTrainFromTemplateVehicle(TemplateVehicle*);

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
