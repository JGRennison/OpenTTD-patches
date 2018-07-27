## JGR's Patchpack Changelog

* * *

### v0.26.0 (2018-07-27)
* Fix handling of load if available cargo type orders.
* Fix double-accounting of road tunnel/bridge infrastructure counts when changing owner.
* Fix assertion failure on selecting cancel depot/service menu item in vehicle list window.
* Fix a required directory not being created when running 'make install'.
* Update current vehicle order if modifying corresponding order's (per-cargo) load/unload mode.
* Add custom bridge heads for rail bridges.
* Add bidirectional advanced mode for signals on tunnels/bridges.
* Add go to depot and sell vehicle orders.
* Add order mode to lock timetable wait time against autofill/automate changes.
* Add setting to allow placing stations under bridges.
* Add setting to allow placing all NewGRF objects under bridges.
* Routing restrictions:
  * Add "wait at entrance PBS for reservation ending here" action.
  * Add support for slot operations at PBS end signal.
* Bump trunk base from commit 11d1690acb73e77995558dad8fbdde1034e969ed to commit bf8d7df7367055dcfad6cc1c21fd9c762ffc2fe4.
  * This includes an increase in the number of rail track types to 64.
  * This includes an increase in the number of cargoes to 64.
  * This includes a fix for crashes when building tunnels.

### v0.25.2 (2018-06-13)
* Revert upstream trunk changes to font/text rendering on Windows which were merged in v0.25.1. This is to fix various crashes and rendering errors.
* Fix crash when attempting to request information on a large number of unknown GRFs from a multiplayer server.
* Fix compilation failures on ARM and Alpha platforms.
* Minor changes to acquiring of GRF information from multiplayer servers.

### v0.25.1 (2018-06-08)
* Fix savegame save/load and multiplayer join for clients running on Apple/Mac OSX.
* Add setting: station rating tolerance to waiting time depends on cargo class.
* Various minor changes to remove undefined behaviour.
* Bump trunk base from commit 2406500140fa3114d446be667f2bc5152f5cbe30 to commit 11d1690acb73e77995558dad8fbdde1034e969ed.

### v0.25.0 (2018-06-04)
* Multiplayer:
  * Allow up to 256 NewGRFs in multiplayer.
  * Fix displayed game info for maps with one or more dimensions >= 65536 tiles.
* Template-based train replacement:
  * Fix display of vehicle sprites for some NewGRFs.
  * Fix sizing issues in large UI modes.
  * Add 'all rail types' option to rail type dropdown, use by default.
* Through load:
  * Fix crash in handling of unload/transfer cargo payment finalisation.
  * Fix/improve handling of full-load orders, in particular when also using in-station refit.
  * Fix crash when leaving a station when the train head was on a waypoint tile.
  * Fix/improve handling of multi-head engines.
* Improve performance of show scrolling viewport on map feature.
* Improve scrolling rendering and performance of link graph overlays on viewport and smallmap.
* Add setting to automatically save when losing connection to a network game.
* Station rating: Track "last visited vehicle type" separately per cargo.
* Various minor performance improvements.
* Bump trunk base from commit 228f8fba55f55b4233ff635223ceb89f720638a5 to commit 2406500140fa3114d446be667f2bc5152f5cbe30.

### v0.24.1 (2018-05-11)
* Fix crash when using through-load orders with refits.
* Fix configure script not being able to detect clanf on Mac OSX.
* Zoning:
  * Fix overlays on tiles with half-tile foundations.
  * Fix changes in town rating not or only partially refreshing the screen in authority overlay mode.

### v0.24.0 (2018-05-06)
* Fix incorrect rendering of disaster vehicles.
* Routing restrictions:
  * Fix incorrect tile and direction being used for conditional tests in reserve through program execution.
  * Fix crash when removing vehicle from slot.
  * Fix highlighting behaviour in slots window.
  * Add vehicle conditional order which checks slot occupancy.
* Increase maximum value of ticks per minute setting.
* Relax validation for conditional order travel time in old savegame load.
* Fix extended savegame version dump in output of -q command line switch.
* Fix hang when drawing vehicle route lines for conditional orders which form a cycle.
* Fix custom bridge heads being reset when upgrading the bridge.
* Signals on bridges/tunnels:
  * Fix signal simulation and reservation states being reset when upgrading the bridge.
  * Gradually slow down trains in advance of red signals on bridges/tunnels.
  * Fix clearing of train reservations at each end of the bridge/tunnel in some circumstances.
* Fix crash when re-routing cargodest cargo packets in some circumstances.
* Fix timetable auto-separation with go via station orders.
* Fix rendering issue in non-SSE 32bpp blitter for certain types of sprites.
* Zoning: Fix unserved building/industry highlight not being removed when tile cleared.
* Add feature: through load. This is an alternative loading mode for freight trains for the case where the train is longer then the platform.
* Avoid auto-refitting to cargo which is marked no-load in per-cargo type order.
* Vehicle list GUI:
  * Add menu item to mass cancel go to or service at depot orders.
  * Add UI setting to disable mass action buttons for top-level vehicle lists.
* Departure Boards: Allow Ctrl-Click on vehicle type buttons to show type exclusively.
* Bump trunk base from r27968 to commit 228f8fba55f55b4233ff635223ceb89f720638a5.

### v0.23.0 (2018-02-10)
* Template-based train replacement:
  * Fix crashes/failures when both template-based train replacement and autoreplace/autorenew were active on the same vehicle.
  * Enable autorenew when template-based train replacement is active.
* Ship pathfinding:
  * Fix ship pathfinder support for multiple docks. Ships can now head to docks other than the linearly closest one.
  * Improve ship collision avoidance.
* Cargo transfer payments are now paid to companies when the cargo eventually reaches its destination, instead of at the point of transfer.
* Scale displayed vehicle running costs by the day length factor.
* Show stops with timetabled wait time of 0 in departure boards.
* Cargo dest:
  * Improve performance of link graph visual map overlay.
  * Slightly improve link graph calculation performance.
* Slightly improve blitter performance of (32bpp animated) sprite rendering, and line drawing.
* Improve performance of zoning overlays.
* Bump trunk base from r27963 to r27968.

### v0.22.2 (2018-01-14)
* Fix crash when trams attempted do a short turnaround in a tunnel mouth.
* Timetabling:
  * Implement autofill/automate for taken conditional orders.
  * Add UI warnings for conditional order timetabling.
* Fix crash when a company went bankrupt whilst having template replacement virtual trains.
* Vehicle breakdowns:
  * Implement critical breakdown speed reduction for road vehicles.
  * Set a minimum speed for critical breakdown speed reductions.
* Fix incorrect vehicle running costs for day lengths > 3.
* Bump trunk base from r27935 to r27963.

### v0.22.1 (2017-12-10)
* Fix not being able to build water industries when removing water is disabled
* Bump trunk base from r27927 to r27935 (includes trunk fix for right mouse scrolling on recent Windows 10 update)

### v0.22.0 (2017-10-17)
* Template-based train replacement:
  * Fix crash when creating template vehicle in some cases
* Fix crash in bootstrap mode (base graphics not installed yet) when attempting to perform keyboard scrolling
* Fix crash involving freeing of NewGRF modified airport data
* Fix timetabled full-load order warning being shown for non station orders in timetable window
* Fix not being allowed to build docks or ship depots, when removing sea/rivers is disabled
* Fix incorrect scheduling of linkgraph jobs with a large number of nodes which caused poor performance
* Add support for multiple docks per station
* Add show passenger and show freight buttons to departure window
* Add cargo type list filter to vehicle list windows, controlled by a setting
* Bump trunk base from r27912 to r27927

### v0.21.0 (2017-09-05)
* Fix numerical overflow in date display/conversion when using high day lengths
* Fix assertion when a GRF supplies an invalid sound.
* Fix flickering when drawing vehicles in viewport, particularly in viewport map mode.
* Fix possible desync when using scheduled dispatch in multiplayer.
* Towns:
  * Add towns build bridges over rails patch (default off).
  * Add very and extremely slow options to town growth rate setting.
  * Add setting to scale town growth rate by proportion of town cargo transported.
* Add setting to disable removing sea/rivers.
* Programmable signals:
  * Add UI setting for whether programmable signals shown in UI (default off).
  * Remove programmable signals from ctrl-click signal type cycling.
* Add warning/info messages to timetable window.
* Add ctrl+click on shared list button in order/timetable window to add single vehicle to a new group.
* Move some settings in interface category of settings window.
* Add Korean translations by kiwitreekor.
* Add German translations by Auge and kruemelmagic.
* Bump trunk base from r27891 to r27912

### v0.20.1 (2017-07-27)
* Scheduled dispatch:
  * Fix hang when decloning vehicle orders.
  * Fix crash when cloning vehicle with no orders.
  * Improve arrival/departure time prediction in departure board.
* Fix sending money to companies in single-player mode.
* Fix circumstances in which PBS reservations are made across level crossings when improved level crossing safety is enabled, in edge cases involving train reversing and non-PBS signal blocks.
* Fix incorrect unit conversion factor when calculating power and acceleration in improved breakdowns reduced power breakdowns.
* Add Korean translations by kiwitreekor.
* Bump trunk base from r27886 to r27891

### v0.20.0 (2017-06-25)
* Fix excessive cost of building long rail tunnels.
* Fix not being able to enter 00:00 as a timetable start time.
* Fix trams not reversing on road custom bridge heads where tram tracks end on the bridge head in the direction of the bridge.
* Fix AIs unintentionally building custom bridge heads.
* Add patch: scheduled dispatch feature
* Add support for loading SpringPP v2.3, v2.4 savegames.
* Routing restrictions: Add program append GUI button.
* Increase maximum permitted depot and station name lengths.
* Trains now break down after colliding with a road vehicle.
* Add Korean translations by TELK.
* Bump trunk base from r27870 to r27886

### v0.19.0 (2017-05-10)
* Fix crashes and non-functionality with non-broadcast network chat, regression from v0.18.0.
* Fix crash when using unrelated buttons in timetable window when also inputting a numeric value.
* Template-based train replacement:
  * Fix crash when build and refitting template trains.
* Viewport:
  * Fix graphical glitches with re-drawing viewport order lines in some circumstances.
  * Fix viewport tooltips not being cleared when scrolling using the keyboard.
* Level crossings:
  * Prevent road vehicles from being stopped on level crossings.
  * Add setting to enable improved level crossing safety (default off).
* Routing restrictions:
  * Show routing restriction and/or programmable signal windows when ctrl-clicking signal.
  * Add advanced features: wait at PBS signal, and slots.
* Add setting for alternative transfer payment mode (default off). Calculate leg payment as a journey from the source to the transfer station, minus transfers.
* Fix company finance window being too small when first opened.
* Fix build issues on MacOS/OSX.
* Add Korean translations by TELK.
* Bump trunk base from r27846 to r27870

### v0.18.0 (2017-04-04)
* Fix incorrect behaviour and crashes with custom bridge heads on steep slopes.
* Fix day length not being reset to 1 when loading pre day-length savegames.
* Signals on bridges/tunnels:
  * Fix bridge/tunnel exit PBS signals never being set to green.
* Routing restrictions:
  * Fix evaluation of PBS entry signal conditional after reserve through.
  * Fix removal of PBS entry signal conditional instruction.
  * Add buttons to GUI to move instructions up/down.
  * Allow shallow-removing conditional blocks by use of ctrl+click.
  * Implement instruction scroll-to for PBS entry signal conditional.
* Template-based train replacement:
  * Assume that virtual vehicles are on a suitably powered railtype.
  * Fix virtual vehicles not having their build year set, which caused incorrect properties with some NewGRF sets.
* Enhanced viewport plans:
  * Fix various alignment issues in plans window.
  * Add a show all button.
  * Add ctrl+click to scroll to plan.
* Fix height above sea-level in measurement tool.
* Add chunnel patch (tunnels under bodies of water).
* Add minimum town distance patch.
* Add give money to company patch.
* Bump trunk base from r27747 to r27846

### v0.17.2 (2017-02-22)
* Fix crash due to articulated trams decoupling when doing a U-turn, when the end-of-line was removed mid-way through the turn.
* Fix wrong calculation of company infrastructure totals for road tunnels, which could cause multiplayer desyncs.
* Fix crash when removing secondary road type from tunnel.
* Fix crash and/or multiplayer desync after updating orders of vehicles which refer to stations/depots owned by a company which is being deleted.
* Increase maximum permitted group name length.
* Improvements to crash logging.
* Improve performance of linkgraph, and minor other areas.
* Fix memory leak when aborting linkgraph jobs due to abandoning game.
* Add Korean translations by kiwitreekor.
* Bump trunk base from r27743 to r27747

### v0.17.1 (2017-02-14)
* Fix crash when deleting bridge/tunnel with signals due to company reset/bankruptcy.
* Fix crash in linkgraph job scheduler when the total estimated cost of all jobs is 0.
* Fix crash when using build and refit with NewGRF sets with unpredicatable/dynamic refit costs.
* Fix support for increased number of NewGRFs in single player.

### v0.17.0 (2017-02-07)
* Template-based train replacement:
  * Fix crash on load/join after a company which owns templates goes bankrupt or is bought out.
  * Fix incorrect cost estimates in GUI for templates.
* Fix go to nearest depot and halt orders, not halting.
* Fix vehicle breakdown repair cost being paid when vehicle is being auto-replaced.
* Fix inverted condition for cargo waiting conditional order.
* Fix trains on adjacent non-connected tiles being able to crash into each other.
* Fix various UI/display issues with group collapsing.
* Fix various issues for cargo type orders in multiplayer.
* Fix lifetime profit not being fully reset when renewing vehicle.
* Fix vehicle list windows erroneously including sort by length for ships and aircraft, which caused a crash when used.
* Minor fix: more fully clear timetable state when clearing timetable automation.
* Minor fix: increase cost of removing secondary road-type from bridges/tunnels to correct amount.
* Add custom bridge heads for road bridges.
* Increase maximum setting limits for per-company vehicle-type limits.
* Increase maximum permitted vehicle name length.
* Signals on tunnel/bridges are now included in company infrastructure stats.
* Add a natural sort function to use if not compiling with ICU.
* Bump trunk base from r27719 to r27743

### v0.16.1 (2017-01-05)
* Fix hang which could occur when using conditional orders and cargodest.
* Fix vehicle repair costs patch, and change cost algorithm to avoid excessive costs.
* Fix redrawing of viewport order lines/markers for multiplayer clients.
* Fix template replacement virtual vehicles from appearing in advice news messages.
* Signals on bridges/tunnels:
  * Fix train crash on bridge/tunnel with signals leaving red signals behind when crash cleared.
  * Change back of bridge/tunnel PBS exit to be a line-end safe waiting point.
* Add shift-clicking on vehicle depot button to select specific depot.
* Show warning dialog if NewGRFs use too many string IDs.
* Add Korean translations by kiwitreekor.
* Bump trunk base from r27680 to r27719

### v0.16.0 (2016-11-26)
* Fix wrong calculation of infrastructure sharing train repayment on track deletion due to track owner bankruptcy.
* Fix loaded SpringPP savegames having wrong red/green state of signals on bridges/tunnels.
* Add setting to increase station catchment radius.
* Allow town cargo generation factor setting to be more finely adjusted (0.1 increments).
* Cargo dest:
  * Changes to link graph job scheduling algorithm, to improve responsiveness of updates.
  * Add hover tool-tips to graph legend window.
  * Improve visual contrast of graph legend window cargo labels.
* Bump trunk base from r27661 to r27680

### v0.15.1 (2016-10-12)
* Fix incorrect behaviour or multiplayer desync when saving/loading or joining a game with a per cargo type order as a vehicle's current order.
* Enhanced viewports:
  * If an order list includes the same destination a large number of times, show a single marker instead of a large stack.
  * Improve performance of route markers/lines.
* Departure board windows can now be scrolled using the mouse wheel.
* Bump trunk base from r27656 to r27661

### v0.15.0 (2016-09-27)
* Signals on bridges/tunnels:
  * Fix crash when dragging signals over bridges/tunnels.
  * Fix bridge/tunnel entrance signal not always being redrawn when state changes.
  * Display correct signal state for all signals on bridge middle part, not just first 16.
* Add cargo type orders patch, this allows order load/unload types to be set per cargo type.
* Add random town road reconstruction patch (default off).
* Add patch: when building tunnels, open new viewports at the far end of the tunnel.
* Template-based train replacement:
  * Fix vehicle not being unselected when aborting drag.
  * Sell button now lowers on hover.
* Enhanced viewports: Fix route markers/lines being hidden when using drop-down menus in the order window.
* Compiler requirements change: C++11 support is now required.
* Improve clang compiler support.
* Various minor performance improvements.
* Bump trunk base from r27623 to r27656

### v0.14.0 (2016-07-27)
* Routing restrictions:
  * Add conditionals: train weight, power, max T.E., power/weight and max T.E/weight.
  * Add client setting to show train weight ratios in details header.
  * Allow value of "PBS entry signal" conditional to be a rail depot.
  * Fix reservation through multiple reserve-through signals after a junction.
  * Fix compliation on MSVC.
* Template-based train replacement:
  * Fix crash when attempting to create a template train which used certain NewGRF features.
  * Select most used rail type by default, instead of first rail type.
* Fix loading of SpringPP savegames (regression in jgrpp-0.13.1).
* Fix crash involving very long articulated vehicles in preview window.
* Enhanced viewports: Update route markers/lines when drag/dropping vehicle orders.
* Improve performance when not running as a dedicated server in some circumstances.
* Bump trunk base from r27599 to r27623

### v0.13.3 (2016-06-12)
* Fix improved breakdowns reducing aircraft speed to 0 in some circumstances.
* Fix town cargo other than passengers and mail (e.g. ECS tourists) not being generated.
* Fix crash after deleting a template replacement vehicle, when it was in use by more than one group.
* Fix compliation on gcc 6 and some platforms.
* Various improvements to the crash logger.
* Bump trunk base from r27564 to r27599

### v0.13.2 (2016-05-13)
* Fix desync issues by reverting from v4 to v2 of house picking/placing patch, due to desync issue present in v4.
* Fix crash when using start date, autofill or clear/change time buttons in timetable window when vehicle had no orders.
* Timetable start times are no longer subject to rounding when the day length is greater than 1.
* Bump trunk base from r27555 to r27564

### v0.13.1 (2016-05-09)
* Fix crash when using erroneously present create group from vehicle list menu item in vehicle group GUI, remove menu item from vehicle group GUI.
* Fix incorrect calculation of final delivery payment after a transfer.
* Signals on bridges/tunnels:
  * Fix trains not leaving stations by PBS into non-empty signalled bridge/tunnels.
  * Fix signalled bridge/tunnel not always being redrawn on (un)reservation.
* Auto timetables:
  * Timetable auto separation is now per vehicle, with a company setting for the default. Remove global on-off settings.
  * Fix automate, start date, change time and autofill buttons being shown enabled for other companies' vehicles.
  * Add client setting to show the remainder ticks in timetable, after dividing to days or minutes.
  * Add a company setting to control the number of ticks used in auto-fill timetable rounding.
* Bump trunk base from r27547 to r27555

### v0.13.0 (2016-04-19)
* Fix crash when dragging two-directional block signals onto a bridge or tunnel.
* Add polyline rail track building tool.
* Routing restrictions: Add a train group membership conditional.
* Increase number of available rail track types from 16 to 32.
* Rail signals on bridges and tunnels now use rail-type specific graphics where available.
* Update from v2 to v4 of house picking/placing patch.
* Bump trunk base from r27525 to r27547

### v0.12.1 (2016-03-23)
* Template-based train replacement:
  * Fix crash on join or load as a spectator in multiplayer, if the game contained template trains.
  * Fix desync on join in multiplayer, if the game contained template trains.
  * Fix crash during a bankruptcy/company reset when another company owned a virtual train.
  * Fix drawing of template trains not being clipped to fit within the window.
* Improved breakdowns:
  * Fix aircraft aborting a landing/take off at a heliport/oil rig in the event of a breakdown to head to a hangar, and leaving the heliport/oil rig marked occupied.
  * Add a console command to fix savegames which were left with blocked heliports/oil rigs due to the above issue.
* Enhanced viewports: Fix incorrect rendering and/or buffer over-reads when using viewport map mode without first opening the small map window at the same map height limit.
* Fix shared restricted signal windows not being immediately updated when removing a restricted signal.
* Add menu item to vehicle list windows to assign all vehicles to a new group.
* Extend changing the timetable values of all of a vehicle's orders at once to also include: clear time, and set/clear speed.
* Improvements to crash logging.
* Add German translations by Auge, for the restricted signals, repair cost, ship collision avoidance, and lifetime profit patches.
* Bump trunk base from r27518 to r27525

### v0.12.0 (2016-03-02)
* Fix "remove all trees in scenario editor" being available in game.
* Fix crash when a company went bankrupt whilst one of its vehicles was in the middle of loading/unloading.
* Add template-base train replacement patch, with many fixes/changes.
* Allow changing the timetabled waiting time for all of a vehicle's orders at once.
* Run tile animations at the normal rate regardless of day length factor.
* Routing restrictions:
  * Fix unreserving through a green PBS signal not setting the state to red.
    This also fixes unsuccessful reservation attempts though a reserve-through signal erroneously leaving the signal set to green.
* Infrastructure sharing:
  * Add company settings to enable competitors to buy/renew vehicles in this company's depots.
  * Add setting to control whether trains can crash with trains owned by other companies.
* Signals on bridges/tunnels:
  * Enable PBS reservations to be made up to the signalled entrance to a bridge/tunnel.
  * Show tunnel/bridge exit signal facing correct way with correct colour.
  * Enable setting semaphore/electric type of signals on bridges/tunnels.
  * Also draw signals for tunnel exits.
  * Fix drag-convert not updating bridge/tunnel direction correctly.
  * Enable bridge/tunnel exit signals to be one-way PBS. Add signal conversion support.
* Enhanced viewports:
  * Fix route step lines not being updated when cloning a vehicle's orders.
* Strip colour codes when writing debug messages to the terminal.
* Improvements to crash logging.
* Bump trunk base from r27505 to r27518

### v0.11.0 (2016-02-04)
* Programmable signals: Fix crash when a programmable signal referenced a signal which was then turned to face the other way, and the programmable signal and then the other signal were then deleted.
* Improved breakdowns:
  * Change the breakdown probability calculation to more closely resemble the original behaviour.
  * Revert airport crash probabilities back to original behaviour, with modified behaviour only during emergency landings.
  * Low power breakdowns now only reduce the power of the engine which has broken down.
  * Low power breakdowns no longer reduce speed directly when using realistic acceleration, trains can coast or accelerate more slowly instead of actively braking.
  * Fix vehicle needs repair speed limits being incorrect in vehicle details.
* Auto timetables:
  * Fix timetable auto not being unset when sharing orders. Clear autofill when sharing/copying orders with timetable auto enabled.
  * Copy timetable auto state when copying orders.
  * Fix set distributed timetable start not working when using minutes.
  * Avoid set distributed timetable start setting start dates in the past.
* Add the hierarchical group collapse patch, with various modifications:
  * Make group collapsing per-window.
  * Add icon for collapsed groups.
  * Only enable collapse & (un)collapse all buttons where useful. Disable collapse button for leaf groups. Disable (un)collapse all button when all non-leaf groups already (un)collapsed.
* Add the ship collision avoidance patch. Add an on/off setting.
* Add the reduced tree growth patch.
* Add the remove all trees in scenario editor patch.
* Add a menu item to the vehicle list to change order target, e.g. for moving depot orders to a different depot.
* Show the average of the order occupancies in the orders window, and add a vehicle sort mode.
* Routing restrictions: Add Long Reserve feature.
* Infrastructure sharing:
  * Trains can now be crashed with trains from other companies
  * PBS reservations are now cleared when other companies' trains are removed due to the company being deleted/bankrupt.
* Cargo dest link graph:
  * Join more than one link graph job at once where possible. This is to prevent a backlog of jobs if the link graph settings are changed mid game.
  * Add setting for link graph times to be in non day length scaled days.
  * Bump trunk base from r27495 to r27505

### v0.10.2 (2016-01-19)
* Improved breakdowns:
  * Fix incorrect train speed limits after a save/load or network join (causing desync issues) after critical/mechanical breakdowns.
  * Reduce severity of train speed limit reduction after critical/mechanical breakdowns, remove reduction limit.

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
