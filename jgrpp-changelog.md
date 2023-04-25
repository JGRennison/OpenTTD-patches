## JGR's Patchpack Changelog

* * *

### v0.53.1 (2023-04-25)
* Fix width of station cargo graph window.
* Fix incorrect accounting of cargo time in transit values.
* Fix crash when checking the consistency of cargo time in transit values, when removing/merging companies or after a multiplayer desync has occured.
* Departure boards:
  * Change vehicle names using the long format to the traditional format if the group name column is also shown.
  * Fix position of vehicle type icon in right-to-left languages
  * Fix hidden columns being included in the minimum window width.
* Fix crash which could occur when sorting towns by rating or engines by power/running cost or cargo capacity/running cost on MacOS.
* Bump trunk base from commit 97cfd40649abab832315f00e6a07e5b6b9a17e23 to commit e5af5907ecfe3845adc613a3312695ed8b40bffc.

### v0.53.0 (2023-04-16)
* Fix water infrastructure total when building multi-tile objects on unowned canal, and when building canals over unowned objects on canals.
* Fix crash when showing vehicles with excessively large sprites in the build vehicle window.
* Fix trains slowing down when part-way into a depot with realistic train braking.
* Fix performance issues with order lists with high numbers of conditional order chains/loops.
* Skip over dummy/invalid orders in destination/next order prediction.
* Add setting for rail depot maximum speed.
* Template-based train replacement:
  * Add train information tooltips in template edit window.
  * Fix crash when removing a company with template replacements applied to nested groups.
* Departure boards:
  * Fix handling of missing travel times with conditional orders.
  * Fix terminus detection from via stops.
  * Fix smart terminus detection with circular routes.
  * Scroll departure boards at constant speed, even if paused or fast forwarding.
  * Add departure board via order type, add support for dual via in departure board.
* Vehicle orders:
  * Allow changing colour of orders in order list and timetable windows.
  * Add company setting for whether to remain at station if next order for same station.
  * Add text label order type.
* Add railtype and signals NewGRF variables for signal vertical clearance.
* Routing restrictions: Add status test for if train is stopping at the current order's station/waypoint destination.
* Fix viewport map mode not being refreshed when removing/merging company.
* Allow generating new default name for station (ctrl-click default button in rename station query window).
* Allow exchanging a station's name with another station in the same town.
* Don't use occupancy of unload and leave empty orders for occupancy average.
* Update OpenTTD content server vanilla compatibility to verison 13.0.
* Only log each AI/GS text string error once.
* Bump trunk base from commit 24e9af83aaca7093ca2ab7e5d54565ec63d42433 to commit 97cfd40649abab832315f00e6a07e5b6b9a17e23.

### v0.52.1 (2023-03-25)
* Fix AI/GS scripts which use text strings.
* Add a "very reduced" mode to the vehicle breakdowns setting.
* Template-based train replacement:
  * Trigger early servicing of trains when a template is added/edited which requires the train to be replaced/modified.
  * Fix wagons in a free wagon chain in the depot not always being used for replacement when this is enabled.
  * Engines in the depot are no longer used for replacement if there are any wagons or other engines attached.
* Bump trunk base from commit e5438891e27c0895964e1a030c91295d3b6ef474 to commit 24e9af83aaca7093ca2ab7e5d54565ec63d42433.

### v0.52.0 (2023-03-19)
* Fix template based train replacement build window not being refreshed for engine variant changes.
* Fix building a road stop/waypoint over an existing road stop/waypoint clearing the one-way state.
* Fix water flooding in the scenario editor at day lengths above 4.
* Fix crash if screen resized to be smaller than confirmation window.
* Fix tooltips for vertical link graph lines.
* Fix dropdown strings for the the autosave setting in the settings window.
* NewGRF:
  * Allow ships to carry more than one cargo.
  * Allow NewGRFs to set town zone radii.
* Routing restrictions:
  * Fix values not being fully initialised to their defaults when changing action type in some cases.
  * Re-order action and condition dropdowns.
* Bump trunk base from commit 09f7f32b8d85ea378984908b6a29764d8576284e to commit e5438891e27c0895964e1a030c91295d3b6ef474.

### v0.51.1 (2023-02-28)
* Fix crash when looking at the town growth speed setting in the settings window.
* Fix wrong texts in the about/question mark menu in the main toolbar.
* Fix template based train replacement template edit window not being refreshed after flipping wagons in multiplayer.
* Fix oversized road waypoints overlapping in the build window.
* Improve speed/performance of generating public roads.

### v0.51.0 (2023-02-24)
* Fix crash after timetabling change counter and release slot orders.
* Fix widths of columns in the build vehicle window after map generation.
* Fix scrollbars of text windows (readme, changelog, etc).
* Run water flooding at constant speed at day lengths >= 4.
* Add routing restriction conditional on whether PBS reservation passes through a tile.
* Template-based train replacement:
  * Allow naming templates.
* NewGRF:
  * Allow NewGRFs to set road/tram type road vehicle collision behaviour, and to disallow tunnels for a road/tram type.
  * Allow more than 255 object and road stop types per GRF.
  * Allow NewGRFs to set a height for road stops in the road stop build window.
  * Add railtype/signal variable to get signal side.
* Re-organise language/translation files.
* Bump trunk base from commit 0b2567d882827f3a2c8b9e927c4d7f354e498a58 to commit 09f7f32b8d85ea378984908b6a29764d8576284e.

### v0.50.3 (2023-01-29)
* Fix crash which could occur when loading savegames which were made when a template train was being edited.
* Fix crash which could occur when reloading NewGRFs when the landscape info window is open.
* Fix some NewGRF rail stations having incorrect layouts.
* Fix incorrect water infrastructure totals when building ship depots and docks over water objects.
* Fix the viewport map default display mode setting.
* Fix map maximum height when generating landscape with TGP for maps larger than 4k in both axes.
* Add setting and per-town override for whether towns can build bridges.
* Music: extmidi (Linux): Don't constantly retry if the music player can't be launched, e.g. if timidity is not installed.
* Bump trunk base from commit 83d5e681fc133d2820aff3cf05159bce820e2b56 to commit 0b2567d882827f3a2c8b9e927c4d7f354e498a58.

### v0.50.2 (2023-01-20)
* Fix crash which could occur when refreshing all signal states when rail infrastructure sharing was disabled and signalled tunnel/bridges were present.
* Fix changing the day length shifting the scheduled dispatch start times, and pending timetable start times, when using time in minutes.
* Fix the set timetable start time/date button.
* Fix direction of semaphore no-entry signal sprites for west/east track.
* Fix freight weight multiplier not being applied in the train build window.
* Fix cargo capacity display of articulated engines with no capacity in the leading part, in the build vehicle window.
* Add zoom in support to the minimap window.
* Add per-town override to disable town growth.
* Enabling the go to depot and sell feature no longer changes the behaviour of the go to depot button when the ctrl key is not pressed.
* Infrastructure sharing:
  * Disallow control over other company trains wholly in depots.
  * Allow using mass start/stop buttons in other company depots.
* Bump trunk base from commit f7e2b6ef12b259817d2a4a3705b33f0b09d0eff7 to commit 83d5e681fc133d2820aff3cf05159bce820e2b56.

### v0.50.1 (2022-12-28)
* Fix crash on hovering link graph link where both ends have same position.
* Fix layout issues with the road stop (bus/lorry stop) build window.
* Fix font shadows being drawn for black text when font AA enabled in some windows.
* Changing the day length or the calendar date no longer changes the current time in minutes.
* NewGRF:
  * Allow NewGRFs to enable generating rocky tiles in desert areas.
  * Allow NewGRFs to provide custom graphics for rocky tiles covered with snow.
* Scheduled dispatch: Allow wrapping at midnight when bulk inserting slots into a 24 hour dispatch schedule.
* Bump trunk base from commit daacde44964e4f42899d9d94f88cc398e17901d7 to commit f7e2b6ef12b259817d2a4a3705b33f0b09d0eff7.

### v0.50.0 (2022-12-11)
* Fix cloning/copying aircraft with go to nearest hangar orders.
* Fix custom road stops types which are only for the other bus/lorry type not being disabled in the build window.
* Fix airport catchment overlays not being refreshed when upgrading/moving.
* Fix the maximum zoom out client setting being reset whenever the config file was saved by vanilla OpenTTD.
* Fix plan lines of other companies not being redrawn when public/private mode changed.
* Change the cost of adding/removing signals from tunnels and bridges to be proportional to the number of affected signals.
* Link graph:
  * Fix incorrect link travel time estimates with day lengths greater than 1.
  * Fix link travel time estimates being set too high when refreshing links in some cases.
  * Fix performance issues when adding a new set of links to an existing large link graph.
  * Improve link graph recalculation performance on large graphs.
  * Reduce link graph recalculation scheduling duration on large graphs.
  * Show more information in the overlay tooltip when holding the ctrl key.
* Departure boards:
  * Fix repeated departures when using implicit orders.
  * Fix handling of conditional orders which cannot be predicted.
  * Fix buttons not being updated when changing departure board settings with departure board windows open.
  * Improve various button labels/tooltips and button handling, and setting names.
* Bump trunk base from commit 7c3c92f8b8d0c67d817095d367720272d96882ab to commit daacde44964e4f42899d9d94f88cc398e17901d7.

### v0.49.2 (2022-11-28)
* Fix crash when clicking non-train counter value or scheduled dispatch conditional order.
* Fix order window dropdown issues with timetable state and scheduled dispatch conditional orders.
* Fix companies being shown as passworded in single-player, after exiting a multiplayer game.
* Fix some NewGRFs having incorrect behaviour in some special cases.
* Fix engine changes not updating build template train windows.
* Vehicle list commands (e.g. start/stop, send to depot) now take into account the cargo type filter.
* Viewport route overlay:
  * Fix go to nearest depot orders incorrectly showing as going to a specific depot.
  * Add a mode setting: off, all locations, station stops only.
  * Add an unset hotkey to switch the mode setting.
  * Show viewport route overlay for shared order vehicle list windows.
* Departure boards:
  * Fix crash with vehicles which only have implicit orders.
  * Fix scheduled dispatch departure time when wait time set.
  * Fix windows not being refreshed when changing settings.
* Add a "if breakdowns enabled" mode to the no depot order warn setting, change the default value to off.
* Add a setting for whether the dual pane train purchase window uses combined buttons.
* Do not clear network server save encrypted saved passwords when saving in single-player.
* Minor performance improvements with some NewGRF houses.
* Bump trunk base from commit 019dcb7b7b010ce85260aa075f859d63fa020868 to commit 7c3c92f8b8d0c67d817095d367720272d96882ab.

### v0.49.1 (2022-11-13)
* Fix crash when the unload type of an order was changed to no unload and then back to unload, whilst a vehicle was unloading using that order.
* Fix multiplayer desync when changing the town zone settings in multiplayer.
* Fix multiplayer desync when creating companies in multiplayer when a saved default face feature was present at the client but not the server or vice versa.
* Fix town setting overrides not being allowed for multiplayer admins when the setting to enable for clients was not enabled.
* Fix trains making a PBS reservation when starting a timetabled wait at a waypoint in some cases.
* Fix new companies created in multiplayer not inheriting the client's default company settings.
* Fix crash if a network client's connection fails during the reporting of a connection problem to the server.
* Prevent spread/regrowth of temperate trees already on snowy ground, in the arctic climate.
* Enable upstream feature: prioritise faster routes for passengers, mail and express cargo.
* Increase maximum engine name length.
* Only show edge level crossing overlays for multi-track level crossings, even when the safer crossings settings is off.
* Allow various game settings to be changed in multiplayer, unless prevented by a NewGRF.
* Minor performance improvements to the rendering of normal-mode viewports.
* Bump trunk base from commit 4daad7f34840bcec2a568eb54149286c7f68c892 to commit 019dcb7b7b010ce85260aa075f859d63fa020868.

### v0.49.0 (2022-10-27)
* Fix calculating train curve speed limit on dual rail type tiles.
* Fix crash if GS is removed when GS settings window is open.
* Orders:
  * Add support for duplicating individual orders.
  * Allow moving the jump target of an existing conditional order.
  * Add a change counter value order type.
* Towns:
  * Add setting for if/when towns can build road tunnels.
  * Add setting to limit length of continuous inclined roads built by towns.
  * Allow overriding town road construction settings on a per-town basis, add setting to enable this for multiplayer clients.
* Custom signal style (normal/shunt combined mode):
  * Fix incorrect default for reservations through intermediary shunt signals.
  * Reservations ending in a depot now default to shunt mode.
* MacOS: Re-enable touchbar support.
* Bump trunk base from commit f011a559d01db3eb43e71031ff03fa904a41d068 to commit 4daad7f34840bcec2a568eb54149286c7f68c892.

### v0.48.5 (2022-10-05)
* Fix vehicles with no cargo being shown with the cargo of the last selected vehicle in the build vehicle window.
* Fix animation of NewGRF road stops.
* Fix crash when reloading NewGRFs when the landscape info window is open.
* Fix crash when selecting a release slot order in ship and aircraft order windows.
* Fix hang which could occur with some NewGRFs.
* Slightly reduce the map entropy of tree tiles.
* Bump trunk base from commit 164ec3ac07c514cdce692554f6339ce1f05d8869 to commit f011a559d01db3eb43e71031ff03fa904a41d068.

### v0.48.4 (2022-09-26)
* Fix crash when a tram attempted to turn around against the underside of a tram custom bridge head.
* Fix crash when removing a routing restriction which enabled reserve through from a tunnel/bridge with signals.
* Fix some NewGRFs having incorrect behaviour in some special cases.
* Bump trunk base from commit 81388d9425c63121eeb43bf247fb1458ca6ead92 to commit 164ec3ac07c514cdce692554f6339ce1f05d8869.

### v0.48.3 (2022-09-18)
* Fix crash when joining a multiplayer server when a saved default face is set.
* Fix displayed capacities/weights of articulated vehicles in build window.
* Fix moving and selling template train wagons/engines being disallowed when paused, when build while paused setting disallows construction actions.
* Fix improved breakdowns being incorrectly enabled after loading old/other savegames.
* Allow changing road vehicle driving side when all road vehicles are in depots.
* Add AI/GS script method to get annual expense category value.
* Do not show max TE/weight for maglevs.
* Bump trunk base from commit 03552996395be4c468d64adc7a076e1b233f0d4c to commit 81388d9425c63121eeb43bf247fb1458ca6ead92.

### v0.48.2 (2022-09-09)
* Fix excessive braking for slopes with realistic train braking.
* Fix incorrect infrastructure totals when overbuilding bay road stops with a different road/tram type active.
* Fix route step markers being rendered incorrectly with some fonts or when the zoom level is changed.
* Fix conflicts between company bankruptcy and manually triggered company sales, reduce delays before showing purchase company prompts.
* Fix crash when showing the maximum achievable speed estimate for trains of 0 mass.
* Fix crash which could occur with autoreplace failure news messages in some languages.
* Fix crash which could occur when adding plans in single player.
* Fix some industry NewGRFs having incorrect behaviour (when querying the closest industry of an invalid or non present type).
* Template-based train replacement:
  * Fix incorrect template replacement error message when the template is not buildable.
  * Show warning if template trains are not compatible with any rail type.
* Only apply the highest resolution sprites to use setting when the NewGRF supplies suitable fallback graphics.
* Add a setting to show the introduction year for train wagons.
* Add setting to show group hierarchy in vehicle names.
* Add routing restriction conditional on direction of order target from signal.
* Remove road vehicles during load which are uncorrectably invalid (i.e. when required NewGRFs are missing), instead of crashing.
* AI/GS script:
  * Increase the maximum number of operations which scripts can use when saving the game.
  * Add script functions to get and set inflation factors.
* Bump trunk base from commit ccb9d9988011725c1ff0d415af37efb99e2b0849 to commit 03552996395be4c468d64adc7a076e1b233f0d4c.

### v0.48.1 (2022-08-01)
* Fix various issues that could occur when attempting to disable infrastructure sharing when shared infrastructure is still in use.
* Fix crashes and other issues when removing a company would remove infrastructure which is in use by the train reservation of another company when realistic braking is enabled.
* Fix some NewGRFs having incorrect behaviour (when using variational action 2 variable 1C in some cases).
* Fix crash which could occur with tooltip windows in some special cases.
* Fix viewport map mode bridges/tunnels not appearing dotted at high zoom.
* Fix insufficient train braking when in realistic braking mode when train lookahead is aspect limited is enabled and the lookahead distance is shorter than the reservation.
* Do not enable the behaviour where vehicles continue loading if the next order is for the same station for implicit orders.
* Do not apply the show signals using default graphics settings for custom signal styles
* Bump trunk base from commit 19af139631b5bc98dba6de4c4f0b7aeb6b3ac6aa to commit ccb9d9988011725c1ff0d415af37efb99e2b0849.

### v0.48.0 (2022-07-03)
* Fix crash which could occur after removing non-rectangular airports.
* Fix crash which could occur with non-buildable template trains with some NewGRFs.
* Fix not being able to construct industries of only one tile.
* Fix the land info window showing incorrect text for no-entry signals.
* Fix wrong powered state or visual effect type, or desync warning messages, which could occur with trains from some NewGRFs and tiles of two different rail types.
* Fix reservation error when a reserve through signal was the last tile, when there were no junctions earlier in the reservation.
* Fix desync which could occur after removing part of a station moved the station sign within the catchment of industries.
* Fix not being able to build waypoints when custom types are no longer present and a custom type was previously selected.
* Fix timetable automation not updating conditional jump travel times.
* Fix road/tram type check when moving depot orders to another depot.
* Add setting to show order occupancy values by default.
* Add conditional order to test if last or next scheduled dispatch is the first or last dispatch slot.
* Show group name when grouping vehicles by shared orders, if all vehicles in shared order set are in the same group.
* Add setting to show full group hierarchy in group names.
* Enable shared orders and occupancy buttons for competitor order windows.
* Add button to highlight all signals using a particular routing restriction program.
* Sending a vehicle to a depot for sale can now sell immediately, if the vehicle is already stopped in a suitable depot.
* NewGRF:
  * Allow signal graphics NewGRF to define additional signal styles and test for additional signal properties.
  * Allow NewGRFs to provide custom graphics for landscape rocky tiles.
* Add a hotkey to toggle the via state of an order.
* Remove the tunnel/bridge signal spacing setting, the usual signal spacing setting in the signal window is used instead.
* Slightly boost the realistic braking stats of trains affected by the freight weight multiplier.
* Add a setting to limit train lookahead to the signal aspect when using realistic braking and multi-aspect signalling.
* Bump trunk base from commit 0d3756818fc2178242b0a72d979131a9cb376d76 to commit 19af139631b5bc98dba6de4c4f0b7aeb6b3ac6aa.

### v0.47.3 (2022-06-09)
* Fix being able to add/remove/modify tunnel/bridge signals when occupied by trains, which could result in train or game crashes.
* Fix crash when building public roads encountered level crossings and other non-normal road.
* Fix performance problems refreshing the cargodist link graph when order lists contained many conditional order loops.
* Fix timetable autofill activation when scheduled dispatch is active.
* Disabling timetable automation without holding the ctrl key no longer clears the timetable.
* Support railtype-dependant GRF train speed limits with realistic braking.
* Fix selecting a savegame with realistic braking enabled in the load savegame window triggering realistic braking signal checks on the current game.
* Allow ctrl-clicking on trains of other companies on own track to start/stop.
* Add setting to disable water animation depending on zoom level.
* Add setting to disable object expiry after a given year.
* Add setting to ignore object introduction dates.
* Allow linking only inputs or outputs to smallmap and viewport map mode in industry chain window.
* Viewport map mode:
  * Fix ships not always updating in viewport map mode.
  * Fix the industry chain window not always updating viewports in industry map mode.
  * Fix scrolling viewport overlay over vehicle dots on animated blitters.
  * Fix scrolling viewport overlay on emscripten.
  * Allow using the measurement tool in viewport map mode.
* Trees:
  * Fix tree tile grass not growing when tree growth/spread was disabled.
  * Make tree tile grass growth speed independent of the tree growth speed.
  * Adjust positioning of seasonally variable snow line width for arctic tree placement.
* Improve reliability of crashlog writing on Unix/Linux and MacOS.
* Add various features to the NewGRF debug window.
* Various NewGRF and realistic braking related minor performance improvements.
* Bump trunk base from commit e79724ea22b2c4428ab402a808b7ab777fec2985 to commit 0d3756818fc2178242b0a72d979131a9cb376d76.

### v0.47.2 (2022-05-01)
* Fix crash and/or multiplayer desync after a new industry is built within the catchment of an existing station.
* Fix multiplayer desync after a raise land action removed a water object next to a dock.
* Fix wrong water infrastructure total and multiplayer desync after building canal/river over a canal tile with an object on it.
* Fix adding a new scheduled dispatch schedule not updating the window in multiplayer.
* Make the company infrastructure window scrollable.
* Snow:
  * Fix arctic tree range around snow line setting not handling seasonally variable snow lines.
  * Add a setting to adjust seasonally variable snow line width for arctic tree placement.
  * Fix flat road tiles with foundations on the snow line not being drawn with snow.
* Station names:
  * Increase the distance a station can be from the town centre and still be assigned have the same name as the town (no suffix/prefix), for large towns.
  * Allow extra station name GRFs to use extra names even when there are default names available.
* Bump trunk base from commit 8537fa72063a7376065fd996fa249cc7dbfdb2f3 to commit e79724ea22b2c4428ab402a808b7ab777fec2985.

### v0.47.1 (2022-04-02)
* Fix crash when a road vehicle leaves a bus/truck stop when it is has no orders.
* Fix road vehicles incorrectly being allowed to be ordered to incompatible depots.
* Fix viewport town/industry tooltips being shown on mouseover when in right-click to show tooltips mode.
* Routing restrictions:
  * Fix deny and penalty actions not being applied to no-entry signals.
  * Fix the restricted signal zoning overlay mode not including tunnels/bridges with restricted signals.
  * Fix the PBS reservation end actions incorrectly handling the case where the state of a slot is tested after an instruction which would change the vehicle's membership of the slot.
* Include a specific reason why a vehicle cannot be ordered to a particular station in the error message.
* Bump trunk base from commit 0d8fbf647b2c819bee0a0883b5fc831aa64e4ee0 to commit 8537fa72063a7376065fd996fa249cc7dbfdb2f3.

### v0.47.0 (2022-03-12)
* Fix crash in scheduled dispatch window with nearest depot dispatch order.
* Fix non-rail bridge construction setting polyrail endpoints.
* Fix the autosave interval setting being reset at startup when it was previously set to use a custom interval.
* Add NewGRF road stops.
* Add routing restriction action to make the train exempt from automatic train speed adaptation.
* Add hotkeys for building road waypoints to the road/tram toolbars.
* Implement automatic train speed adaptation on signalled tunnels/bridges.
* Allow configuring the width of tropic zones around water during map generation.
* If an aircraft or road vehicle's next order is for the current station when leaving, start loading again without moving, instead of leaving.
* Bump trunk base from commit 83b6defbfb0fa649a854767ae7c8b5a18f917e80 to commit 0d8fbf647b2c819bee0a0883b5fc831aa64e4ee0.

### v0.46.1 (2022-02-07)
* Fix crash or incorrect text in the scheduled dispatch window when a dispatch schedule is assigned to a depot order.
* Fix crash which could occur when using aircraft with cargodist after loading a 12.x vanilla savegame/scenario.
* Fix some non-vanilla settings having invalid values after loading a 12.x vanilla savegame/scenario.
* Add NewGRF properties for NewGRF object tile type to use in the small map window and in viewport map mode.
* Bump trunk base from commit 2c42b6adc87765750436dc5005e9e186db84daeb to commit 83b6defbfb0fa649a854767ae7c8b5a18f917e80.

### v0.46.0 (2022-02-01)
* Add build vehicle window sort mode: cargo capacity / running cost.
* Add Korean translations by TELK.
* Bump trunk base from commit 9e47df298faf6889c8be7dd0b0eeedeb65db1cdc to commit 2c42b6adc87765750436dc5005e9e186db84daeb.

### v0.46-rc2 (2022-01-29)
* Road waypoints:
  * Fix crash when changing one-way state of road waypoints.
  * Fix crash in road vehicle overtaking checks with road waypoints.
  * Fix removal of road waypoints during bankruptcy.
  * Road waypoints no longer block road inferred one-way state interpolation.
* Fix crash when opening rail waypoint window if there are now fewer types available than the type that was last selected.
* Add Korean translations by TELK.

### v0.46-rc1 (2022-01-28)
* Fix timetable wait times not being cleared when changing to a non-stopping order.
* Fix text input and display of speeds in tiles/day units in routing restriction window.
* Fix industry monthly production figures being able to overflow when industry production scaling is set to a high value.
* Fix station catchment highlight from coverage button in station window not being redrawn when station extents changed.
* Fix various issues in unserved industries zoning overlay mode.
* Fix wrong error message when building a bridge over an obstructing station.
* Fix window preference save/load of build vehicle windows.
* Conditional orders:
  * Fix crash when evaluating a train in slot conditional order when no slot was assigned.
  * Fix manual setting of conditional order jump taken travel times.
  * Improve handling of conditional order waiting loops.
  * Follow predictable conditional orders in timetable and departure windows.
* Add support for multiple scheduled dispatch schedules per order list.
* Allow non-train vehicles to test counter values in conditional orders.
* Add road waypoints.
* Allow road vehicle go to station/waypoint orders to have an associated required stop/bay/waypoint direction.
* Add slot support to road vehicles, ships and aircraft.
* Add train through load speed limit setting.
* Add client setting for whether to sync localisation settings with the server in multiplayer.
* Add client setting to allow hiding viewport labels of individual waypoints.
* Add NewGRF properties for default object map generation amounts.
* Remember the last-used signal type between games.
* Disable touchbar support to fix crash issues on MacOS.
* Add Korean translations by TELK.
* Bump trunk base from commit 93e8d4871d3c927cf08eaa322bfdcd2cb73a1730 to commit 9e47df298faf6889c8be7dd0b0eeedeb65db1cdc.

### v0.45.1 (2022-01-10)
* Fix crash which could occur when removing invalidated link graph flows.
* Fix template replacement without refitting selecting the wrong cargo when using zero capacity engines with a livery cargo.
* Fix wrong signal aspects when track was built up to the rear of a tunnel/bridge entrance.
* Fix ground/tree tile vegetation changes not updating map mode viewports in vegetation mode.
* Scale limit on cargo which can be moved from industries to stations in one step by the cargo production scaling factor.
* Add support for automatic numbering of screenshots saved using the screenshot console command.
* AI/GS script: Add methods related to road and tram types.
* Bump trunk base from commit d62c5667cff2eed82deb18e28d98345500b30d3f to commit 93e8d4871d3c927cf08eaa322bfdcd2cb73a1730.

### v0.45.0 (2022-01-05)
* Fix crash when removing signals from a bridge or tunnel when one or more routing restriction programs were attached.
* Fix crash when a template replacement train had an engine with an invalid cargo type.
* Fix multiplayer desync which could occur after removing track with a signal on it at the end of a PBS reservation with a moving train approaching and realistic braking enabled.
* Fix multiplayer desync which could occur after estimating building a road stop.
* Fix invalid data being wrtten to the config file when display of income texts was disabled.
* Fix give money chat message showing the wrong value in some cases.
* Fix through load failed due to a depot news messages being shown when no problem actually occured in some circumstances.
* Fix re-routing of unrelated cargo to "any station" when removing invalidated link graph flow.
* Fix incorrect window and column widths in the departure boards window.
* Fix newly generated network server ID not being saved in the config file in some circumstances.
* Add support for having more than 256 rail waypoint types.
* Add setting to distribute cargo received at a station to all accepting industries equally, instead of just one of them.
* Add setting to increase the cargodist link graph distance/cost metric of aircraft links.
* Add clear schedule function to the scheduled dispatch window.
* Add client setting to show all signals using the default baseset sprites.
* Store company passwords in network server saves in an encrypted form such that they are automaticaly restored when loaded into the same network server.
* Show vehicle destination when mousing over a vehicle breakdown in the vehicle status bar.
* Allow setting the autosave interval to a custom number of in-game days or real-time minutes.
* Adjust automatic servicing behaviour to avoid unnecessarily cancelling automatic servicing orders.
* If a ship's next order is for the current station when leaving, start loading again without moving, instead of leaving.
* Enable news warning for missing depot order in order list by default.
* Fix dedicated network servers logging too much by default.
* Bump trunk base from commit 6953df7b5e52d749e50275640197e5fc17e2310c to commit d62c5667cff2eed82deb18e28d98345500b30d3f.

### v0.44.2 (2021-12-10)
* Fix multiplayer desync which could occur when using order backups in some circumstances.
* Fix loading of the game log from upstream 12.x savegames.
* Apply negative values of the town cargo generation factor setting more strictly/accurately.
* Add a "default" mode to the timetable autofill rounding setting, use as the new default.
* Add NewGRF properties for NewGRF object ground sprite mode, slope/foundation mode and flood resistance.
* On dedicated servers, save a copy of the last autosave when a crash occurs (when not using the keep_all_autosave setting).
* Add Korean translations by TELK and Galician translations by pvillaverde.

### v0.44.1 (2021-11-29)
* Signals on bridges/tunnels:
  * Fix crash when the ignore signals button is used for wrong-way running on a signalled tunnel/bridge when using a multi-aspect signal GRF.
  * Fix incorrect exit signal state when unable to leave a signalled custom bridge head when the exit direction is different to the bridge direction.
  * Fix pending speed restriction changes not being applied on signalled tunnel/bridges.
  * Fix incorrect PBS reservations in the case where a single-vehicle train's reservation from a tunnel/bridge exit enters the corresponding tunnel/bridge entrance at the opposite end, and the tunnel/bridge is otherwise empty.
  * Fix signals on approach to a tunnel/bridge entrance temporarily showing an incorrect aspect with multi-aspect signalling in the case where the signalling on the tunnel/bridge was modified.
  * Allow placing routing restrictions on tunnel/bridge entrance/exit signals (this does not include reserve through support).
* Realistic braking:
  * Try to extend PBS reservations when approaching the sighting distance of non-end signals.
  * Fix PBS reservations not being extended sufficiently after a target at which the train reverses is found.
* Scheduled dispatch:
  * Fix entering the dispatch duration and max slot delay when using days instead of minutes.
  * Also show hours and minutes for dispatch duration when using minutes.
  * Allow adding multiple departure slots at once.
* Fix trains with non-front parts needing repair not being serviced.
* Fix not all windows being deleted as expected when using the delete key in some cases.
* Fix the ctrl-click signal cycling setting.
* Fix station/waypoint vehicle tooltip showing incorrect ctrl-click text.
* Add settings to reduce vehicle running costs when a vehicle is stationary or in a depot.
* Add setting to disable road vehicles from passing through each other when blocked for an extended period of time (default off).
* Change the map generation allow lakes to spawn in deserts setting to also allow spawning rivers in deserts.
* If a train's next order is for the current station when leaving, start loading again without moving, instead of leaving.
* Run most "daily" vehicle tasks at a fixed frequency instead of daily at day lengths of 8 or more (running cost accounting, track sharing costs, breakdown checks, servicing checks, order checks).
* Only show level crossing overlay sprites on the outsides of multi-track crossings when using both the adjacent crossings and safer crossings settings.
* Increase the object class limit.
* Connect new plan lines to end of the previous line when ctrl-clicking.
* Fix compilation issues on some platforms.
* Bump trunk base from commit 48c1c7f221cd51fbe4fda3771eaed09edacef997 to commit 6953df7b5e52d749e50275640197e5fc17e2310c.

### v0.44.0 (2021-11-10)
* Fix crash on non-GCC/clang compilers.
* Fix custom signal NewGRFs never showing semaphore signals as having a routing restriction program attached.
* Fix compilation issues on some platforms.
* Bump trunk base from commit 9edb75ec0b4ecfb2803728d129b353d1d224beaf to commit 48c1c7f221cd51fbe4fda3771eaed09edacef997.

### v0.44-rc1 (2021-11-03)
* Merge OpenTTD 12.0, including new networking, savegames and configs.
* Bump trunk base from commit 8fa53f543a5929bdbb12c8776ae9577594f9eba7 to commit 9edb75ec0b4ecfb2803728d129b353d1d224beaf.

### v0.43.2 (2021-10-29)
* Fix crash when using the ignore signals button to sent a train the wrong-way on a signalled tunnel or bridge.
* Fix multiplayer desync when using "perfect" tree placement mode in arctic climate.
* Fix aircraft shadows being drawn facing the wrong direction.
* Fix timetabled 0 wait times not being shown for stations/depots in the timetable window.
* Add settings for minimum contiguous landmass size for town and city placement.
* Add current day and current month routing restriction conditionals.
* Add current day and current month conditional orders.
* Company bankruptcy:
  * When declining to buy a company, ask the next company immediately instead of after the time period expires.
  * Do not wait for companies which have no connected clients to buy a company.
  * Add console command to offer a company for sale.
* Add Korean translations by TELK.

### v0.43.1 (2021-10-04)
* Fix multi-aspect signal graphics not being immediately enabled for newly generated maps.
* Fix premature PBS reservations with using reverse at waypoint orders with timetabled wait times.
* Fix incorrect font heights when using custom fonts on MacOS.
* Fix crash when trying to place multitile objects at map edge.
* Routing restrictions:
  * The reverse behind signal pathfinder now takes into account the train length to avoid reversing sidings which are too short.
* Add sort by maximum speed (fully loaded) to train list window.

### v0.43.0 (2021-09-12)
* Fix reversing a train inside a depot disrupting the PBS reservation of another train heading into the depot.
* Fix ships being drawn with the wrong image direction after rotating in place in some circumstances.
* Fix ships with images which depend on speed not being redrawn when the speed has changed.
* Fix signals on dual railtype tiles using wrong per-railtype custom signals.
* Fix conditional order loops on leaving a depot when a timetabled wait time is set.
* Signals on bridges/tunnels:
  * Fix tunnel exit signal not being set to red when train exited.
  * Fix signals on bridge middle parts not using per-railtype custom signals.
  * The signal spacing distance is now fixed at signalling time, changing the company spacing setting now only affects newly signalled bridges/tunnels, not existing ones.
  * The signal spacing distance is now automatically adjusted to fit the tunnel/bridge length. This is to avoid the last middle signal being too close to the exit signal.
* Routing restrictions:
  * Add slot action: try to acquire (only on reserve).
  * Fix last station visited not being set when the reservation ends at the target station, this could cause long-reserve conditionals to use the wrong last station visited value.
  * Fix reverse behind signal pathfinding when there is no dead-end beyond the signal.
* NewGRF:
  * Allow using NewGRF switches (Action 2/3) for general rail signal sprites, in the same way as per-railtype signal sprites.
  * Enable recolouring of signal graphics.
  * Add support for multi-aspect signal graphics (requires realistic braking).
* Realistic braking:
  * Adjust braking constants to slightly increase train braking forces.
  * Block signals into blocks with junctions now default to red with realistic braking.
* Template-based train replacement:
  * Show refitted capacity when adding template vehicles with cargo filter.
  * Show buy cost and running cost in template windows.
  * Allow cloning trains directly from the template train list.
* Add new signal type: no-entry signal. (This is not shown by default).
* Add sort by number of vehicles calling to station list window.
* Add improved breakdowns speed reductions for ships.
* Train speed adaption: adjust look-ahead distances at lower speeds.
* Make remove and routing restriction buttons in the signal build window mutually exclusive.
* Add hotkey support to the signal build window.
* Add spectate menu item to company toolbar menu.
* Send back a message for rcon and settings_access failures.
* Show linear scaling value in settings window for cargo scaling settings.
* Add support for retrieving JGRPP-only content from the Bananas content service.
* Add Korean translations by TELK.

### v0.42.3 (2021-08-04)
* Fix multiplayer server crash when client joined during a threaded save or autosave.
* Fix station ratings tooltip in right click mode.
* Fix send vehicle to specific depot allowing incompatible road/tram types and rail types.
* Fix reversing a train not aborting through loading in some circumstances.
* Fix excessive logging of debugging information by default when running as a dedicated server.
* Fix network servers showing an incorrect client/company window when loading a save where the local company is not the first company.
* Fix false positive desync log messages for powered free wagon chains.
* Fix issues which could result in a multiplayer desync in some circumstances.
* Add the estimated max speed when full to the train template windows.
* Add NewGRF feature: extra station name strings.
* No longer mark the new train purchase window as experimental.

### v0.42.2 (2021-07-09)
* Further fix for incorrect display of vehicle capacity and cargo, and/or crashes in the new train purchase window.
* Mark the new train purchase window as experimental.

### v0.42.1 (2021-07-09)
* Fix crashes which could occur when using the new train purchase window.
* Fix incorrect display of vehicle capacity and cargo in the new train purchase window.
* Fix not being able to sort locomotives by tractive effort in the new train purchase window.
* Show unowned roads in viewport map owner mode as black (same as town roads), instead of not showing them.

### v0.42.0 (2021-07-04)
* Fix crash when removing a company (e.g. due to bankrupcty or the stop_ai command).
* Fix crash when a network server sends a large multiplayer desync log to a desyncing network client.
* Fix crash when clearing a tunnel where only the near end is reserved with realistic braking.
* Fix crash when autoreplacing vehicle with no orders, when refits are not compatible.
* Fix crash which could occur when logging debug messages to the network admin socket.
* Fix incorrect infrastructure accounting when moving a signalled tunnel/bridge to another company with a different signal spacing setting, causing multiplayer desyncs.
* Fix founding towns inside the catchment on an existing station not associating the town with the station catchment, causing multiplayer desyncs.
* Fix house placing in the scenario editor picking the wrong town when placing houses outside towns is enabled.
* Fix news window viewports not updating vehicle images.
* Fix changing the font zoom level not updating the height of window widgets containing text.
* Fix the status bar time/date section being truncated with large font sizes, and when changing time/date settings.
* Fix owner legend colours when the company starting colour setting is used.
* Fix speed unit conversions in the routing restrictions window.
* Viewport map mode:
  * Fix rendering of sloped tile, which could cause misalignment of tunnels with the entrance tiles.
  * Fix display of high freeform edges at the north edges.
* Map generation:
  * Add public roads (road network automatically built between towns) at map generation and in the scenario editor.
  * Add generation of wide rivers.
  * Allow lakes to be disabled.
  * Adjust lake generation to be closer to the specified lake size.
  * Add setting for a max height level for towns.
* Trees:
  * Add a new tree placement mode with improved distribution.
  * Increase maximum width of artic tree range around snow line setting.
* Add feature where trains adjust their speed to match the train in front to avoid stop-start behaviour.
* Add a new train purchase window, where locomotive and wagons are in separate lists.
* Add a waiting cargo history graph to stations.
* Add feature to create a new auto-named group when dragging and dropping a vehicle onto the new group button.
* Add information about train full and empty loads and achievable speeds to the train info window.
* Add setting for whether to confirm before demolishing industries and/or rail stations.
* Add setting to sort tracks by category and speed.
* Add mode to the cargo payment graph to show payment based on average transit speed.
* Add a tooltip to show station rating details (controlled by a setting).
* Add topography and industries screenshot types.
* Add a setting to turn off road vehicle slowdown in curves.
* Add a setting for whether to pathfind up to back of a one-way path signal.
* Disable town noise limits in indifferent town tolerance mode.
* Set a maximum size for the left part of the build rail station window.
* Use a lower resort interval in vehicle list windows when sorting vehicles by timetable delay.
* Open the routing restriction window when ctrl-clicking any signal (except programmable pre-signals).
* Settings window: Move the day length factor setting to the environment section.
* Allow threaded saves in network server mode.
* Add Korean translations by TELK.
* Trunk base remains at commit 8fa53f543a5929bdbb12c8776ae9577594f9eba7, with some further commits picked up to ef25afd55ab868a4322d0c241b5c4898966ac919.

### v0.41.3 (2021-06-07)
* Fix crash which could occur when a train reaches a disallowed 90° turn.
* Fix crash which could occur on Linux/SDL2 when a text entry widget is focused without a window being focused.
* Fix vehicle sprites not being updated when instantaneously moving a viewport to a non-overlapping position.
* Fix multiplayer network clients failing to join when no client name was set in the network server config.
* Routing restrictions:
  * Fix PBS entry signal conditional with signalled tunnel/bridges.
  * Add action to disable PBS signal back pathfinder penalty.
* Add references to a Homebrew package to the readme.
* Add Korean translations by TELK.
* Trunk base remains at commit 8fa53f543a5929bdbb12c8776ae9577594f9eba7, with some further commits picked up to 4613ababd3fea832d5b11832784768323c39b5a9.

### v0.41.2 (2021-05-21)
* Fix multiplayer servers outputting a corrupt data stream when saving was faster than the connection speed.
* Fix crash when route step UI sprites unintentionally overwritten by a NewGRF.
* Allow moving between drive through train depot ends when the current exit is blocked.
* Add engine class routing restriction conditional.
* Performance:
  * Fix performance issues with deep vehicle group hierarchies.
  * Improve performance when using NewGRFs with large/complex graphics chains.
  * Reduce performance cost of updating vehicles which are not visible on screen.
  * Remove "Disable vehicle image update" setting.
* Add features to the NewGRF debug window (in particular for vehicles).
* Fix incorrect logging of game save failures.
* Improve logging of network activity.
* Fix multiplayer version mismatch issues when compiling with CMake 3.11 or earlier.
* Fix compilation issue on some platforms.
* Trunk base remains at commit 8fa53f543a5929bdbb12c8776ae9577594f9eba7, with some further commits picked up to 5c01f9ea525616b432968df845a90da1d888631f.

### v0.41.1 (2021-05-08)
* Fix crash which could occur due to houses having the wrong tile layout when loading old savegames where the NewGRFs had more overriding house types than the previous lower house ID limit.
* Fix crash when removing airport with order backup, when the hangar window is open.
* Fix crash when using the 32bpp-sse2 blitter with tree-shading.
* Fix scheduled dispatch initialising with incorrect values when the date times the daylength was too large.
* Fix timetable hours/minutes window dialog window setting incorrect values when the date times the daylength was too large.
* Fix button states for other company vehicles and some tooltip texts in the scheduled dispatch window.
* Fix date cheat/scenario load not adjusting vehicle date of last service.
* Fix crash in debug window parent button for non-GRF industries.
* Fix debug window persistent storage display showing the last non-zero stored item as zero if it is the last in a group of 4.
* Add cheat: town local authority ratings fixed as Outstanding.
* Disallow converting town-owned roads to types with the no houses flag.
* Allow moving between drive through train depot ends when exit of the current depot is blocked.
* Realistic braking:
  * Add NewGRF railtype property to disable realistic braking physics for trains of that railtype.
  * Disable realistic braking for TELE, PIPE, and WIRE railtypes by default.
* Console tab completion now also includes command aliases.
* Re-write the readme document.
* Add Korean translations by TELK.
* Bump trunk base from commit 3e0a16c027a42c84678b723540532d1f89fc4fbc to commit 8fa53f543a5929bdbb12c8776ae9577594f9eba7, with some further commits picked up to 8c3fa2a3bf079424529a49b58f0466e4285d5874.

### v0.41.0 (2021-04-14)
* Realistic braking:
  * Fix crash which could occur when the ignore signals button is used to send a train the wrong way onto a signalled tunnel/bridge.
  * Fix crash or misrouting which could occur when a train which ignores signals is used to partially remove the reservation of another train,
    and the track layout is modified to remove the endpoint of the train's original reservation, or an unreserved diverging junction is unexpectedly encountered.
  * Adjust realistic braking physics to fix discrepancies between realistic braking and realistic acceleration.
  * Refresh train lookahead when starting train from stationary.
  * Reduce sensitivity of train brakes overheated breakdown.
  * Fix train brakes overheated breakdown not triggering under some conditions where it should.
* Tooltips:
  * Fix tooltip flickering when dragging outside window.
  * Fix viewport drag tooltips not being removed when dragging over other windows.
  * Fix old polyrail tooltips being left on screen.
* Fix crash when a path to directory is passed as a config file name.
* Fix articulated train units having all of their total weight allocated to the first articulated part, causing issues with slopes for realistic acceleration and braking.
* Fix building objects or trees on coast/shore tiles and then removing them preventing the tile being flooded afterwards in some circumstances.
* Fix network clients which fail to connect being left in the clients list of other connected clients in some circumstances.
* Fix desync which could occur when using drive-through train depots in some circumstances.
* Fix false positive desync warning messages for train cached deceleration values.
* Fix false positive desync warning messages when loading very old savegames.
* Fix setting console command displaying wrong min/max values with some settings.
* Fix map generator creating excessively square lakes, create more natural-looking shapes instead.
* Add cheat to fix station ratings at 100%.
* Add settings to customise the size of city zones separately from town zones.
* Increase the limit of NewGRF house IDs in a single game from 512 to 1024.
* Change numbering of zones in the house picker window to match the town zone settings and the NewGRF specification.
* Change setting default for "Enable showing vehicle routes in the viewport" to on.
* Enable hardware acceleration/OpenGL renderer.
* Add Korean translations by TELK.
* Bump trunk base from commit f70aa8fabe5eabb39a62cc50a3a27ec1c2434ded to commit 3e0a16c027a42c84678b723540532d1f89fc4fbc.

### v0.40.5 (2021-03-29)
* Fix through load crash when the rearmost unit of a train is longer than the whole platform and has no cargo capacity.
* Realistic braking:
  * Fix crash which could occur when a command caused multiple reserved signals to be unreserved.
  * Fix unnecessary braking when leaving station with order without non-stop flag.
  * Fix building over existing stations bypassing realistic braking moving train restrictions.
  * Fix train overshot station advice message being shown to all companies.
  * Improve braking behaviour when descending slopes.
  * Increase signal sighting distance (how close the train needs to get to a signal before the driver can "see" it).
* Map generation:
  * Allow configuring the height at which rainforests start in the sub-tropic climate.
  * Allow configuring the size of rocky patches and how the size of rocky patches increases with height.
  * Add "very many" and "extremely many" modes for the number of rivers to generate.
  * Add settings to control river and lake generation parameters, and how rivers interact with deserts in the sub-tropic climate.
* Fix flickering of polyrail measurement tooltip.
* Fix routing restriction train is loading status condition incorrectly evaluating as false when predicting future orders.
* Add settings to customise the size of town zones.
* Allow clicking the money text in the cheats window to enter a quantity.
* Allow shift-clicking on borrow/repay money buttons to enter a quantity.
* Add setting to disable new vehicles being introduced after a given year.
* Add setting to enable non-admin multiplayer clients to rename towns.
* Add timetable lateness/earliness conditional order.
* Add additional YAPF pathfinder penalty when reversing at a waypoint which is already reserved.
* Fix save/load errors which could occur on some GCC versions.
* Fix Windows crash log dialog not appearing for crashes not on the main thread.
* Packaging: Fix various issues with the package metadata for Debian/Ubuntu .deb files.
* Add Korean translations by TELK.

### v0.40.4 (2021-03-07)
* Fix crash when removing/upgrading airport with hangar window open.
* Fix compiling as a dedicated server.
* Fix compiling with Allegro.
* Add screenshot type: whole map at current zoom level.
* Add empty hotkey for the industry chains window.
* Allow following vehicles at all non-map zoom levels.
* Add Korean translations by TELK.
* Bump trunk base from commit c656633bea39d2002330eddee54522c8db542785 to commit f70aa8fabe5eabb39a62cc50a3a27ec1c2434ded.

### v0.40.3 (2021-03-02)
* Fix crashes with trains with no valid orders when using the realistic braking model.
* Fix trains with no orders not stopping at stations when using the realistic braking model.
* Fix trains passing signals when using both the original acceleration and original braking models.
* Fix various issues with ship collision avoidance which could result in a group of ships being routed in a circle.
* Fix for crash when exiting whilst NewGRF scan still in progress.
* Add support for zstd savegame compression for autosaves and network joins.
* Add setting for shading trees on slopes in viewports (default on).
* Improve visibility of slopes under trees in viewport map vegetation mode.
* Improve performance of water flooding checks.
* Improve performance of snow line checks in the arctic climate.
* Improve performance of drawing rail catenary.
* Fix compilation on ARM Windows with MSVC.
* MacOS: Change bundle identifier.
* Add Korean translations by TELK.
* Bump trunk base from commit 069fb5425302edc93a77ca54b3665a7102747f5a to commit c656633bea39d2002330eddee54522c8db542785.

### v0.40.2 (2021-02-17)
* Fix upgrading road bridge with opposite road/tram type producing broken bridge ramps where required road/tram pieces are missing.
* Fix crash if AI attempts to set order flags of invalid vehicle's order and the no non-stop orders setting is enabled.
* Fix case where reservations could become detached from trains when a restricted signal returns inconsistent reserve through results in the realistic braking model.
* Fix trains slowing down too much when stopping at stations in the original braking model.
* Fix train speed not being set to 0 for timetabled wait at waypoint orders.
* Fix departure boards not handling unconditional order jumps.
* Fix viewport order lines not handling unconditional jump orders.
* Fix autorenew failure advice due to bad refit being shown to all companies.
* Add conditional order for current time/date.
* Add release slot train order.
* Add "indifferent" mode to the town council attitude to area restructuring setting.
* Show warning icon in order window if there are timetable warnings.
* If realistic braking can't be enabled, show an extra viewport on the signal preventing enabling.
* Routing restrictions:
  * Add PBS end signal conditional for use with realistic braking.
  * Add reserved tiles ahead conditional for use with realistic braking.
  * Change PBS entry signal conditional to be in the advanced category in the UI.
* Fix building on Apple MacOS 10.12.

### v0.40.1 (2021-02-05)
* Fix crash when removing signals from bridge or tunnel.
* Fix left mouse button scrolling in viewport map mode.
* Fix clicking trains in slots window, when a slot is selected.
* Fix timetable crash which could occur when all rail tiles are removed from the station of a train order.
* Realistic braking:
  * Fix crash when downgrading road bridge when realistic braking enabled.
  * Fix crash when removing tunnel which is not currently reserved when realistic braking enabled.
  * Fix crash which could occur when a track edit command causes the reservation of a stationary train to be freed.
  * Fix maglevs having braking parameters calculated incorrectly, and braking excessively slowly.
  * Fix signal dragging placing the final signal too early when obstructed by a train reservation.
  * Fix curve speed limit prediction being too pessimistic in some circumstances.
  * Give monorail and maglev higher limits for realistic braking deceleration.
* Fix players being charged to cycle tunnel/bridge signal type with CTRL, when it should be free.

### v0.40.0 (2021-02-02)
* Fix crash in auto-separation when all orders removed.
* Fix crash when removing rail depot or road when debug window open on tile.
* Signals on bridges/tunnels:
  * Fix handling of bridge signals when reversing inside.
  * Fix reversing behind waypoint/signal when front is inside a bridge/tunnel.
  * Fix bridges not being redrawn after all signals reset when cleared.
  * Fix reservation not being cleared from far end of signalled tunnel when removing.
  * Fix handling of entrance availability when reversing inside bidrectionally signalled bridges/tunnels.
  * Fix train crash which could occur when reversing trains on both sides of a bidirectional bridge/tunnel entrance tile.
  * Set tunnel/bridge PBS exit to red when leaving.
* Template-based train replacement:
  * Add support for flipping engine/wagon directions.
  * No longer use idle vehicles in depots by default.
  * Use idle vehicles in depot no longer uses vehicles which have orders/shared orders, or are in a group.
  * Fix vehicle window not being closed when acquiring idle vehicle for replacement.
* Fix false positive desync warning messages for vehicle cached weight/length.
* Add feature: realistic train braking.
* Add setting for dates over which inflation is applied.
* Add client setting for vehicle naming scheme.
* Reduce clipping and graphical issues with NewGRF vehicle sets where the sprite bounds vary with overall curvature.
* Show if train breakdown is due to collision with road vehicle, even if improved breakdowns is disabled.
* Print warning instead of asserting for invalid NewGRF string IDs.
* Add Korean translations by TELK.
* Bump trunk base from commit b7851e51adf0fb0d39ed34a579cf6fe68d8949be to commit 069fb5425302edc93a77ca54b3665a7102747f5a.

### v0.39.2 (2020-12-29)
* Fix crash which could occur when loading older scenarios or savegames which do not already have a company.
* Fix crash which could occur when using the restart command after opening the save/load window.
* Fix crash which could occur when landing a helicopter at 180° rotated intercontinental airport.
* Fix aircraft landing at a 180° rotated intercontinental airport taxiing at the wrong height if the northernmost missing tile is at a different height.
* Fix road/tram type conversion when loading JokerPP v1.27 savegames.
* Fix rendering artefacts in colour news window viewports.
* Fix viewport map mode using the wrong colours when using extra-zoomed-in ground tile GRFs.
* Fix decimal settings not permitting typing a '-' character.
* Fix multiplayer clients printing spurious warning messages to the console.
* Fix cloning a vehicle with a name ending in a very large number resulting in the new vehicle having the wrong number in its name.
* Add setting to scale primary industry cargo production.
* Template-based train replacement:
  * Fix editing a template not refitting the first engine and any articulated or rear engine parts.
  * Fix templates using the wrong colouring scheme in various circumstances.
* Add console commands for conditional execution from game date.
* Allow AI/GS developers to reload GSs.
* Fix CMake looking for fctix on Apple patforms.
* Bump trunk base from commit 0a9aed052295a98f1c1438cf1fa05b9a7e6b6607 to commit b7851e51adf0fb0d39ed34a579cf6fe68d8949be.

### v0.39.1 (2020-11-28)
* Fix crash which could occur with very large numbers of orders per vehicle.
* Fix scrollbar functionality in schedule dispatch window.
* Fix map-mode viewports not updating when changing company and land colours.
* Fix case where unsuccessfully attempting to build a rail could possibly change the railtype of the existing rail on the tile.
* Routing restrictions:
  * Fix crash which could occur when using the set counter value action in a conditional block.
  * Add current time conditional.
* Prevent various types of multiplayer desync caused by incorrectly implemented NewGRFs.
* Change tunnel/bridge signal simulation spacing to be a company setting.
* Improve performance of animated tiles (industries, stations, objects, houses).
* Add Korean translations by TELK.
* Bump trunk base from commit cf29d23ba4ca2b9e6b638720e186bf33e11d5a0f to commit 0a9aed052295a98f1c1438cf1fa05b9a7e6b6607.

### v0.39.0 (2020-11-08)
* Fix crash when mousing over the vehicle UI order bar for a stopped vehicle which is heading for sale at a depot.
* Fix crash which could occur when re-drawing a window element which is entirely off-screen.
* Fix crash which could occur with very large numbers of orders per vehicle.
* Fix crash which could occur when dragging after cancelling dragging vehicles/groups in the vehicle list window.
* Fix crash when scrolling a non-map mode extra viewport, when a shaded map mode extra viewport is present.
* Fix multiplayer desync which could occur after programming a new programmable pre-signal.
* Fix clearing timetable travel time clearing wait time instead.
* Fix smallmap not refreshing when paused.
* Fix changing tree transparency not updating vegetation map mode viewports.
* Road vehicles/one-way roads:
  * Allow overtaking inside (non-custom) bridges/tunnels.
  * Allow drive-through road stops to be one-way.
  * Road segments with no junctions between one-way road tiles in the same direction, are now also one-way.
  * T-junctions on the driving side with one-way road tiles either side, are now also one-way.
  * Road vehicles on one-way roads may now stay in the overtaking lane as long as necessary, and have fewer constraints to start overtaking.
  * Various other improvements to overtaking.
  * Add zoning mode to show one-way roads.
* Plans:
  * Fix selected plan not being unselected when closing window.
  * Allow changing the colour of plans.
* Add features to reverse the order of an order list, and to append the reverse of an order list.
* Increase the maximum allowed value for cargo waiting amount conditional orders.
* No longer charge vehicle running costs when waiting in depot due to timetable.
* Upgrading an airport to an identical configuration now returns an error instead of charging the full amount again.
* AI/GS script: Add date methods for getting time in minutes.
* Add setting to disable continuously updating NewGRF vehicle image.
* Improve performance of trains and road vehicles with a continuously updating NewGRF vehicle image.
* Add Korean translations by TELK.
* Bump trunk base from commit 313141d2f1218e487a546514831b91d794c20fde to commit cf29d23ba4ca2b9e6b638720e186bf33e11d5a0f.

### v0.38.1 (2020-10-21)
* Orders:
  * Fix crash when saving or joining network server games with order backups.
  * Fix conditional order jumps to orders at positions greater than 256.
  * Fix inserting or modifying orders at positions greater than 4096.
  * Fix currently selected order being de-selected when inserting/moving orders.
* Fix crash when skipping suppressed unreached implicit orders when beginning loading at a station.
* Fix NewGRF load error when using custom rail/road/tram type properties.
* Add display setting for income/cost text effects.

### v0.38.0 (2020-10-16)
* Fix crash when placing object, when object class has no available objects.
* Template-based train replacement:
  * Fix various crashes which could occur in multiplayer when multiple template move/delete operations are in flight at the same time.
  * Fix crash which could occur when replacing template.
  * Fix appending to template not refreshing window in multiplayer.
* Fix crash in download base graphics bootstrap mode.
* Fix changing smallmap legends not updating viewport maps.
* Fix PBS handling of mixed rail type layouts which could cause train crashes, when using NewGRFs which don't correctly define rail type compatibilities.
* Signals on bridges/tunnels:
  * Fix reversing train inside signalled bridge/tunnel not unreserving exit.
  * Fix PBS detection outwards from PBS bridge/tunnel exit.
* Add drive-through train depot emulation.
* Increase per-vehicle order limit from 254 to 64k.
* Add viewport map mode: transport routes (similar to smallmap transport routes mode).
* Allow converting track type under trains when compatible with the new rail type.
* Add sort by vehicle count to the vehicle purchase window.
* Add company setting for whether to add vehicle to group on copy-clone.
* Plans:
  * Improve performance of plan rendering.
  * Fix adding plan lines in viewport map mode.
  * Fix marking plans visible/invisible not always fully updating the screen.
* NewGRF:
  * Allow rail type GRF to provide custom signal sprites for restricted signals and programmable pre-signals.
  * Add bridge property to prevent towns or AI/GS building bridge type.
  * Add road/tram type properties: not available to AI/GS, and may not be modified by towns.
* Increase number of settings which can be changed in multiplayer.
* Console:
  * Add network server commands to get/set company password hashes.
  * Allow sending an empty password to drop settings_access.
* Make smallmap refresh period variable with map mode/zoom and pause state.
* Various performance improvements for viewport map mode and some windows.
* Improve scheduling of cargodist link graph updates.
* Reduce screen-tearing on Linux/Unix (SDL2).
* Fix build/compilation issue on MacOS.
* Add Korean translations by TELK.
* Bump trunk base from commit 53a3d940b15ca2e769b4db19079b3b6913c48647 to commit 313141d2f1218e487a546514831b91d794c20fde.

### v0.37.0 (2020-09-22)
* Fix crash when upgrading dual road/tram bridge, when the other road/tram type does not extend across the bridge, but is present on the upgrade tile.
* Fix crashes or other erros which could occur after the NewGRF error window is shown after generating a new map.
* Routing restrictions:
  * Fix crash which could occur when using reverse behind signal.
  * Allow referencing competitor infrastructure where allowed by sharing.
  * Add train counters.
* Programmable pre-signals:
  * UI improvements.
  * Add train slot and counter conditionals.
* Template-based train replacement:
  * Fix being able to open template replacement window more than once.
  * Fix replacement flags being reset when editing template.
  * Fix group add/remove/rename not updating template replacement GUI in multiplayer.
  * Fix no error message when attaching new template vehicle fails.
  * Do not keep remaining vehicles by default.
* Fix bulk land purchasing removing structures and water.
* Fix excessively long time periods between updates for small link graph networks.
* Fix town label colour not being updated when switching companies.
* Fix connecting link graph overlay links not being redrawn when when moving station sign.
* Only show ship is lost messages if lost for a significant time.
* Allow building objects by area (1x1 objects only).
* Add rate limit for object construction.
* Add setting for default road/tram types, to match default rail type setting.
* Add conditional order which tests counter value.
* AI/GS:
  * Allow changing ops limit and memory limit settings in game.
  * Allow AI/GS developers to change game script in-game.
* Hotkeys:
  * Allow using the hash (#) key as a hotkey on Linux/SDL.
  * Add empty hotkeys for message history, template replacement window, slots window, counters window.
* On load, use previous local company or the first usable company, instead of always using the first company slot.
* NewGRF: Fix industry probability at map generation was scaled differently when set via property or callback.
* MacOS: Fix font support (builds did not include Freetype).
* Bump trunk base from commit 9340fe9c7ceca3349df171770480683097f0e436 to commit 53a3d940b15ca2e769b4db19079b3b6913c48647.

### v0.36.0 (2020-08-30)
* Fix incorrect infrastructure totals which could cause multiplayer desyncs when using the road convert tool on bay road stops.
* Fix vehicle window mouse over colour when both stopped and waiting/stuck.
* Fix order lookahead changing percent of times conditional order state.
* Hide screenshot window when taking a normal screesnhot.
* Use two columns in the cargo type orders window when there are more than 32 cargoes.
* Improve performance of cargodist when using refit to any cargo orders.
* Add leave early if any/all cargoes fully loaded timetable modes.
* Template-based train replacement: Show empty and full template train weights, and weight ratios if enabled.
* Routing restrictions: Add feature to control news reports about stuck trains.
* Add news setting for trains waiting due to routing restrictions.
* Add setting for alternative linkgraph overlay colour schemes.
* Add basic tab-completion to the console window.
* Scenario editor:
  * Add setting to enable multiple churches/stadiums in scenario editor.
  * Add settings to ignore date/zone/GRF when placing houses in scenario editor.
* Build:
  * Fix languages not always being recompiled when updated.
  * Fix source being incorrectly detected as modified when not using git on some locales.
* Add Korean translations by TELK.
* Bump trunk base from commit 00eccbe298ad7f7d656e121ce58c2a6326dabe2f to commit 9340fe9c7ceca3349df171770480683097f0e436.

### v0.35.1 (2020-07-20)
* Fix crash which could occur when selling/replacing a vehicle which is currently displayed in a departure board window.
* Fix colour of vehicle destination text when mousing over in vehicle window.
* Fix missing window/taskbar icon when using SDL/SDL2.
* Fix compilation on CMake versions prior to 3.12.
* Add hotkey to show link graph legend window (defaults to: Y).
* Unix: Add wrapper build scripts to invoke CMake/make.
* Add Korean translations by TELK.
* Add Czech translations by Lurker23.

### v0.35.0 (2020-07-12)
* Fix crash on maps larger than 64k in either axis.
* Fix crash which could occur when displaying vehicles with no orders in the departure board window.
* Fix crash when template replacing a train with a free wagon chain.
* Fix incorrect calculation of town growth rate.
* Fix incorrect scheduled dispatch timetable dates after using date change cheat.
* Fix incorrect display date on load for savegame versions < 31.
* Fix incorrect news window type and autoreplace behaviour for vehicle too heavy advice messages.
* Fix general transparency hotkey not updating vehicles in tunnels.
* Fix crash which could occur on WINE on systems with more than 8 network interfaces.
* Fix overflowing the maximum possible amount of money being done incorrectly.
* Fix performance issues which could occur when dragging windows.
* Fix station catchment overlay not always being cleared when distant join window is closed.
* Various improvements to timetable separation and automation.
* Improve road vehicle pathfinding when multiple vehicles are simultaneously heading to a station with multiple bay/stop entrances.
* Add setting to scale station cargo capacity and rating tolerance by size.
* Add setting to disable vehicle expiry after a given year.
* Add setting to control road vehicle re-routing on road layout changes.
* Add a "none" option to the tree growth rate setting.
* Time display settings are now stored in the savegame, with an option for client-local override.
* Also show vehicle destination on mouseover when waiting for PBS or routing restriction.
* Fix incorrect goal time left display when using BeeRewardGS and day length greater than 1.
* Prevent Mop Generic NRT Vehicles GRF from causing multiplayer desyncs.
* Bump trunk base from commit eeed3a7613d375f66781f53b42e03729a4ca1c33 to commit 00eccbe298ad7f7d656e121ce58c2a6326dabe2f.

### v0.34.4 (2020-06-05)
* Fix crash which could occur when pathfinding over railtypes which prohibit 90° turns.
* Fix vehicle refit when used with per-cargo no-load orders.
* Add support for fences and bare land to rail custom bridge heads.
* Bump trunk base from commit 83cd040c61cf6ce966e78cc496c058d42977b387 to commit eeed3a7613d375f66781f53b42e03729a4ca1c33.

### v0.34.3 (2020-05-14)
* Fix crash which could occur when using the reverse behind signal feature.
* Fix text entry using modifier keys when using Fcitx on Linux/SDL2.
* Continue waiting at rail waypoint if the next order is a wait order for the same waypoint.
* Conditional orders:
  * Fix comparison operator not being reset when switching variable to load percentage or waiting cargo amount.
  * Add mode to waiting cargo amount variable to check waiting station cargo via next node.
  * Add slot acquire modes to train in slot conditional.
  * Improve cargo dist link refresher handling of complex conditional orders.
* Fix compilation on MSVC.
* Various changes to improve thread safety/data races.
* Bump trunk base from commit 1f1345de098294a4744981d0043512569a35102a to commit 83cd040c61cf6ce966e78cc496c058d42977b387.

### v0.34.2 (2020-05-01)
* Fix crash which could occur when scrolling the viewport.
* Fix crash which could occur when using the reverse behind signal feature.
* Fix a source of multiplayer desyncs caused by the build and refit vehicle feature.
* Fix removing a track piece from a rail custom bridge head to create two parallel tracks assigning the wrong track type to the non-bridge track.
* Fix cargo type load orders which contain both load if available and full load loading types.
* Fix timetable handling of wait at depot or waypoint orders in the departures window.
* Fix graphical rendering issues (clipping/flickering) in some circumstances.
* Fix too short length limit of waiting cargo amount conditional order text input.
* Fix `screenshot minimap <name>` console command ignoring the name parameter.
* Fix GameScripts being able to consume all available CPU time by repeatedly attempting to found a town.
* Cheats:
  * Add support for server admin use of money, magic bulldozer, tunnels and jet crashes cheats in multiplayer.
  * Add setting to allow non server admins to use the money cheat in multiplayer.
  * Add cheats to set inflation income and cost factors.
* Ctrl-click up/down in NewGRF window to move to top or bottom.
* Minor performance improvements.
* Add Korean translations by TELK.
* Bump trunk base from commit 9339e4dcad8aa74ff1b2723ea63a2e31c23f5d44 to commit 1f1345de098294a4744981d0043512569a35102a.

### v0.34.1 (2020-04-13)
* Fix crash which could occur at startup for some combinations of resolution and zoom settings.
* Fix crash which could occur on WINE on systems with more than two network interfaces.
* Scheduled dispatch:
  * Fix double dispatch request when timetable is not started.
  * Fix lateness not being updated when timetabled waiting time at dispatch point changes.
  * Fix dispatch order timetabled waiting time being taken as zero in some circumstances.
* Various changes to improve thread safety/data races.
* Bump trunk base from commit b50d77b831c60f9f162a6f1d2bc9ca19e702784e to commit 9339e4dcad8aa74ff1b2723ea63a2e31c23f5d44.

### v0.34.0 (2020-04-07)
* Fix crash when attempting to draw zero-size or invalid sprite.
* Fix crash which could occur when scrolling the viewport on some platforms.
* Fix crash which could occur when renaming a vehicle group or engine, when the list window is open and sorted by name, on some platforms.
* Fix crash which could occur when changing company colours when invalid NewGRF objects are present.
* Add support for allowing/disallowing supply to a station, per cargo, by ctrl-clicking the station cargo rating.
* Open train vehicle details window on total cargo tab if shift pressed, instead of ctrl.
* Increase margin between right-hand columns in depatures window.
* Fix window/viewport rendering regressions from v0.34-rc1.
* Bump trunk base from commit 71913607540088819b60f12b765504ab7dfe7a64 to commit b50d77b831c60f9f162a6f1d2bc9ca19e702784e.

### v0.34-rc1 (2020-03-11)
* Fix crash when using house pick/place tool with NewGRF houses.
* Fix crash which could occur when using a high town cargo generation factor.
* Fix crash which could occur when re-arranging a train displayed in a departure board window.
* Fix text rendering issue with scheduled dispatch tag in timetable/order list.
* Various viewport rendering performance improvements, especially at higher zoom levels.
* Minor performance improvements to vehicle collision detection.
* Bump trunk base from commit 75031c9693ee0525c75e8e02ead345b1f8264735 to commit 71913607540088819b60f12b765504ab7dfe7a64.

### v0.33.2 (2020-02-21)
* Fix crash on 32-bit platforms.
* Fix crash which could occur when moving new cargo to nearby stations.
* Bump trunk base from commit 2b6df2544fd2896e09eac24598721e5259ff791f to commit 75031c9693ee0525c75e8e02ead345b1f8264735.

### v0.33.1 (2020-02-13)
* Template-based train replacement:
  * Fix template replacement refits having 0 cost.
  * Fix drawing artefacts when resizing replacement window.
  * Send train to depot when replacement due but servicing is disabled.
  * Template replacements now also apply to child groups.
* Link graph:
  * Fix demand allocation in partitioned graphs.
  * Fix slightly uneven demand allocation in asymmetric (equal) mode.
* Scheduled dispatch:
  * Various improvements to the user interface.
  * Fix times shown in timetable window when using scheduled dispatch.
  * Fix handling of wait time associated with scheduled dispatch order.
  * No longer require order list to be fully timetabled.
  * Invalid departure slots are now ignored.
* Fix incorrect town noise level when a town had multiple airports.
* Fix incorrect reservation when a signal is removed from under a train when the front is in a signalled tunnel/bridge.
* Add conditional orders for cargo load percentage and waiting cargo amount.
* Adjust timetable automation to bias wait time adjustments in positive direction.
* Various viewport rendering performance improvements.
* Bump trunk base from commit 8b0e4bb10170d8eeb882f0fcc0ad58e80d751027 to commit 2b6df2544fd2896e09eac24598721e5259ff791f.

### v0.33.0 (2020-01-10)
* Fix crash when post road-works cleanup removes all road pieces.
* Fix crash when checking for train reverse on custom bridge heads with YAPF.
* Template-based train replacement:
  * Fix incorrect train not buildable warning with articulated units.
  * Fix refitting of virtual trains costing actual money.
* Fix set timetabled wait time for all orders command setting wait times for waypoint orders.
* Add support for loading JokerPP v1.19 - v1.27 savegames, subject to caveats.
* Add support for loading ChillPP v8 and v14.7 savegames, subject to caveats.
* Improve performance of departures window.
* Fix compilation on MSVC.
* Add Korean translations by TELK.
* Bump trunk base from commit 35dc377a58c90abb67304a0c557449b6db3c0d3f to commit 8b0e4bb10170d8eeb882f0fcc0ad58e80d751027.

### v0.32.4 (2019-12-13)
* Fix incorrect company infrastructure totals and multiplayer desyncs when removing tram road stops.
* Fix vehicle autoreplace AI event when autoreplacing trains.
* Add patch: show the name of the NewGRF in the build vehicle window.
* Routing restrictions: Add speed restriction feature.
* Rename programmable signals to programmable pre-signals.
* Bump trunk base from commit ef8455f5498cc01bc60eb1c02902c38bbc332a7a to commit 35dc377a58c90abb67304a0c557449b6db3c0d3f.

### v0.32.3 (2019-11-20)
* Fix loading of savegames which use LZO compression.
* Fix crash which could occur when attempting to load an unreadable/invalid savegame.
* Fix crash which could occur when attempting to stop an aircraft which is outside the map.
* Fix aircraft possibly being routed to the wrong hangar when using the send to hangar button on an aircraft which is outside the map.
* Fix some trains having zero power on load for some savegame/GRF configurations.
* Fix the give money input textbox not correctly handling money quanitities greater than 2.1 billion in local currency units.
* Template-based train replacement: Fix various scaling and alignment issues in the template window.
* Routing restrictions: Add support for signalled tunnel/bridges to PBS entry signal conditional.
* Stations under bridges:
  * Fix use of station GRF defined bridge pillar disallowed flags.
  * Always allow buoys under bridges.
  * Add seperate settings for allowing NewGRF rail, road and dock stations under bridges.
* Fix compilation on MSVC (32 bit).
* Add Korean translations by TELK.
* Bump trunk base from commit d5a9bd404a3ca90a18abeeaaaabdbf5185437ba7 to commit ef8455f5498cc01bc60eb1c02902c38bbc332a7a.

### v0.32.2 (2019-11-06)
* Fix crash which could occur after removing oil rig.
* Fix crash which could occur when scanning NewGRF files.
* MacOS: Fix crash issues on MacOS 10.15 Catalina.
* SDL2 video driver (Linux):
  * Fix home and end keys in text editing contexts.
  * Fix page down key.
  * Fix up/down and function keys printing '?' in text editing contexts.
* Fix multiplayer issues which could occur on networks with a reduced MTU.
* Bump trunk base from commit e2e112baaabaaeec1f04f13c3759f24c06b42cf2 to commit d5a9bd404a3ca90a18abeeaaaabdbf5185437ba7.

### v0.32.1 (2019-10-20)
* Fix "undefined string" appearing in 3rd line of error message window.
* Fix width of bottom row of template-based train replacement create/edit template window.
* Fix link graph link usage statistics (used for the link graph overlay colours) becoming increasingly inaccurate on large networks over time.
* SDL2 video driver (Linux):
  * Fix handling of shift key in text-editing mode.
  * Fix up/down keys in console window.
  * Fix passing keypresses to Fcitx which are unknown to SDL.
  * Fix attempting to use SDL2 versions prior to 2.0.5, which do not compile.
  * Automatically detect and use SDL1 if SDL2 is not present and usable.
* Fix compilation on MSVC and on Mac OSX.
* Add Korean translations by TELK.

### v0.32.0 (2019-10-12)
* Fix crash when disabling infrastructure sharing with vehicles with go to nearest depot orders.
* Fix order backup not saving/restoring timetable automation, separation and scheduled dispatch states.
* Fix modifier key window not always updating.
* Routing restrictions: Add load percentage conditional.
* Add support for IMEs on Linux/SDL2 (SDL2-supported IMEs and Fcitx).
* Various performance improvements.
* Bump trunk base from commit 1f418555a13b63379e4ce52ec96cbed6e04dca7d to commit e2e112baaabaaeec1f04f13c3759f24c06b42cf2.

### v0.32-rc5 (2019-09-22)
* Fix crash when using road convert tool on road station tiles.
* Fix crash when clicking on a station from the order window where the station sign tile is not a station tile.
* Fix road/tram catenary not being drawn on custom bridge heads.
* Fix town growth not correctly following custom bridge heads.
* Fix line heights of fonts on Windows in some circumstances.

### v0.32-rc4 (2019-09-18)
* Fix crash when removing docking tile adjacent to an industry without an associated station.
* Fix crash which could occur after copying orders over an order list currently containing conditional orders.
* Fix incorrect company infrastructure totals and multiplayer desyncs when using the road/tram type conversion tool on road/tram depots.
* Fix mass order destination change not working with load/unload by cargo orders.
* Fix mass order destination change not preserving locked waiting times.
* Fix crash when using -q switch on a savegame which could not be loaded.
* Add modifier key window for toggling shift/ctrl key states using mouse.
* Bump trunk base from commit dabccf70b4c02f68ebf51aca807376ca4f2a0e15 to commit 1f418555a13b63379e4ce52ec96cbed6e04dca7d.

### v0.32-rc3 (2019-08-29)
* Include change from v0.31.5

### v0.31.5 (2019-08-29)
* Fix multiplayer desync when when moving newly built wagon

### v0.32-rc2 (2019-08-24)
* Include changes from v0.31.4
* Fix crash when using query tool on non-road bridges.
* Fix road vehicles not being limited by the road type max speed.
* Bump trunk base from commit a52bbb72a8a2cbcbefb0ff91b559f33c34094239 to commit dabccf70b4c02f68ebf51aca807376ca4f2a0e15.

### v0.31.4 (2019-08-24)
* Fix crash when removing signals from tunnel/bridge with trainless reservation.
* Fix various cases where reversing a train inside a signalled tunnel/bridge handled PBS reservations incorrectly.
* Fix error windows being closed when returning to the main menu.
* Add a password mechanism to change network game settings from a network client.
* Change station tile coverage highlight colour to light blue.
* Change network protocol to send server/join and rcon passwords in hashed form instead of in clear text.
* Fix various possible sources of non-determinism which could potentially cause multiplayer desyncs.

### v0.32-rc1 (2019-07-13)
* Include NotRoadTypes (NRT).
* Bump trunk base from commit 21edf67f89c60351d5a0d84625455aa296b6b950 to commit a52bbb72a8a2cbcbefb0ff91b559f33c34094239.

### v0.31.3 (2019-07-13)
* Fix the target order number of conditional order jumps being loaded incorrectly from SpringPP savegames.
* Fix order backups not being restored when using buy and refit.
* Fix rendering error when waypoint sign is moved.
* Fix virtual trains in the template train replacement editing window reserving a unit number.
* Re-add previously removed group collapse and expand all buttons.
* Fix compilation on MSVC.
* Bump trunk base from commit 66cd32a252ee0edab11448b560371878b2189223 to commit 21edf67f89c60351d5a0d84625455aa296b6b950.

### v0.31.2 (2019-06-18)
* Fix through load orders which use full load any cargo, with multi-cargo trains.
* Fix PBS reservations not being shortened or extended when adding or removing signals to bridges/tunnels.
* Fix various issues which could cause multiplayer desyncs.
* Fix incorrect save/load when using compilers other than GCC/clang (e.g. MSVC).

### v0.31.1 (2019-05-28)
* Fix crash when articulated road vehicles overtook other road vehicles on custom bridge heads.
* Fix airports not being deleted on bankruptcy/company deletion when an aircraft from another company was taking off/landing.
* Fix max speed in road vehicle purchase window when using original acceleration model.
* Fix various issues in the company bankruptcy/take-over process.
* Template-based train replacement:
  * Fix state of front engine not being cleared when being replaced and kept in the depot.
  * Fix trace restrict slot ownership not being transfered when replacing the front engine.
  * Add error messages for replacement failure due to wrong depot railtype or owner.
* Add news/advice setting to warn if no depot order in vehicle schedule.
* Enable vehicle list buttons in station window when the list would be non-empty.
* Enable vehicle group management actions on other companies' stations.
* Improve performance of name sorting in town, industry and station list windows.
* Improve performance of server to client map transfer on multiplayer join.
* Fix various possible sources of non-determinism which could potentially cause multiplayer desyncs.
* Emit "crash" log, savegame and screenshot on multiplayer desync.

### v0.31.0 (2019-05-06)
* Fix online content requests which included a large numbers of missing items.
* Fix crash which could be triggered by an AI.
* Add setting to allow articulated road vehicles to overtake other vehicles (default on).
* Allow removing signals from plain rail track when a train is present.
* Open train vehicle details window on total cargo tab if ctrl presed.
* Link graph:
  * Allow overriding distribution mode on a per-cargo basis, in game.
  * Add new distribution modes: asymmetric (equal) and asymmetric (nearest).
* Template-based train replacement:
  * Allow cloning template trains with unavailable vehicles.
  * Show warning on templates which include unavailable vehicles.
* Change default non-global polyrail hotkeys to Y, CTRL-Y.
* Improvements to crash logging (on Unix and Mac).
* Fix potential multiplayer desync.
* Fix false positive warnings in desync debug logging.
* Add further checks to desync debug logging.
* Bump trunk base from commit c0836bccefb7fbc6ebc8c5fa28886602067070f8 to commit 66cd32a252ee0edab11448b560371878b2189223.

### v0.30.3 (2019-04-12)
* Fix crashes on Windows/MinGW caused by race condition at thread initialisation due to incorrect template argument deduction.

### v0.30.2 (2019-04-11)
* Fix crash and/or non-functionality which could occur when using the bootstrap UI to download the base graphics, or when using the content download window.
* Fix crash which could occur when displaying the origin station of cargo in the station window.
* Fix crashes related to caching of viewport station sign positions.
* Fix create group from vehicle list command.
* Fix rail type conversion of dual track tiles when rail type labels differ at load.
* Allow diagonal construction of rivers in the scenario editor.
* Persist the zoning overlay modes in UI setting.
* Bump trunk base from commit 66c60e52bac69b752f1dd7b7c599577fcbfa17a1 to commit c0836bccefb7fbc6ebc8c5fa28886602067070f8.

### v0.30.1 (2019-04-05)
* Fix multiplayer desync when using build and refit (regression in v0.30.0).
* Bump trunk base from commit 24fc25164a7c4efbf78d28ce9a3dbc22d1f45f5f to commit 66c60e52bac69b752f1dd7b7c599577fcbfa17a1.

### v0.30.0 (2019-04-03)
* Fix crash which could occur when attempting to build a rail station partially off the map.
* Fix crash which could occur when disaster vehicles were present.
* Fix mass changing of rail waypoint orders.
* Fix wrong rail type being used in some circumstances for dual rail type tiles.
* Fix enabling/disabling timetable automation for a vehicle in some circumstances.
* Fix viewport hovering and tunnel build viewport length tooltip when hover mode is set to right-click.
* Fix the show town population in label setting not being followed when also showing the rating.
* Fix display of restricted programmable signals which use NewGRF graphics.
* Disallow ordering ordinary road vehicles to tram depots and vice versa.
* Add UI setting for whether to open the new vehicle GUI when share-cloning.
* Add company setting for whether to advance order when cloning/copying/sharing (if current depot is in order list).
* Add client setting for the zoning overlay UI state.
* Remove town builds bridges over rail setting, feature in trunk.
* Add Japanese translations by Qwerty Asd.
* Bump trunk base from commit 690d1dd6a4490821759a6025114e0dc3eb656293 to commit 24fc25164a7c4efbf78d28ce9a3dbc22d1f45f5f.

### v0.29.3 (2019-02-22)
* Fix crash which could occur when disaster vehicles which emit effects were present.
* Fix case where trains were unable to exit signalled bridge/tunnels.
* Fix ships being drawn facing the wrong direction in some circumstances.
* Fix flickering of viewport hover tooltips in fast-forward mode.
* Fix second rail track type not being preserved when upgrading bridges or changing the NewGRF railtype configuration.
* Fix cases where the game blocked instead of pausing when cargo dest link graph jobs lagged.
* Fix ship collision avoidance near docks when dock not directly under station sign.
* Add game setting to allow only non-stop orders for ground vehicles.
* Fix compilation in MSVC.
* Adjust bundle install paths on OSX.
* Bump trunk base from commit 33e3f4916173b4129cbbe60f94dae659a70edb83 to commit 690d1dd6a4490821759a6025114e0dc3eb656293.

### v0.29.2 (2019-02-04)
* Fix order list corruption when drag-moving order.
* Fix trains not reversing in station when the front is on a diagonal rail piece or in a bridge/tunnel.
* Fix loading of bridges from Spring 2013 Patchpack savegames (v2.1.147 and later).
* Bump trunk base from commit fa53abe864a6939dc4dac8a6c61443e486e0eb04 to commit 33e3f4916173b4129cbbe60f94dae659a70edb83.

### v0.29.1 (2019-02-02)
* Fix train disconnecting when reversing at the end of a sloped bridge ramp due to heading the wrong way onto a signalled bridge.
* Fix display of two rail types per tile (horizontal overlay tracks).
* Fix crash or other failures when using more than approximately 230 NewGRFs.
* Fix input and display of hhmm times in timetable and scheduled dispatch GUI, when using large day length or ticks/minute values.
* Add GUI setting for when to ask for confirmation before overwriting an existing savegame file, add unique ID to savegames.
* Performance improvements.
* Bump trunk base from commit 391bc45c41287bf3016e33266b24f30cdbfb5f07 to commit fa53abe864a6939dc4dac8a6c61443e486e0eb04.

### v0.29.0 (2019-01-02)
* Fix crash or other failures when using more than approximately 230 NewGRFs.
* Allow up to two rail types per tile.
* Land area purchasing:
  * Allow purchasing a region of tiles at once, by dragging.
  * Add company rate limit for land purchasing.
  * Add setting to control if and how land purchasing is permitted.
* Routing restrictions: Add advanced feature reverse behind signal.
* Prevent AIs from creating or adding to rail custom bridge heads.
* NewGRF interface: Add Action 5 support for programmable signals graphics.
* Bump trunk base from commit 16a36dffa0ccd7753de0100ee320a4982bb1945c to commit 391bc45c41287bf3016e33266b24f30cdbfb5f07.

### v0.28.0 (2018-11-22)
* Fix trains unnecessarily slowing down when passing waypoints.
* Template-based train replacement: Add option to replace only old vehicles.
* Timetabling:
  * Extend timetable wait/travel times from 16 to 32 bits wide.
  * Fix timetabling of through-load orders.
* Scheduled dispatch: Don't show invalid required vehicle estimate.
* Routing restrictions:
  * Add train is in slot conditional order.
  * Prevent adding train slot state conditional orders to non-train vehicles.
* Zoning:
  * Add modes to show 2x2 and 3x3 town road grids.
  * Fix refreshing of SW edge of station coverage area.
* Stations under bridges:
  * Add NewGRF properties for permitted bridge pillars above station tiles, and bridge pillars present below bridges.
  * Set minimum bridge height clearances and permitted bridge pillars for the default stations.
  * Set present pillars for the default bridges.
* Bump trunk base from commit 59a1614ba0724bf5240b91d8cd2b90ff7eeb286c to commit 16a36dffa0ccd7753de0100ee320a4982bb1945c.

### v0.27.1 (2018-09-20)
* Fix crash when changing timetable leave early flag of current order.
* Add setting to enable flipping direction of all train types in depot.
* Fix build/compilation issue on MacOS.
* Bump trunk base from commit 703e7f8fc78a7032b7a5315092604fb62f471cb8 to commit 59a1614ba0724bf5240b91d8cd2b90ff7eeb286c.

### v0.27.0 (2018-08-29)
* Fix crash when selling a train that's in a routing restriction slot.
* Fix crash and/or reservation errors at the far end when a train exited a signalled bridge/tunnel.
* Fix routing restriction slot window not being refreshed.
* Fix order extra data/flags not always being copied and/or applied to vehicle.
* Fix various issues involving viewport plans in multiplayer due to lack of validation.
* Fix visual glitches rendering multi-part order lines.
* Add a "leave early" order timetable flag.
* Add order mode to lock timetable travel time against autofill/automate changes.
* Add support for timetabled wait times at waypoints.
* Add support for assigning names to viewport plans.
* Show reversing and timetabled wait states in vehicle status bar.
* Show information relevant to sort key in vehicle list windows when sorting.
* Performance improvements.
* Bump trunk base from commit 50d930298dd99d20022c0f4a3bc080487f8afc17 to commit 703e7f8fc78a7032b7a5315092604fb62f471cb8.

### v0.26.2 (2018-08-12)
* Fix crash/incorrect behaviour when propagating signal state changes up to the rear side of bridge/tunnel tiles.
* Fix script/AI construction of rail track and waypoints.
* Fix line height mismatch when selecting items in the timetable window.
* Fix the cost of constructing a depot not including the cost of its foundation.
* NewGRF bridges:
  * Increase the number of bridge types from 13 to 16.
  * Allow NewGRFs to set bridge selection sprites.
* Bump trunk base from commit 5df3a65074295f7a50f3a5a6bab355b6ab28afdc to commit 50d930298dd99d20022c0f4a3bc080487f8afc17.

### v0.26.1 (2018-08-06)
* Fix crash when deleting train from signalled tunnel/bridge (e.g. due to company bankruptcy).
* Fix being able to add signals to bridge with junction custom bridge head at far end.
* Fix being able to build bridges over airports (when using the setting to allow placing stations under bridges).
* Fix incorrect alignment of trains on bridges after entering via a custom bridge head at a 45° angle.
* Fix line height mismatch between columns in timetable window.
* NewGRF stations:
  * Implement variable 0x42 and property 0x1B.
* Bump trunk base from commit bf8d7df7367055dcfad6cc1c21fd9c762ffc2fe4 to commit 5df3a65074295f7a50f3a5a6bab355b6ab28afdc.
  * This fixes being unable to build rail waypoints on the NW-SE axis.

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
* Fix configure script not being able to detect clang on Mac OSX.
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
