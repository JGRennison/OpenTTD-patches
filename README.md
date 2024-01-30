## JGR's Patchpack version 0.57.0

This is a collection of patches applied to [OpenTTD](http://www.openttd.org/)

* * *

OpenTTD is a transport simulation game based upon the popular game Transport
Tycoon Deluxe, written by Chris Sawyer. It attempts to mimic the original
game as closely as possible while extending it with new features.

OpenTTD is licensed under the GNU General Public License version 2.0,
but includes some 3rd party software under different licenses. See the
section "Licensing" below for details,

* * *

See [below](#openttd) for the original OpenTTD readme.

The thread for this patchpack can be found [here](http://www.tt-forums.net/viewtopic.php?f=33&t=73469).

See [jgrpp-changelog.md](jgrpp-changelog.md) for the changelog.

See the [wiki](https://github.com/JGRennison/OpenTTD-patches/wiki) for guides on how to use some of the included features.

See [installation.md](/installation.md) for instructions on how to install.

(Nearly all of the patches which are listed below have been modified, fixed or extended in some way, and so are not the same as the originals which are linked).

#### Railways and Trains

* Drive-through train depots.
* [Template-based train replacement](http://www.tt-forums.net/viewtopic.php?f=33&t=58904).
* [Routing restrictions](http://www.tt-forums.net/viewtopic.php?f=33&t=73397).  
  See the [guide on the wiki](https://github.com/JGRennison/OpenTTD-patches/wiki/Signalling) for more information.
* [Programmable pre-signals](http://www.tt-forums.net/viewtopic.php?f=33&t=47690).  
  These are not shown in the build signal window by default.  
  See the [guide on the wiki](https://github.com/JGRennison/OpenTTD-patches/wiki/Signalling) for more information.
* Freight train through load.  
  This is an alternative loading mode for freight trains for the case where the train is longer then the platform.
* Multiple rail types per tile.
* [Polyline rail track building tool](http://www.tt-forums.net/viewtopic.php?f=33&t=57080).
* Add news setting for trains waiting due to routing restrictions.
* Add setting to enable flipping direction of all train types in depot.
* Realistic train braking.  
  In this mode, trains have a stopping distance and will reserve ahead accordingly, trains cannot stop instantly.  
  See the [guide on the wiki](https://github.com/JGRennison/OpenTTD-patches/wiki/Realistic-braking) for more information.
* Allow converting track type under trains when compatible with the new rail type.
* Add feature where trains adjust their speed to match the train in front to avoid stop-start behaviour.
* Add a new train purchase window, where locomotive and wagons are in separate lists.
* Add information about train full and empty loads and achievable speeds to the train info window.
* Add setting to sort track types by category and speed.
* Add a setting for whether to pathfind up to back of a one-way path signal.
* Multi-aspect signal graphics.  
  This requires a NewGRF which supports this and realistic train braking.
* No-entry signals.  
  These are not shown in the build signal window by default.
* Add client setting to show all signals using the default baseset sprites.
* Remember the last-used signal type between games.
* Add client setting to show the introduction year for train wagons.
* Add setting for rail depot maximum speed.
* Add setting to allow auto-fill signal dragging to skip over stations/waypoints.

#### Roads and Road Vehicles

* One-way road and road vehicle overtaking enhancements.  
  See the [wiki](https://github.com/JGRennison/OpenTTD-patches/wiki/One-way-roads) for full details.
* Add setting to allow articulated road vehicles to overtake other vehicles.
* Add setting to control road vehicle re-routing on road layout changes.
* Disallow ordering ordinary road vehicles to tram depots and vice versa.
* Improve road vehicle pathfinding when multiple vehicles are simultaneously heading to a station with multiple bay/stop entrances.
* Add setting for default road/tram types.
* Add a setting to turn off road vehicles slowing in curves.
* Add a setting to disable road vehicles from passing through each other when blocked for an extended period of time.
* Allow road vehicle go to station/waypoint orders to have an associated required stop/bay/waypoint direction.
* Allow changing road vehicle driving side when all road vehicles are in depots.

#### Level Crossings

* [Close adjacent level crossings](http://www.tt-forums.net/viewtopic.php?p=836749).
* Prevent road vehicles from being stopped on level crossings.
* Add setting to enable improved level crossing safety.
* Trains break down after colliding with a road vehicle.
* Only show level crossing overlay sprites on the outsides of multi-track crossings when using both adjacent and safer crossings settings.

#### Bridges and Tunnels

* Signals in tunnels and on bridges.
* Custom bridge heads.
* [Chunnels (tunnels under bodies of water)](https://www.tt-forums.net/viewtopic.php?f=33&t=41775). Off by default.
* Allow building rail stations under bridges, subject to height/clearance and bridge pillar limitations.
* Add setting to allow placing NewGRF rail stations under bridges, when the GRF doesn't specify whether or not it can be placed under bridges.
* Add setting to allow placing all NewGRF objects under bridges, even when it would not otherwise be allowed by the GRF.
* Add setting to allow placing road/tram stops under bridges.
* Add setting to allow placing docks under bridges.
* Vehicles visible in tunnels (transparency setting).

#### Airports

* [Upgrade airports](http://www.tt-forums.net/viewtopic.php?f=33&t=35867).

#### Ships

* [Ship collision avoidance](http://www.tt-forums.net/viewtopic.php?f=33&t=74365).
* Allow NewGRF ships to carry more than one cargo.

#### Vehicles in General

* [Improved breakdowns](http://www.tt-forums.net/viewtopic.php?f=33&t=39518).
* [Vehicle repair cost setting](http://www.tt-forums.net/viewtopic.php?f=33&t=45642).
* Send vehicles which need auto-renewing due to age, for servicing, even if breakdowns are off and no servicing if no breakdowns is on.
* Add shift-clicking on vehicle depot button to select specific depot.
* Cargo type filter in vehicle list windows.
* Add client setting for vehicle naming scheme.
* [Vehicle lifetime profit](http://www.tt-forums.net/viewtopic.php?f=33&t=72844).
* Add settings to disable vehicle expiry and introduction after the given years.
* Open train vehicle details window on total cargo tab if shift pressed.
* Add news/advice setting to warn if no depot order in vehicle schedule.
* [Add buttons to collapse/expand all groups](http://www.tt-forums.net/viewtopic.php?f=33&t=74365).
* Add a menu item to the vehicle list to assign all listed vehicles to a new group.
* Add a setting to include the train length and group name in the vehicle details window.
* Add a setting for whether to open the new vehicle GUI when share-cloning.
* Add setting to disable mass action buttons for top-level vehicle lists.
* Add feature to create a new auto-named group when dragging and dropping a vehicle onto the new group button (ctrl includes shared order vehicles).
* Add settings to reduce vehicle running costs when a vehicle is stationary or in a depot.
* If a vehicle's next order is for the current station when leaving, start loading again without moving, instead of leaving.
* Slots and counters.  
  See the [guide on the wiki](https://github.com/JGRennison/OpenTTD-patches/wiki/Signalling) for more information.
* Add cargo capacity / running cost sort mode to the build vehicle window.
* Add client settings to show the full group hierarchy in group and vehicle names.

#### Orders and Timetabling

* [Automated timetables and separation](http://www.tt-forums.net/viewtopic.php?f=33&t=46391).
* Allow clearing of timetable time fields which are at 0. Allow explicitly setting timetable time fields to 0 without clearing them.  
* Allow changing/clearing the timetabled waiting time and max speed of all of a vehicle's orders at once.  
* Add client setting to show the remainder ticks in timetable, after dividing to days or minutes.  
* Add a company setting to control the number of ticks used in auto-fill timetable rounding.
* [Cargo type orders](https://www.tt-forums.net/viewtopic.php?p=1047749).  
  This allows order load/unload types to be set per cargo type. (This does work with cargodist).
* Order occupancy.  
  Add column to the orders GUI to show occupancy running average, show the average order occupancy, and add a vehicle sort mode.
* [Timetabling waiting time in depots](http://www.tt-forums.net/viewtopic.php?f=33&t=70969).
* Scheduled dispatch.  
  This allows dispatching vehicles from timing points using one or more repeating schedules. This is useful for clock-face timetabling.
* [More conditional orders](http://www.tt-forums.net/viewtopic.php?f=33&t=38317).  
  Next station: is cargo waiting, is cargo accepted, number of free platforms, amount of cargo waiting.  
  Percent of times, per-cargo load percentage, current time/date, timetable lateness.  
  Slots/counters: train in slot, slot occupancy, counter value.  
  Scheduled dispatch departure slots.
* Reverse at waypoint orders.
* Add a menu item to the vehicle list to change order target, e.g. for moving depot orders to a different depot.
* Add game setting to allow only non-stop orders for trains and road vehicles.
* Go to depot and sell vehicle orders.
* Order mode to lock timetable wait and travel times against autofill/automate changes.
* Leave early and leave early if any/all cargoes fully loaded order timetable flags.
* Timetabled wait times at waypoints.
* Add warning/info messages to the timetable window.
* Add features to reverse the order of an order list, and to append the reverse of an order list.  
  (Use the ctrl key when the end of orders marker is selected, or enable the order management button).
* Add features to duplicate an individual order and to change the jump target of conditional orders.
* Add company setting for whether to advance the current order when cloning/copying/sharing (if current depot is in order list).
* Add vehicle list menu item to mass cancel go to or service at depot orders.
* Allow changing colour of orders in order list and timetable windows.
* Add text label and departure board via order types.

#### Stations

* [Departure boards](https://www.tt-forums.net/viewtopic.php?f=33&t=49956).
* Add road waypoints.
* Add NewGRF road stops.
* Add a setting to increase the station catchment radius.
* Station rating: track "last visited vehicle type" separately per cargo.
* Add setting to scale station cargo capacity and rating tolerance by size.
* Add setting: station rating tolerance to waiting time depends on cargo class.
* Enable vehicle list buttons in station window when the list would be non-empty.
* Enable vehicle group management actions on other companies' stations.
* Add support for allowing/disallowing supply to a station, per cargo, by ctrl-clicking the station cargo rating.
* Add setting to show a company-coloured mark next to vehicles in vehicle list windows, if their owner does not match the list owner.
* Add a waiting cargo history graph for stations.
* Add a tooltip to show station rating details (controlled by a setting).
* Add sort by number of vehicles calling to the station list window.
* Add setting to distribute cargo received at a station to all accepting industries equally, instead of just one of them.
* Add setting to allow hiding viewport labels of individual waypoints.
* Increase the distance a station can be from the town centre and still be assigned have the same name as the town (no suffix/prefix), for large towns.
* [Allow NewGRFs to supply additional station name strings](https://github.com/JGRennison/OpenTTD-patches/wiki/GRF-features#extra-station-names).
* Allow generating new default name for station (ctrl-click default button in rename station query window).
* Allow exchanging a station's name with another station in the same town.

#### Towns

* [Town cargo generation factor](http://www.tt-forums.net/viewtopic.php?t=46399).
* [Rating in town label](http://www.tt-forums.net/viewtopic.php?f=33&t=42598).
* [Random town road reconstruction](https://www.tt-forums.net/viewtopic.php?f=33&t=36438). This defaults to off.
* Add very and extremely slow options to town growth rate setting.
* Add a setting to scale town growth rate by proportion of town cargo transported.
* Add "indifferent" mode to the town council attitude to area restructuring setting.
* Disallow converting town-owned roads to types with the no houses flag.
* Add public roads (road network automatically built between towns) at map generation and in the scenario editor.
* Add settings for if/when towns can build road bridges and tunnels.
* Add setting to limit length of continuous inclined roads built by towns.
* Add setting for whether to allow converting town road to non-house types.
* Allow overriding town road construction settings and whether town growth is enabled on a per-town basis, add setting to enable this for multiplayer clients.
* Allow NewGRFs to set town zone radii.
* Show town count in town directory window.

#### Industries

* Industry cargo generation factor.
* Allow linking only inputs or outputs to the smallmap and map mode viewports in the industry chain window.

#### Map and Landscaping

* Add a setting to [reduce](http://www.tt-forums.net/viewtopic.php?p=890778#p890778) or stop the tree growth rate.
* [Adjusted arctic tree placement](http://www.tt-forums.net/viewtopic.php?f=33&t=72502).
* Add a new tree placement mode (perfect).
* [Minimum town distance](https://www.tt-forums.net/viewtopic.php?f=33&t=33625).
* Add map generation settings to control river/lake, rocky patch, and tropic zone generation.
* Add generation of wide rivers.
* Add settings to customise the size of town zones, and city zones.

#### Construction

* Enable building rivers in game. Off by default.
* Add a setting to disable removing sea/rivers.
* Allow building objects by area (1x1 objects only).
* Allow purchasing a region of tiles at once, by dragging.
* Add setting to control if and how land purchasing is permitted.
* Add a company rate limit for land purchasing.
* Add a company rate limit for object construction.
* Add setting to disable object expiry after a given year.
* Add setting to ignore object introduction dates.
* Add setting for whether to confirm before demolishing industries and/or rail stations.
* Add picker tool for objects, rail types, road types, rail stations/waypoint, road stops/waypoints and signals, to the main toolbar help menu.

#### Scenario Editor

* [Picking and placing single houses in the scenario editor](http://www.tt-forums.net/viewtopic.php?f=33&t=68894).
* Add settings to enable multiple churches/stadiums and to ignore date/zone/GRF when placing houses in the scenario editor.
* [Remove all trees in scenario editor](http://www.tt-forums.net/viewtopic.php?f=33&t=49326).

#### Interface and Visuals

* [Zoning](http://www.tt-forums.net/viewtopic.php?f=33&t=33701).
* [Measurement tools](http://www.tt-forums.net/viewtopic.php?f=33&t=49212).
* [Enhanced viewport](https://www.tt-forums.net/viewtopic.php?f=33&t=53394).  
  Extra zoomed-out zoom levels with different map display modes (page up/down or ctrl-mousewheel).  
  Selected vehicle order overlays.  
  Industry tooltips.   
  Plans (useful in multiplayer).
* Add setting for shading trees on slopes in viewports (default on).
* Add setting for alternative linkgraph overlay colour schemes.
* [When building tunnels, open new viewports at the far end of the tunnel](https://www.tt-forums.net/viewtopic.php?f=33&t=72639).
* [Smallmap screenshots](http://www.tt-forums.net/viewtopic.php?f=33&t=44596).
* Whole map screenshots at current zoom level.
* Topography and industry screenshots.
* Make smallmap refresh period variable with map mode/zoom and pause state.
* Add display setting for income/cost text effects.
* Make the company infrastructure window scrollable.
* Add setting to disable water animation depending on zoom level.
* Add zoom in support to the minimap window.
* Add setting to increase the size of the main toolbar.
* Add cargo filtering and a show by cargo mode to the company delivered cargo graph.
* Add setting to display the area outside of the map as water.

#### Limits

* [Extra large maps](http://www.tt-forums.net/viewtopic.php?f=33&t=33137).
  Maximum map size is now 256M tiles, ranging from 16k x 16k to 256 x 1M.
* Increase the limit of NewGRF house IDs in a single game from 512 to 1024.
* Increase per-vehicle order limit from 254 to 64k.
* Increase maximum setting limits for per-company vehicle-type limits.
* Increase maximum permitted vehicle, group, depot and station/waypoint name lengths.
* Increase maximum permitted rail waypoint types from 256 to 64k.

#### Time and Date

* [Variable day length](http://www.tt-forums.net/viewtopic.php?p=1148227#p1148227).
* Add settings to show time in hours and minutes as well as or instead of days.

#### Multiplayer

* [Infrastructure sharing](http://www.tt-forums.net/viewtopic.php?f=33&t=42254)  
* Add company settings to enable competitors to buy/renew vehicles in this company's depots.  
* Add setting to control whether trains can crash with trains owned by other companies.
* [Give money to company, instead of player](https://www.tt-forums.net/viewtopic.php?f=33&t=63899), broadcast money transfer notifications to all players.
* Add setting to enable non-admin multiplayer clients to rename towns.
* Add a password mechanism to change network game settings from a network client.
* Auto-kick clients after too many failed rcon/settings attempts.
* Various changes to reduce the probability of desyncs and improve desync reporting/diagnostics.
* Add support for zstd savegame compression for autosaves and network joins.
* Increase the number of settings which can be changed in multiplayer.
* Store company passwords in network server saves in an encrypted form such that they are automatically restored when loaded into the same network server.
* Add client setting for whether to sync localisation settings (such as measurement units) with the server.

#### Money

* Add setting to control dates over which inflation is applied.
* Allow shift-clicking on borrow/repay money buttons to enter a quantity.
* Add mode to the cargo payment graph to show payment based on average transit speed.

#### Cheats

* Add support for server admin use of money, magic bulldozer, tunnels and jet crashes cheats in multiplayer.
* Add setting to allow non server admins to use the money cheat in multiplayer.
* Allow clicking the money text in the cheats window to enter a quantity.
* Add cheats to set inflation income and cost factors.
* Add cheat to set all station ratings to 100%.
* Add cheat to set all town local authority ratings to Outstanding.

#### Cargo Distribution and Link Graph

* Adjust link graph job scheduling algorithm to significantly improve responsiveness and prevent pausing.
* Improve scrolling rendering of link graph overlay on viewport and small map.
* Add new link graph distribution modes: asymmetric (equal) and asymmetric (nearest).
* Allow overriding distribution mode on a per-cargo basis, in game.
* Fix inaccurate cargo distribution and link graph overlays, and various other problems with large link graphs.
* Add setting to increase the cargodist link graph distance/cost metric of aircraft links.

#### Input

* Add modifier key window for toggling shift/ctrl key states using mouse.
* Add IME support on Linux/SDL2 (SDL2-supported IMEs and Fcitx).

#### Console and Scripts

* Add basic tab-completion to the console window.
* Add console commands for conditional execution from game date.
* [Daily/monthly/yearly scripts](http://www.tt-forums.net/viewtopic.php?f=33&t=49595)

#### Miscellaneous

* Pause on savegame load if ctrl key is pressed.
* Ctrl-click up/down in NewGRF window to move to top or bottom.
* Add setting for when to ask for confirmation before overwriting an existing savegame file, add unique ID to savegames.
* Allow setting the autosave interval to a custom number of in-game days or real-time minutes.
* Add more hotkeys.
* Allow AI/GS developers to reload GSs.
* Various extensions to the NewGRF developer debug tools.
* Various performance improvements.
* Various minor fixes, see changelog.
* [NewGRF specification additions](docs/newgrf-additions.html) ([online copy](https://jgrennison.github.io/OpenTTD-patches/newgrf-additions.html)).
* [NML specification additions](docs/newgrf-additions-nml.html) ([online copy](https://jgrennison.github.io/OpenTTD-patches/newgrf-additions-nml.html)).
* [AI/GS script additions](docs/script-additions.html) ([online copy](https://jgrennison.github.io/OpenTTD-patches/script-additions.html)).
* [Low-level code/performance changes](docs/jgrpp-low-level-changes.md).

#### Save/load and savegame format changes  
* Various changes to improve handling of savegames which use features not in trunk.  
* Savegames from this patchpack are not loadable in trunk.  
* Savegames from trunk up to the last savegame version which has been merged into this branch (*jgrpp*) should be loadable in this patchpack.  
* Savegames from other branches which use the save/load code in the *save_ext* branch (usually suffixed: *-sx*) which are also merged into this branch (*jgrpp*), or where the added feature is marked as discardable/ignorable, should be loadable in this patchpack.  
* Savegames from other patched versions are not loadable in this patchpack except for savegames from:  
  * The *tracerestrict* branch ([routing restrictions patch](http://www.tt-forums.net/viewtopic.php?f=33&t=73397))  
  * The [Spring 2013 Patch Pack](http://www.tt-forums.net/viewtopic.php?f=33&t=66892) v2.0 - v2.4 (subject to caveats, see below)  
  * [Joker's Patch Pack](https://www.tt-forums.net/viewtopic.php?f=33&t=74365) v1.19 - v1.27 (subject to caveats, see below)  
  * [Chill's Patch Pack](https://www.tt-forums.net/viewtopic.php?f=33&t=47622) v8 and v14.7 (subject to caveats, see below)

#### Caveats for loading savegames from the [Spring 2013 Patch Pack](http://www.tt-forums.net/viewtopic.php?f=33&t=66892):  
* This is not guaranteed to be bug free  
* Savegames with huge airports are rejected  
* Map sizes greater than 16k x 16k are rejected  
* PAX signals/stations and traffic lights are cleared, leaving ordinary signals/stations/roads  
* Rail ageing/grass on tracks, trip histories, leave order/wait for cargo, auto advertising campaigns, base cost multipliers and other features not in this patch pack are dropped/ignored.  
* SpringPP v2.0.102/103 only:  
  * Savegames which have aircraft approaching, landing, taking off or landed at an oil rig are rejected  
  * The inflation cost multiplier is adjusted on load

#### Caveats for loading savegames from [Joker's Patch Pack](https://www.tt-forums.net/viewtopic.php?f=33&t=74365):  
* This is not guaranteed to be bug free  
* Logic signals are cleared, leaving ordinary signals  
* Various vehicle separation settings and partially-automatic modes are not supported.  
* Rail ageing/grass on tracks, trip histories, waiting cargo histories, station cargo punishment and other features not in this patch pack are dropped/ignored.

#### Caveats for loading savegames from [Chill's Patch Pack](https://www.tt-forums.net/viewtopic.php?f=33&t=47622):  
* This is not guaranteed to be bug free  
* Speed signals are cleared, leaving ordinary signals  
* Various vehicle, economy, town and other settings are not supported  
* Link graph data (but not settings) is cleared  
* Train stuck counters, traffic lights and other features not in this patch pack are dropped/ignored.

#### A note on branches

Many features have two branches, the *feature* branches are just the raw features, without any modified savegame code.  
There are not generally savegame compatible with anything else, except for loading of trunk savegame versions at or before the point where the branch diverged from trunk.  
All other load attempts may result in undefined behaviour.  
The *feature-sx* branches use the savegame framework in the *save_ext* branch.

* * *

* * *

# OpenTTD

## Table of contents

- 1.0) [About](#10-about)
    - 1.1) [Downloading OpenTTD](#11-downloading-openttd)
    - 1.2) [OpenTTD gameplay manual](#12-openttd-gameplay-manual)
    - 1.3) [Supported platforms](#13-supported-platforms)
    - 1.4) [Installing and running OpenTTD](#14-installing-and-running-openttd)
    - 1.5) [Add-on content / mods](#15-add-on-content--mods)
    - 1.6) [OpenTTD directories](#16-openttd-directories)
    - 1.7) [Compiling OpenTTD](#17-compiling-openttd)
- 2.0) [Contact and community](#20-contact-and-community)
    - 2.1) [Multiplayer games](#21-multiplayer-games)
    - 2.2) [Contributing to OpenTTD](#22-contributing-to-openttd)
    - 2.3) [Reporting bugs](#23-reporting-bugs)
    - 2.4) [Translating](#24-translating)
- 3.0) [Licensing](#30-licensing)
- 4.0) [Credits](#40-credits)

## 1.0) About

OpenTTD is a transport simulation game based upon the popular game Transport Tycoon Deluxe, written by Chris Sawyer.
It attempts to mimic the original game as closely as possible while extending it with new features.

OpenTTD is licensed under the GNU General Public License version 2.0, but includes some 3rd party software under different licenses.
See the section ["Licensing"](#30-licensing) below for details.

## 1.1) Downloading OpenTTD

OpenTTD can be downloaded from the [official OpenTTD website](https://www.openttd.org/).

Both 'stable' and 'nightly' versions are available for download:

- most people should choose the 'stable' version, as this has been more extensively tested
- the 'nightly' version includes the latest changes and features, but may sometimes be less reliable

OpenTTD is also available for free on [Steam](https://store.steampowered.com/app/1536610/OpenTTD/), [GOG.com](https://www.gog.com/game/openttd), and the [Microsoft Store](https://www.microsoft.com/p/openttd-official/9ncjg5rvrr1c). On some platforms OpenTTD will be available via your OS package manager or a similar service.


## 1.2) OpenTTD gameplay manual

OpenTTD has a [community-maintained wiki](https://wiki.openttd.org/), including a gameplay manual and tips.


## 1.3) Supported platforms

OpenTTD has been ported to several platforms and operating systems.

The currently supported platforms are:

- Linux (SDL (OpenGL and non-OpenGL))
- macOS (universal) (Cocoa)
- Windows (Win32 GDI / OpenGL)

Other platforms may also work (in particular various BSD systems), but we don't actively test or maintain these.

### 1.3.1) Legacy support
Platforms, languages and compilers change.
We'll keep support going on old platforms as long as someone is interested in supporting them, except where it means the project can't move forward to keep up with language and compiler features.

We guarantee that every revision of OpenTTD will be able to load savegames from every older revision (excepting where the savegame is corrupt).
Please report a bug if you find a save that doesn't load.

## 1.4) Installing and running OpenTTD

OpenTTD is usually straightforward to install, but for more help the wiki [includes an installation guide](https://wiki.openttd.org/en/Manual/Installation).

OpenTTD needs some additional graphics and sound files to run.

For some platforms these will be downloaded during the installation process if required.

For some platforms, you will need to refer to [the installation guide](https://wiki.openttd.org/en/Manual/Installation).


### 1.4.1) Free graphics and sound files

The free data files, split into OpenGFX for graphics, OpenSFX for sounds and
OpenMSX for music can be found at:

- [OpenGFX](https://www.openttd.org/downloads/opengfx-releases/latest)
- [OpenSFX](https://www.openttd.org/downloads/opensfx-releases/latest)
- [OpenMSX](https://www.openttd.org/downloads/openmsx-releases/latest)

Please follow the readme of these packages about the installation procedure.
The Windows installer can optionally download and install these packages.


### 1.4.2) Original Transport Tycoon Deluxe graphics and sound files

If you want to play with the original Transport Tycoon Deluxe data files you have to copy the data files from the CD-ROM into the baseset/ directory.
It does not matter whether you copy them from the DOS or Windows version of Transport Tycoon Deluxe.
The Windows install can optionally copy these files.

You need to copy the following files:
- sample.cat
- trg1r.grf or TRG1.GRF
- trgcr.grf or TRGC.GRF
- trghr.grf or TRGH.GRF
- trgir.grf or TRGI.GRF
- trgtr.grf or TRGT.GRF


### 1.4.3) Original Transport Tycoon Deluxe music

If you want the Transport Tycoon Deluxe music, copy the appropriate files from the original game into the baseset folder.
- TTD for Windows: All files in the gm/ folder (gm_tt00.gm up to gm_tt21.gm)
- TTD for DOS: The GM.CAT file
- Transport Tycoon Original: The GM.CAT file, but rename it to GM-TTO.CAT


## 1.5) Add-on content / mods

OpenTTD features multiple types of add-on content, which modify gameplay in different ways.

Most types of add-on content can be downloaded within OpenTTD via the 'Check Online Content' button in the main menu.

Add-on content can also be installed manually, but that's more complicated; the [OpenTTD wiki](https://wiki.openttd.org/) may offer help with that, or the [OpenTTD directory structure guide](./docs/directory_structure.md).


### 1.6) OpenTTD directories

OpenTTD uses its own directory structure to store game data, add-on content etc.

For more information, see the [directory structure guide](./docs/directory_structure.md).

### 1.7) Compiling OpenTTD

If you want to compile OpenTTD from source, instructions can be found in [COMPILING.md](./COMPILING.md).


## 2.0) Contact and Community

'Official' channels

- [OpenTTD website](https://www.openttd.org)
- [OpenTTD official Discord](https://discord.gg/openttd)
- IRC chat using #openttd on irc.oftc.net [more info about our irc channel](https://wiki.openttd.org/en/Development/IRC%20channel)
- [OpenTTD on Github](https://github.com/OpenTTD/) for code repositories and for reporting issues
- [forum.openttd.org](https://forum.openttd.org/) - the primary community forum site for discussing OpenTTD and related games
- [OpenTTD wiki](https://wiki.openttd.org/) community-maintained wiki, including topics like gameplay guide, detailed explanation of some game mechanics, how to use add-on content (mods) and much more

'Unofficial' channels

- the OpenTTD wiki has a [page listing OpenTTD communities](https://wiki.openttd.org/en/Community/Community) including some in languages other than English


### 2.1) Multiplayer games

You can play OpenTTD with others, either cooperatively or competitively.

See the [multiplayer documentation](./docs/multiplayer.md) for more details.


### 2.2) Contributing to OpenTTD

We welcome contributors to OpenTTD.  More information for contributors can be found in [CONTRIBUTING.md](./CONTRIBUTING.md)


### 2.3) Reporting bugs

Good bug reports are very helpful.  We have a [guide to reporting bugs](./CONTRIBUTING.md#bug-reports) to help with this.

Desyncs in multiplayer are complex to debug and report (some software development skils are required).
Instructions can be found in [debugging and reporting desyncs](./docs/debugging_desyncs.md).


### 2.4) Translating

OpenTTD is translated into many languages.  Translations are added and updated via the [online translation tool](https://translator.openttd.org).


## 3.0) Licensing

OpenTTD is licensed under the GNU General Public License version 2.0.
For the complete license text, see the file '[COPYING.md](./COPYING.md)'.
This license applies to all files in this distribution, except as noted below.

The squirrel implementation in `src/3rdparty/squirrel` is licensed under the Zlib license.
See `src/3rdparty/squirrel/COPYRIGHT` for the complete license text.

The md5 implementation in `src/3rdparty/md5` is licensed under the Zlib license.
See the comments in the source files in `src/3rdparty/md5` for the complete license text.

The fmt implementation in `src/3rdparty/fmt` is licensed under the MIT license.
See `src/3rdparty/fmt/LICENSE.rst` for the complete license text.

The nlohmann json implementation in `src/3rdparty/nlohmann` is licensed under the MIT license.
See `src/3rdparty/nlohmann/LICENSE.MIT` for the complete license text.

The OpenGL API in `src/3rdparty/opengl` is licensed under the MIT license.
See `src/3rdparty/opengl/khrplatform.h` for the complete license text.

The catch2 implementation in `src/3rdparty/catch2` is licensed under the Boost Software License, Version 1.0.
See `src/3rdparty/catch2/LICENSE.txt` for the complete license text.

The icu scriptrun implementation in `src/3rdparty/icu` is licensed under the Unicode license.
See `src/3rdparty/icu/LICENSE` for the complete license text.

## 4.0 Credits

See [CREDITS.md](./CREDITS.md)
