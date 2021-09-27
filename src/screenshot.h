/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file screenshot.h Functions to make screenshots. */

#ifndef SCREENSHOT_H
#define SCREENSHOT_H

void InitializeScreenshotFormats();

const char *GetCurrentScreenshotExtension();

/** Type of requested screenshot */
enum ScreenshotType {
	SC_VIEWPORT,    ///< Screenshot of viewport.
	SC_CRASHLOG,    ///< Raw screenshot from blitter buffer.
	SC_ZOOMEDIN,    ///< Fully zoomed in screenshot of the visible area.
	SC_DEFAULTZOOM, ///< Zoomed to default zoom level screenshot of the visible area.
	SC_WORLD,       ///< World screenshot.
	SC_WORLD_ZOOM,  ///< World screenshot using current zoom level.
	SC_HEIGHTMAP,   ///< Heightmap of the world.
	SC_MINIMAP,     ///< Minimap screenshot.
	SC_TOPOGRAPHY,  ///< Topography screenshot.
	SC_INDUSTRY,    ///< Industry screenshot.
	SC_SMALLMAP,    ///< Smallmap window screenshot.
};

class SmallMapWindow;

void SetupScreenshotViewport(ScreenshotType t, struct Viewport *vp, uint32 width = 0, uint32 height = 0);
bool MakeHeightmapScreenshot(const char *filename);
bool MakeSmallMapScreenshot(unsigned int width, unsigned int height, SmallMapWindow *window);
void MakeScreenshotWithConfirm(ScreenshotType t);
bool MakeScreenshot(ScreenshotType t, std::string name, uint32 width = 0, uint32 height = 0);
bool MakeMinimapWorldScreenshot(const char *name);
bool MakeTopographyScreenshot(const char *name);
bool MakeIndustryScreenshot(const char *name);
void SetScreenshotAuxiliaryText(const char *key, const char *value);
inline void ClearScreenshotAuxiliaryText() { SetScreenshotAuxiliaryText(nullptr, nullptr); }

extern std::string _screenshot_format_name;
extern uint _num_screenshot_formats;
extern uint _cur_screenshot_format;
extern char _full_screenshot_name[MAX_PATH];

#endif /* SCREENSHOT_H */
