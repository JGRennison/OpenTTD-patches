/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file video_driver.hpp Base of all video drivers. */

#ifndef VIDEO_VIDEO_DRIVER_HPP
#define VIDEO_VIDEO_DRIVER_HPP

#include "video_driver_base.hpp"
#include "../driver.h"
#include "../core/geometry_type.hpp"
#include "../core/math_func.hpp"
#include "../gfx_func.h"
#include "../settings_type.h"
#include "../zoom_type.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>

/** The base of all video drivers. */
class VideoDriver : public VideoDriverBase {
	static constexpr uint DEFAULT_WINDOW_WIDTH = 640u;  ///< Default window width.
	static constexpr uint DEFAULT_WINDOW_HEIGHT = 480u; ///< Default window height.

public:
	VideoDriver(bool uses_hardware_acceleration = false) : fast_forward_key_pressed(false), fast_forward_via_key(false), is_game_threaded(true), uses_hardware_acceleration(uses_hardware_acceleration) {}

	/**
	 * Queue a function to be called on the main thread with game state
	 * lock held and video buffer locked. Queued functions will be
	 * executed on the next draw tick.
	 * @param func Function to call.
	 */
	void QueueOnMainThread(std::function<void()> &&func)
	{
		std::lock_guard<std::mutex> lock(this->cmd_queue_mutex);

		this->cmd_queue.emplace_back(std::forward<std::function<void()>>(func));
	}

	void GameLoopPause();

	/**
	 * Get the currently active instance of the video driver.
	 * @return The instance.
	 */
	static VideoDriver *GetInstance()
	{
		return static_cast<VideoDriver *>(DriverFactoryBase::GetActiveDriver(Driver::Type::Video).get());
	}

	static std::string GetCaption();

	/**
	 * Helper struct to ensure the video buffer is locked and ready for drawing. The destructor
	 * will make sure the buffer is unlocked no matter how the scope is exited.
	 */
	struct VideoBufferLocker {
		VideoBufferLocker()
		{
			this->unlock = VideoDriver::GetInstance()->LockVideoBuffer();
		}

		~VideoBufferLocker()
		{
			if (this->unlock) VideoDriver::GetInstance()->UnlockVideoBuffer();
		}

	private:
		bool unlock; ///< Stores if the lock did anything that has to be undone.
	};

	static bool EmergencyAcquireGameLock(uint tries, uint delay_ms);

protected:
	const uint ALLOWED_DRIFT = 5; ///< How many times videodriver can miss deadlines without it being overly compensated.

	/**
	 * Get the resolution of the main screen.
	 * @return The dimension of the screen in pixels.
	 */
	virtual Dimension GetScreenSize() const { return { DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT }; }

	/**
	 * Apply resolution auto-detection and clamp to sensible defaults.
	 */
	void UpdateAutoResolution()
	{
		if (_cur_resolution.width == 0 || _cur_resolution.height == 0) {
			/* Auto-detect a good resolution. We aim for 75% of the screen size.
			 * Limit width times height times bytes per pixel to fit a 32 bit
			 * integer, This way all internal drawing routines work correctly. */
			Dimension res = this->GetScreenSize();
			_cur_resolution.width  = ClampU(res.width  * 3 / 4, DEFAULT_WINDOW_WIDTH, UINT16_MAX / 2);
			_cur_resolution.height = ClampU(res.height * 3 / 4, DEFAULT_WINDOW_HEIGHT, UINT16_MAX / 2);
		}
	}

	/**
	 * Handle input logic, is CTRL pressed, should we fast-forward, etc.
	 */
	virtual void InputLoop() {}

	/**
	 * Make sure the video buffer is ready for drawing.
	 * @returns True if the video buffer has to be unlocked.
	 */
	virtual bool LockVideoBuffer()
	{
		return false;
	}

	/**
	 * Unlock a previously locked video buffer.
	 */
	virtual void UnlockVideoBuffer() {}

	/**
	 * Paint the window.
	 */
	virtual void Paint() {}

	/**
	 * Process any pending palette animation.
	 */
	virtual void CheckPaletteAnim() {}

	/**
	 * Process a single system event.
	 * @returns False if there are no more events to process.
	 */
	virtual bool PollEvent() { return false; };

	/**
	 * Start the loop for game-tick.
	 */
	void StartGameThread();

	/**
	 * Stop the loop for the game-tick. This can still tick at most one time before truly shutting down.
	 */
	void StopGameThread();

	/**
	 * Give the video-driver a tick.
	 * It will process any potential game-tick and/or draw-tick, and/or any
	 * other video-driver related event.
	 */
	void Tick();

	/**
	 * Sleep till the next tick is about to happen.
	 */
	void SleepTillNextTick();

	void InvalidateGameOptionsWindow();

	std::chrono::steady_clock::duration GetGameInterval()
	{
#ifdef DEBUG_DUMP_COMMANDS
		/* When replaying, run as fast as we can. */
		extern bool _ddc_fastforward;
		if (_ddc_fastforward) return std::chrono::microseconds(0);
#endif /* DEBUG_DUMP_COMMANDS */

		/* If we are paused, run on normal speed. */
		if (_pause_mode.Any()) return std::chrono::milliseconds(MILLISECONDS_PER_TICK);
		/* Infinite speed, as quickly as you can. */
		if (_game_speed == 0) return std::chrono::microseconds(0);

		return std::chrono::microseconds(MILLISECONDS_PER_TICK * 1000 * 100 / _game_speed);
	}

	std::chrono::steady_clock::duration GetDrawInterval()
	{
		/* If vsync, draw interval is decided by the display driver */
		if (_video_vsync && this->uses_hardware_acceleration) return std::chrono::microseconds(0);
		return std::chrono::microseconds(1000000 / _settings_client.gui.refresh_rate);
	}

	/** Execute all queued commands. */
	void DrainCommandQueue()
	{
		std::vector<std::function<void()>> cmds{};

		{
			/* Exchange queue with an empty one to limit the time we
			 * hold the mutex. This also ensures that queued functions can
			 * add new functions to the queue without everything blocking. */
			std::lock_guard<std::mutex> lock(this->cmd_queue_mutex);
			cmds.swap(this->cmd_queue);
		}

		for (auto &f : cmds) {
			f();
		}
	}

	std::chrono::steady_clock::time_point next_game_tick;
	std::chrono::steady_clock::time_point next_draw_tick;

	bool fast_forward_key_pressed; ///< The fast-forward key is being pressed.
	bool fast_forward_via_key; ///< The fast-forward was enabled by key press.

	bool is_game_threaded;
	std::thread game_thread;
	std::recursive_mutex game_state_mutex;
	std::mutex game_thread_wait_mutex;

	bool uses_hardware_acceleration;

	static void GameThreadThunk(VideoDriver *drv);

private:
	std::mutex cmd_queue_mutex;
	std::vector<std::function<void()>> cmd_queue;

	void GameLoop();
	void GameThread();
};

#endif /* VIDEO_VIDEO_DRIVER_HPP */
