// _template_gui_main.h

#ifndef TEMPLATE_GUI_H
#define TEMPLATE_GUI_H

#include "engine_type.h"
#include "group_type.h"
#include "vehicle_type.h"
#include "string_func.h"
#include "strings_func.h"

#include "tbtr_template_vehicle.h"
#include "tbtr_template_vehicle_func.h"
#include "tbtr_template_gui_replaceall.h"

typedef GUIList<const Group*> GUIGroupList;

void ShowTemplateReplaceWindow(byte, int);

#endif
