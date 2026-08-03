/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file video_driver_base.hpp Base of all video drivers base, with a smaller interface. */

#ifndef VIDEO_VIDEO_DRIVER_BASE_HPP
#define VIDEO_VIDEO_DRIVER_BASE_HPP

#include "../driver.h"
#include "../core/geometry_type.hpp"
#include <vector>

extern std::string _ini_videodriver;
extern std::vector<Dimension> _resolutions;
extern Dimension _cur_resolution;
extern bool _rightclick_emulate;
extern bool _video_hw_accel;
extern bool _video_vsync;

/** The base of all video drivers. */
class VideoDriverBase : public Driver {
public:
	/**
	 * Mark a particular area dirty.
	 * @param left   The left most line of the dirty area.
	 * @param top    The top most line of the dirty area.
	 * @param width  The width of the dirty area.
	 * @param height The height of the dirty area.
	 */
	virtual void MakeDirty(int left, int top, int width, int height) = 0;

	/**
	 * Perform the actual drawing.
	 */
	virtual void MainLoop() = 0;

	/**
	 * Change the resolution of the window.
	 * @param w The new width.
	 * @param h The new height.
	 * @return True if the change succeeded.
	 */
	virtual bool ChangeResolution(int w, int h) = 0;

	/**
	 * Change the full screen setting.
	 * @param fullscreen The new setting.
	 * @return True if the change succeeded.
	 */
	virtual bool ToggleFullscreen(bool fullscreen) = 0;

	/**
	 * Change the vsync setting.
	 * @param vsync The new setting.
	 */
	virtual void ToggleVsync([[maybe_unused]] bool vsync) {}

	/**
	 * Callback invoked after the blitter was changed.
	 * @return True if no error.
	 */
	virtual bool AfterBlitterChange()
	{
		return true;
	}

	/**
	 * Claim the exclusive rights for the mouse pointer.
	 */
	virtual void ClaimMousePointer() {}

	/**
	 * Get whether the mouse cursor is drawn by the video driver.
	 * @return True if cursor drawing is done by the video driver.
	 */
	virtual bool UseSystemCursor()
	{
		return false;
	}

	/**
	 * Populate all sprites in cache.
	 */
	virtual void PopulateSystemSprites() {}

	/**
	 * Clear all cached sprites.
	 */
	virtual void ClearSystemSprites() {}

	/**
	 * Whether the driver has a graphical user interface with the end user.
	 * Or in other words, whether we should spawn a thread for world generation
	 * and NewGRF scanning so the graphical updates can keep coming. Otherwise
	 * progress has to be shown on the console, which uses by definition another
	 * thread/process for display purposes.
	 * @return True for all drivers except null and dedicated.
	 */
	virtual bool HasGUI() const
	{
		return true;
	}

	/**
	 * Has this video driver an efficient code path for palette animated 8-bpp sprites?
	 * @return True if the driver has an efficient code path for 8-bpp.
	 */
	virtual bool HasEfficient8Bpp() const
	{
		return false;
	}

	/**
	 * Does this video driver support a separate animation buffer in addition to the colour buffer?
	 * @return True if a separate animation buffer is supported.
	 */
	virtual bool HasAnimBuffer()
	{
		return false;
	}

	/**
	 * Get a pointer to the animation buffer of the video back-end.
	 * @return Pointer to the buffer or nullptr if no animation buffer is supported.
	 */
	inline uint8_t *GetAnimBuffer()
	{
		return this->anim_buffer;
	}

	/**
	 * An edit box lost the input focus. Abort character compositing if necessary.
	 */
	virtual void EditBoxLostFocus() {}

	/**
	 * An edit box gained the input focus
	 */
	virtual void EditBoxGainedFocus() {}

	/**
	 * Get a list of refresh rates of each available monitor.
	 * @return A vector of the refresh rates of all available monitors.
	 */
	virtual std::vector<int> GetListOfMonitorRefreshRates()
	{
		return {};
	}

	/**
	 * Get some information about the selected driver/backend to be shown to the user.
	 * @return The information.
	 */
	virtual const char *GetInfoString() const
	{
		return this->GetName();
	}

	/**
	 * Prevents the system from going to sleep.
	 *
	 * @param inhibited If true, sleep will be disabled. If false, sleep will be enabled.
	 */
	virtual void SetScreensaverInhibited([[maybe_unused]] bool inhibited) {}

	/**
	 * Get the currently active instance of the video driver.
	 * @return The instance.
	 */
	static VideoDriverBase *GetInstance()
	{
		return static_cast<VideoDriverBase *>(DriverFactoryBase::GetActiveDriver(Driver::Type::Video).get());
	}

protected:
	uint8_t *anim_buffer = nullptr; ///< Animation buffer, (not used by all drivers, here because it is accessed very frequently)
};

#endif /* VIDEO_VIDEO_DRIVER_BASE_HPP */
