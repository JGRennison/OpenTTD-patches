## JGR's Patchpack Changelog

* * *

### v0.10.1 (2016-01-15)
* Fix FreeType fonts having an incorrect height (regression from v0.10.0).
* Routing restrictions:
  * Fix copying a signal without a program creating an empty program and marking the signal as restricted, instead of not creating a program.
  * Fix GUI issue where if a picker button was clicked when another picker button was already lowered/active, both would be raised.

### v0.10.0 (2016-01-13)
* Fix building rivers being disabled in scenario editor mode.
* Fix minor visual issue with SSE2 palette animator.
* Fix group info window when GUI/font is scaled to a larger size.
* Fix cargo dest overlay not being updated after a zoom change.
* Fix newly cloned routing restrictions not being activated.
* Fix compilation on gcc 4.3 to 4.6.
* Improved breakdowns: Limit low speed breakdowns to lower of 1/4 max speed or 28km/h.
* Zoning: Add mode to show station catchment only where station window open.
* Add vehicle lifetime profit patch, modified to show current lifetime profit, instead of the yearly-updated value.
* Change default measurement tool hotkey to shift-R.
* Minor performance improvement in fonts and viewport text labels.
* Minor configure script library detection changes.
* Add German translations by Auge, for the building rivers in game and improved breakdowns patches.
* Bump trunk base from r27472 to r27495

### v0.9.0 (2015-12-19)
* Improved breakdowns: Fix desync which occured when issuing a "train too heavy" advice message.
* Routing restrictions:
  * Add a conditional to test which company owns the train.
  * OpenGFX electric signal sprites are now considered "default", so can be recoloured blue when restricted.
  * Add a setting to show restricted electric signals using the default sprites (recoloured blue).
* Increase maximum value of max loan difficulty setting.
* Add a 32bpp SSE2 palette animator. This is ~4x faster than the non-accelerated palette animator.
* Version detection: git and the .ottdrev-vc file now override SVN and Hg.
* Bump trunk base from r27466 to r27472

### v0.8.1 (2015-12-06)
* Fix crash when a station is deleted with its departure boards window open.
* Enhanced viewports: Fix new/enlarged route step markers not being redrawn.
* Fix version detection of when git support is missing.
* Label threads with a descriptive name on supported Unixy platforms.
* Performance improvements:
  * Improve dedicated server performance. Up to approximately 2.5x faster.
  * Improve cargodest link graph calculation performance. Up to approximately 33% faster.
* Add German translations by Auge, for the improved breakdowns patch.
* Desync debugging: Changes to debug random logging.
* Bump trunk base from r27455 to r27466

### v0.8.0 (2015-11-24)
* Enhanced viewports:
  * Fix flicker and render errors of tunnels in viewport map mode.
  * Fix displayed height of bridges in viewport map mode.
  * Fix companies with a green colour scheme having sloping tiles and tunnels appearing as flashing yellow in viewport map mode (owner mode).
  * Fix out of bounds memory reads for bridges/tunnels in viewport map mode.
  * Fix a severe performance regression from v0.7.1 involving redrawing of modified vehicle route lines.
* Routing restrictions: Fix non-default signal sprites being recoloured blue for restricted signals.
* Programmable signals:
  * Fix the remove program button not working at all in multiplayer, causing desyncs.
  * Fix the copy program button only working correctly in the most trivial cases, and sometimes crashing.
* Build: Change file names of bundles when building on MinGW.
* Fixes to avoid potentially problematic undefined behaviour.
* Add a setting to add a company-coloured mark next to vehicles in vehicle list windows, if their owner does not match list owner (default on). Patch by McZapkie (modified).
* Desync debugging:
  * If a clients desyncs, the ejected client, the server and all remaining clients run some desync checks.
  * Changes to desync checks and debug levels.
  * No longer write desync messages to the console on Windows, as this can cause crashes, this is now only written to the file.
* Add German translations by Auge, for the adjacent level crossings patch.
* Bump trunk base from r27428 to r27455

### v0.7.1 (2015-11-01)
* Enhanced viewports:
  * Fix flicker and render errors of bridge/tunnels in viewport map mode.
  * Fix rendering, clearing and timely update issues of vehicle route lines.
* Zoning: Fix adding/removing station tiles not redrawing affected surrounding tiles when in the station catchment or unserved building/industry modes.
* Bridges on signals and tunnels:
  * Fix adjacent signals around bridge/tunnels not being updated when adding/updating (on the far side) and removing (on both sides) signals from the bridge/tunnel.
  * Fix middle of bridge not being redrawn when adding/updating/removing signals to bridges.
  * Fix vehicles continually emitting smoke when stopped at a red signal on a bridge.
* Change the default for the auto timetable separation rate company setting to 40%. This is to improve jam-resistance by default.
* Vehicle group info: make margins around text symmetric.
* Fix compilation on some compilers/platforms
* Add the changelog and readme to the bundle/install target.
* Add German translations by Auge, including: vehicle group info, vehicle details window, and the zoning toolbar.
* Bump trunk base from r27415 to r27428

### v0.7.0 (2015-10-29)
* Fix timetable rounding depending on the client time display mode setting, which caused desyncs in multiplayer (departure boards patch bug).
* Add reverse at waypoint orders.
* Change the order occupancy smoothness and automated timetables vehicle lateness adjustment scale settings, to be company settings.
* Fix compilation on some compilers/platforms.
* Bump trunk base from r27410 to r27415

### v0.6.0 (2015-10-17)
* Zoning: Add mode to show restricted signals.
* Pause on savegame load if ctrl key is pressed.
* Fix build and refit with articulated/multihead vehicles with non-zero refit costs.
* Fix YAPF pathfinder desync issue involving rail track type changes which where only passable by a subset of vehicles (trunk bug).
* Add a changelog file to the repository.
* Logging: Add debug category "yapfdesync" to enable desync checks for YAPF only. Save YAPF state dumps to new files on Unix platforms. Fix false positive in FindNearestSafeTile desync check. Log truncated revision strings at debug level 1 instead of 0.
* Bump trunk base from r27403 to r27410

### v0.5.3 (2015-10-03)
* Improved breakdowns patch: Fix non-determinism which caused desyncs in multiplayer.
* Programmable signals patch:
  * Fix programmable signal programs from the previous game not being cleared when starting or loading a new game.
  * Fix crash when the target of a signal state conditional changed to no longer be a rail tile.
  * Fix the invalidation of signal state conditionals when the target signal is removed not being performed correctly.
  * Fix test remove rail actions (e.g. clearing using shift or by opening the land info window) clearing signal programs.
  * Show the coordinates of the target signal in signal state conditionals in the program window.
* Extra large maps patch: Fix the terrain generator setting the height limit to 0 when both edges were longer than 4096 tiles.
* Logging: Improve desync and random logging when running with day length > 1. Log desync debug output to the console as well as the file. Increase thoroughness of vehicle and YAPF cache desync debug checks.
* Bump trunk base from r27402 to r27403

### v0.5.2 (2015-09-26)
* Everest treeline patch: fix planting random trees planting cacti above the snowline.
* Fix house picker window. Change picker/selection logic to be more like the object picker window.
* Bump trunk base from r27395 to r27402

### v0.5.1 (2015-09-18)
* Fix performance regression in road-vehicle path-finding introduced in v0.5.0 due to the maximum map size increase in the extra large maps patch.
* Fix bug in earlier fix for crashes when looking at aqueducts in the viewport map mode of the enhanced viewports patch.
* Fix compilation on some compilers/platforms.

### v0.5.0 (2015-09-13)
* Add the daily/monthly/yearly scripts patch.
* Add the flat minimap screenshot patch.
* Add the extra large maps patch. (Maximum map size is now 256M tiles, ranging from 16k x 16k to 256 x 1M).
* Add the build and refit patch, with changes to make it multi-player safe.
* Fix status bar date when time in minutes and show date with time are both on.
* Fix crash when opening object picker window in scenario editor. (Introduced in trunk r27346).
* Fix no breakdown smoke NewGRF vehicle engine flag being ignored.
* Fix breakdown smoke persisting long after vehicles have gone, with improved breakdowns reduced power/speed breakdowns.
* Fix crash when editing/selecting a conditional order in a non-train orders window.
* Fix crash when loading SpringPP savegame with huge airports (which should be rejected), in cases where the crash occurred before the rejection check was run.
* Scale cargodest link graph timeout and compression intervals by day length.
* Allow only one instance of house picker window, remove button toggle behaviour. This makes it behave more like the object picker window.
* (Unixy platforms) Include bin/data directory in make install target.
* Crash log improvements on MinGW and Unixy/glibc platforms (enable stack traces on MinGW, try to demangle C++ symbol names, try to use libbfd for better symbol lookup, handle SIGSEGV while backtracing).
* Bump trunk base from r27394 to r27395

### v0.4.1 (2015-09-06)
* Fix compilation on some compilers/platforms
* Fix out of date version information not always been detected and rebuilt by the build scripts (this affects the v0.4.0 windows builds posted on the thread)

### v0.4.0 (2015-09-05)
* Fix wait for cargo orders not being properly cleared when loading SpringPP games, resulting in a crash when looking at them in the orders window
* Add a company setting to enable automatic timetabling for new vehicles
* Pause the game instead of blocking when cargo dest link graph jobs lag.
* Update routing restrictions patch:
  * Program GUI changes to make 'or if' conditions easier to add, remove and use.
  * Add a 'reserve through' program command.  
    If a restricted PBS signal uses this command, PBS reservations which would otherwise stop at this signal instead continue through it to the next signal/waiting point. In effect this allows the 'safe waiting point' property of a PBS signal to be conditionally turned off.
  * Improvements to the correctness and thoroughness of the program validator.
* Bump trunk base from r27389 to r27394

### v0.3.2 (2015-08-31)
* Fix crash when loading SpringPP games with day length > 1

### v0.3.1 (2015-08-31)
* Fix crash when opening orders window for competitors' vehicles

### v0.3.0 (2015-08-31)
* Fix more compilation issues on some old compilers/platforms
* Time in minutes is no longer scaled by the day length factor.
* Enable building rivers in game (default off)
* Add more conditional orders patch
* Add train length and group name to vehicle details window
* Add support for loading SpringPP v2.0, v2.1, v2.2 savegames, subject to caveats, see above.
* Misc build-script/version info changes

### v0.2.0 (2015-08-22)
* Fix memory leak in departure boards patch
* Fix dates/times in departure board and timetable windows when day length is greater than 1
* Update routing restrictions patch to include visual indicators for restricted signals (blue signal post)
* Add Everest tree-line patch (with various changes, see above)
* Bump trunk base from r27386 to r27389

### v0.1.2 (2015-08-17)
* Fix signals on bridges in tunnels sometimes permitting adding/modifying/removing signals when occupied by trains.
* Fix compilation on some compilers/platforms
* Fix various compiler warnings
* Misc build-script changes

### v0.1.1 (2015-08-15)
* Fix bug in improved breakdown patch where config string was inserted into the middle of the stop location setting string range, resulting in an assertion failure when looking at the stop location setting in the GUI.
