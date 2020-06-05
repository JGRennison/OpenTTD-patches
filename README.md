## JGR's Patchpack version 0.34.4

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

See [jgrpp-changelog.md](jgrpp-changelog.md) for changelog.


#### This patchpack contains the following

* Routing restrictions: [thread](http://www.tt-forums.net/viewtopic.php?f=33&t=73397)  
  As of v0.11.0 this includes the Long Reserve feature from [here](http://www.tt-forums.net/viewtopic.php?f=33&t=74365).  
  A version of this feature rebased onto [Cirdan's new map features branch](http://repo.or.cz/w/openttd/fttd.git) is in the *tracerestrict-cirdan* branch, see [this thread](http://www.tt-forums.net/viewtopic.php?f=33&t=58420)

* Programmable pre-signals: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=47690)  
  This includes additions to the patch from the [Spring 2013 Patch Pack](http://www.tt-forums.net/viewtopic.php?f=33&t=66892)  
  These are not shown in the build signal window by default.

* Upgrade airports: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=35867)

* Vehicle group info: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=72855)

* Close adjacent level crossings: [imported](http://www.tt-forums.net/viewtopic.php?p=836749)

* Zoning: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=33701)  
  * This is modified to remove unimplemented modes, implement station ownership checks and implement station facility checks for industries.  
  * Add a mode to show restricted signals.
  * Add a mode to show station catchment only where station window open.

* Departure boards: [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=49956)  
  * Fixed departure boards with orders with timetabled 0 travel times, e.g. those with depot service orders.  
  * Made modifications to work with day length greater than 1.

* Town cargo generation factor: [imported](http://www.tt-forums.net/viewtopic.php?t=46399)  
  * Allow factor to be more finely adjusted in 0.1 increments. (added in v0.16.0)

* Vehicles visible in tunnels (transparency setting): [imported](http://dev.openttdcoop.org/projects/clientpatches/repository/changes/VehicelsInTunnels.diff)

* Signals in tunnels and on bridges: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=41260)  
  Modifications include support for PBS and setting the semaphore/electric type of signals.

* Measurement tools: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=49212)

* Increase maximum number of NewGRFs to 255  
  * In single player mode: [imported](http://www.tt-forums.net/viewtopic.php?p=894743#p894743)  
  * In multiplayer mode: (added in v0.25.0)

* Improved breakdowns: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=39518)  
  Add a lower limit for low speed breakdowns.

* Timetabling waiting time in depots: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=70969)

* Picking and placing single houses in scenario editor: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=68894)

* Smallmap screenshots: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=44596)  
  This is modified to use an extra button in the smallmap window, instead of a console command, and use the current zoom level and display mode of the smallmap window.

* Automated timetables and separation: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=46391)  
  * Auto timetabling: Bias timetable adjustment to favour negative adjustments; this is to avoid positive feedback between congestion delays and increased timetable length. Reduce jam detection threshold.  
  * Auto separation: Fix handling of non-station orders (e.g. waypoints and depots). Change to a per-vehicle setting. Add a company setting to scale vehicle lateness adjustments. No longer set vehicle lateness to 0 if separation fails, instead leave it as it was.  
  * Timetable GUI: Allow clearing of timetable time fields which are at 0. Allow explicitly setting timetable time fields to 0 without clearing them.
  * Add company settings to enable automatic timetabling or separation for new vehicles.  
  * Allow changing/clearing the timetabled waiting time and max speed of all of a vehicle's orders at once.  
  * Add client setting to show the remainder ticks in timetable, after dividing to days or minutes.  
  * Add a company setting to control the number of ticks used in auto-fill timetable rounding.

* Vehicle repair cost: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=45642)

* Enhanced viewport: [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=53394)

* Infrastructure sharing: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=42254)  
  * Add company settings to enable competitors to buy/renew vehicles in this company's depots.  
  * Add setting to control whether trains can crash with trains owned by other companies.

* Rating in town label: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=42598)

* Day length: [imported](http://www.tt-forums.net/viewtopic.php?p=1148227#p1148227)  
  * Add a setting to use non day length scaled days for cargo dest link graph calculation times (added in v0.11.0)

* Order occupancy  
  Add column to orders GUI to show occupancy running average, show the average order occupancy, and add a vehicle sort mode.

* Servicing  
  Send vehicles which need auto-renewing due to age for servicing, even if breakdowns are off and no servicing if no breakdowns is on.

* Everest tree-line: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=72502) (added in v0.2.0)  
  * Remove "no trees on this level" setting.  
  * Add on/off setting (default off).  
  * Add to settings GUI, add strings, help texts, etc.  
  * Change algorithm to make tree line and border of mixed forest zone less abrupt.

* Enable building rivers in game (added in v0.3.0)  
  This is controlled by a setting (off by default).

* More conditional orders: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=38317) (added in v0.3.0)

* Include the train length and group name in the vehicle details window (added in v0.3.0)  
  These are each controlled by a setting (on by default).

* Pause the game when cargo dest link graph jobs lag (added in v0.4.0)  
  Previously if a cargo dest link graph update job took longer than permitted, the game would block until it completed, preventing all user interaction.  
  This patch instead pauses the game until the job is completed. (This does not apply to network clients, which cannot pause/unpause the game).

* Daily/monthly/yearly scripts patch: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=49595) (added in v0.5.0)
 
* Flat minimap owner screenshot patch: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=42845) (added in v0.5.0)
 
* Extra large maps: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=33137) (added in v0.5.0)  
  Maximum map size is now 256M tiles, ranging from 16k x 16k to 256 x 1M.
  (The NewGRF debug inspection window is disabled for all map coordinates longer than 27 bits).

* Build and refit: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=35805) (added in v0.5.0)  
  This has been modified to change the UI, and make it multiplayer safe.

* Pause on savegame load if ctrl key is pressed. (added in v0.6.0)

* Reverse at waypoint orders (added in v0.7.0)

* Show a company-coloured mark next to vehicles in vehicle list windows, if their owner does not match list owner (imported: by McZapkie) (added in v0.8.0)  
  This has been modified to change the mark and be controlled by a setting (on by default).

* Vehicle lifetime profit: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=72844) (added in v0.10.0)  
  This has been modified to show current lifetime profit, instead of the yearly-updated value.

* Hierarchical group collapse: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=74365) (added in v0.11.0)  
  This has been modified to show an icon for collapsed groups, and only the enable the collapse/(un)collapse all buttons where useful.

* Ship collision avoidance: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=74365) (added in v0.11.0)

* Reduced tree growth: [imported](http://www.tt-forums.net/viewtopic.php?p=890778#p890778) (added in v0.11.0)

* Remove all trees in scenario editor: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=49326) (added in v0.11.0)

* Add a menu item to the vehicle list to change order target (added in v0.11.0)

* Template-based train replacement [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=58904) (added in v0.12.0)

* Add a menu item to the vehicle list to assign all listed vehicles to a new group (added in v0.12.1)

* Polyline rail track building tool [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=57080) (added in v0.13.0)

* Cargo type orders, this allows order load/unload types to be set per cargo type [imported](https://www.tt-forums.net/viewtopic.php?p=1047749) (added in v0.15.0)  
  This has been modified so that cargo dest can follow orders with different load/unload types.

* Random town road reconstruction [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=36438) (added in v0.15.0)  
  This has been modified to change the setting scale. This defaults to off.

* When building tunnels, open new viewports at the far end of the tunnel [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=72639) (added in v0.15.0)

* Add a setting to increase the station catchment radius (added in v0.16.0)

* Custom bridge heads for road bridges (added in v0.17.0) and rail bridges (added in v0.26.0)

* Chunnels (tunnels under bodies of water) [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=41775) (added in v0.18.0)  
  This is enabled by a setting (off by default).

* Give money to company, instead of player [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=63899) (added in v0.18.0)  
  This has been modified to broadcast money transfer notifications to all players.

* Minimum town distance [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=33625) (added in v0.18.0)

* Level crossing improvements (added in v0.19.0)  
  * Prevent road vehicles from being stopped on level crossings.  
  * Add setting to enable improved level crossing safety.

* Scheduled dispatch [imported](https://github.com/innocenat/OpenTTD-patches/tree/scheduled-dispatch-sx) (added in v0.20.0)

* Add a setting to disable removing sea/rivers (added in v0.21.0)

* Town growth  
  * Add very and extremely slow options to town growth rate setting. (added in v0.21.0)  
  * Add a setting to scale town growth rate by proportion of town cargo transported. (added in v0.21.0)

* Performance improvements  
  * Improve dedicated server performance. Up to approximately 2.5x faster. (added in v0.8.1)  
  * Improve cargodest link graph calculation performance. Up to approximately 2x faster. (~1.3x faster in v0.8.1, further improvements in v0.17.2)  
  * Various minor changes (see changelog).

* Multiple docks per station [imported](https://github.com/KeldorKatarn/OpenTTD_PatchPack/tree/feature/multiple_docks) (added in v0.22.0)

* Cargo type filter in vehicle list windows [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=77147) (added in v0.22.0)  
  This has been modified to support more windows and more cargo options.  
  This is enabled by a setting (on by default).

* Freight train through load (added in v0.24.0)  
  This is an alternative loading mode for freight trains for the case where the train is longer then the platform.

* Multiple rail types per tile (added in v0.29.0)

* More cheats and cheats in multiplayer (added in v0.34.2)  
  * Add support for server admin use of money, magic bulldozer, tunnels and jet crashes cheats in multiplayer.  
  * Add setting to allow non server admins to use the money cheat in multiplayer.  
  * Add cheats to set inflation income and cost factors.

* Save/load and savegame format changes  
  * Various changes to improve handling of savegames which use features not in trunk.  
  * Savegames from this patchpack are not loadable in trunk.  
  * Savegames from trunk up to the last savegame version which has been merged into this branch (*jgrpp*) should be loadable in this patchpack.  
  * Savegames from other branches which use the save/load code in the *save_ext* branch (usually suffixed: *-sx*) which are also merged into this branch (*jgrpp*), or where the added feature is marked as discardable/ignorable, should be loadable in this patchpack.  
  * Savegames from other patched versions are not loadable in this patchpack except for savegames from:  
    * The *tracerestrict* branch ([routing restrictions patch](http://www.tt-forums.net/viewtopic.php?f=33&t=73397))  
    * The [Spring 2013 Patch Pack](http://www.tt-forums.net/viewtopic.php?f=33&t=66892) v2.0 - v2.4 (subject to caveats, see below)  
    * [Joker's Patch Pack](https://www.tt-forums.net/viewtopic.php?f=33&t=74365) v1.19 - v1.27 (subject to caveats, see below)  
    * [Chill's Patch Pack](https://www.tt-forums.net/viewtopic.php?f=33&t=47622) v8 and v14.7 (subject to caveats, see below)

* Miscellaneous  
  * Various improvements to the crash logger.  
  * Adjust cargo dest link graph job scheduling algorithm to improve responsiveness. (added in v0.16.0)  
  * Add hover tool-tips, and improve visual contrast of cargo labels, in cargo dest graph legend window. (added in v0.16.0)  
  * Add shift-clicking on vehicle depot button to select specific depot. (added in v0.16.1)  
  * Increase maximum setting limits for per-company vehicle-type limits. (added in v0.17.0)  
  * Increase maximum permitted vehicle name length (added in v0.17.0), vehicle group name length (added in v0.17.2), and depot/station name lengths (added in v0.20.0).  
  * Trains break down after colliding with a road vehicle. (added in v0.20.0).  
  * Add warning/info messages to timetable window. (added in v0.21.0).  
  * Add ctrl+click on shared list button in order/timetable window to add single vehicle to a new group. (added in v0.21.0).  
  * Improve scrolling rendering of link graph overlay on viewport and small map. (added in v0.25.0).  
  * Add setting to automatically save when losing connection to a network game. (added in v0.25.0).  
  * Station rating: track "last visited vehicle type" separately per cargo. (added in v0.25.0).  
  * Go to depot and sell vehicle orders. (added in v0.26.0).  
  * Order mode to lock timetable wait and travel times against autofill/automate changes. (added in v0.26.0 and v0.27.0 respectively).  
  * Settings to allow placing stations and all NewGRF objects under bridges. (added in v0.26.0).  
  * Leave early order timetable flag. (added in v0.27.0).  
  * Timetabled wait times at waypoints. (added in v0.27.0).  
  * Add setting to enable flipping direction of all train types in depot. (added in v0.27.1).  
  * Allow purchasing a region of tiles at once, by dragging, and add a company rate limit for land purchasing (added in v0.29.0).  
  * Add setting to control if and how land purchasing is permitted. (added in v0.29.0).  
  * Add GUI setting for when to ask for confirmation before overwriting an existing savegame file, add unique ID to savegames. (added in v0.29.1).  
  * Add game setting to allow only non-stop orders for trains and road vehicles. (added in v0.29.3).  
  * Disallow ordering ordinary road vehicles to tram depots and vice versa. (added in v0.30.0).  
  * Add UI setting for whether to open the new vehicle GUI when share-cloning. (added in v0.30.0).  
  * Add company setting for whether to advance order when cloning/copying/sharing (if current depot is in order list). (added in v0.30.0).  
  * Allow diagonal construction of rivers in the scenario editor. (added in v0.30.2).  
  * Add setting to allow articulated road vehicles to overtake other vehicles. (added in v0.31.0).  
  * Add new link graph distribution modes: asymmetric (equal) and asymmetric (nearest). (added in v0.31.0).  
  * Add news/advice setting to warn if no depot order in vehicle schedule. (added in v0.31.1).  
  * Enable vehicle list buttons in station window when the list would be non-empty. (added in v0.31.1).  
  * Enable vehicle group management actions on other companies' stations. (added in v0.31.1).  
  * Add a password mechanism to change network game settings from a network client. (added in v0.31.4).  
  * Change network protocol to send server/join and rcon passwords in hashed form instead of in clear text. (added in v0.31.4).  
  * Add modifier key window for toggling shift/ctrl key states using mouse. (added in v0.32-rc4).  
  * Add IME support on Linux/SDL2 (SDL2-supported IMEs and Fcitx). (added in v0.32.0).  
  * Add support for allowing/disallowing supply to a station, per cargo, by ctrl-clicking the station cargo rating. (added in v0.34.0).  
  * Open train vehicle details window on total cargo tab if shift pressed. (added in v0.34.0).  
  * Ctrl-click up/down in NewGRF window to move to top or bottom. (added in v0.34.2).  
  * Additional conditional order types/modes. (added in v0.24.0, v0.33.1, v0.34.3).  
  * Various minor fixes, see changelog.  
  * [NewGRF specification additions](docs/newgrf-additions.html) ([online copy](https://htmlpreview.github.io/?https://github.com/JGRennison/OpenTTD-patches/blob/jgrpp/docs/newgrf-additions.html)).
  * [Low-level code/performance changes](docs/jgrpp-low-level-changes.md).

* Translations  
  * German (by Auge and Kruemelchen)  
  * Korean (by kiwitreekor and TELK)  
  * Japanese (by Qwerty Asd)

* Superseded features
  * Label threads with a descriptive name on supported Unixy platforms (added in v0.8.1), in trunk as of r27670.  
  * Add a 32bpp SSE2 palette animator. This is ~4x faster than the non-accelerated palette animator (added in v0.9.0), in trunk as of commit 17257b96.  
  * Increase number of available rail track types from 16 to 32 [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=74365) (added in v0.13.0), this is increased to 64 rail track types in trunk as of commit bf8d7df7, (added in v0.26.0).  
  * Towns build bridges over rails [imported](https://www.tt-forums.net/viewtopic.php?f=33t=76052) (added in v0.21.0), in trunk as of commit 50a0cf19.  
  * Add setting for alternative transfer payment mode (added in v0.19.0), in trunk and unconditionally enabled as of commit 2fee030a.

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


#### Compiler requirements

As of v0.15.0, C++11 support is required.

The minimum supported compiler versions are:
* GCC: 4.7
* clang: 3.3

Sufficiently up-to-date versions of other compiler toolchains including MSVC and ICC should also work.

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
    - 2.1) [Contributing to OpenTTD](#21-contributing-to-openttd)
    - 2.2) [Reporting bugs](#22-reporting-bugs)
    - 2.3) [Translating](#23-translating)
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

On some platforms OpenTTD will also be available via your OS package manager or a similar service.


## 1.2) OpenTTD gameplay manual

OpenTTD has a [community-maintained wiki](https://wiki.openttd.org/), including a gameplay manual and tips.


## 1.3) Supported platforms

OpenTTD has been ported to several platforms and operating systems.

The currently working platforms are:

- FreeBSD (SDL)
- Haiku (SDL)
- Linux (SDL)
- macOS (universal) (Cocoa video and sound drivers)
- OpenBSD (SDL)
- OS/2 (SDL)
- Windows (Win32 GDI (faster) or SDL)

### 1.3.1) Legacy support
Platforms, languages and compilers change.
We'll keep support going on old platforms as long as someone is interested in supporting them, except where it means the project can't move forward to keep up with language and compiler features.

We guarantee that every revision of OpenTTD will be able to load savegames from every older revision (excepting where the savegame is corrupt).
Please report a bug if you find a save that doesn't load.

## 1.4) Installing and running OpenTTD

OpenTTD is usually straightforward to install, but for more help the wiki [includes an installation guide](https://wiki.openttd.org/Installation).

OpenTTD needs some additional graphics and sound files to run.

For some platforms these will be downloaded during the installation process if required.

For some platforms, you will need to refer to [the installation guide](https://wiki.openttd.org/Installation).


### 1.4.1) Free graphics and sound files

The free data files, split into OpenGFX for graphics, OpenSFX for sounds and
OpenMSX for music can be found at:

- https://www.openttd.org/download-opengfx for OpenGFX
- https://www.openttd.org/download-opensfx for OpenSFX
- https://www.openttd.org/download-openmsx for OpenMSX

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

Add-on content can also be installed manually, but that's more complicated; the [OpenTTD wiki](https://wiki.openttd.org/OpenTTD) may offer help with that, or the [OpenTTD directory structure guide](./docs/directory_structure.md).

### 1.5.1) AI opponents

OpenTTD comes without AI opponents, so if you want to play with AIs you have to download them.

The easiest way is via the 'Check Online Content' button in the main menu.

You can select some AIs that you think are compatible with your playing style.

AI help and discussions may also be found in the [AI section of the forum](https://www.tt-forums.net/viewforum.php?f=65).

### 1.5.2) Scenarios and height maps

Scenarios and heightmaps can be added via the 'Check Online Content' button in the main menu.

### 1.5.3) NewGRFs

A wide range of add-content is available as NewGRFs, including vehicles, industries, stations, landscape objects, town names and more.

NewGRFs can be added via the 'Check Online Content' button in the main menu.

See also the wiki [guide to NewGRFs](https://wiki.openttd.org/NewGRF) and [the forum graphics development section](https://www.tt-forums.net/viewforum.php?f=66).

### 1.5.4) Game scripts

Game scripts can provide additional challenges or changes to the standard OpenTTD gameplay, for example setting transport goals, or changing town growth behaviour.

Game scripts can be added via the 'Check Online Content' button in the main menu.

See also the wiki [guide to game scripts](https://wiki.openttd.org/Game_script) and [the forum graphics game script section](https://www.tt-forums.net/viewforum.php?f=65).

### 1.6) OpenTTD directories

OpenTTD uses its own directory structure to store game data, add-on content etc.

For more information, see the [directory structure guide](./docs/directory_structure.md).

### 1.7) Compiling OpenTTD

If you want to compile OpenTTD from source, instructions can be found in [COMPILING.md](./COMPILING.md).


## 2.0) Contact and Community

'Official' channels

- [OpenTTD website](https://www.openttd.org)
- IRC chat using #openttd on irc.oftc.net [more info about our irc channel](https://wiki.openttd.org/Irc)
- [OpenTTD on Github](https://github.com/openTTD/) for code repositories and for reporting issues
- [forum.openttd.org](https://forum.openttd.org/) - the primary community forum site for discussing OpenTTD and related games
- [OpenTTD wiki](https://wiki.openttd.org/) community-maintained wiki, including topics like gameplay guide, detailed explanation of some game mechanics, how to use add-on content (mods) and much more

'Unofficial' channels

- the OpenTTD wiki has a [page listing OpenTTD communities](https://wiki.openttd.org/Community) including some in languages other than English


### 2.1) Contributing to OpenTTD

We welcome contributors to OpenTTD.  More information for contributors can be found in [CONTRIBUTING.md](./CONTRIBUTING.md)


### 2.2) Reporting bugs

Good bug reports are very helpful.  We have a [guide to reporting bugs](./CONTRIBUTING.md#bug-reports) to help with this.

Desyncs in multiplayer are complex to debug and report (some software development skils are required).
Instructions can be found in [debugging and reporting desyncs](./docs/debugging_desyncs.md).


### 2.3) Translating

OpenTTD is translated into many languages.  Translations are added and updated via the [online translation tool](https://translator.openttd.org).


## 3.0) Licensing

OpenTTD is licensed under the GNU General Public License version 2.0.
For the complete license text, see the file '[COPYING.md](./COPYING.md)'.
This license applies to all files in this distribution, except as noted below.

The squirrel implementation in `src/3rdparty/squirrel` is licensed under the Zlib license.
See `src/3rdparty/squirrel/COPYRIGHT` for the complete license text.

The md5 implementation in `src/3rdparty/md5` is licensed under the Zlib license.
See the comments in the source files in `src/3rdparty/md5` for the complete license text.

The implementations of Posix `getaddrinfo` and `getnameinfo` for OS/2 in `src/3rdparty/os2` are distributed partly under the GNU Lesser General Public License 2.1, and partly under the (3-clause) BSD license.
The exact licensing terms can be found in `src/3rdparty/os2/getaddrinfo.c` resp. `src/3rdparty/os2/getnameinfo.c`.

The implementation of C++17 `std::optional` in `src/3rdparty/optional` is licensed under the Boost Software License - Version 1.0.
See `src/3rdparty/optional/LICENSE_1_0.txt` for the complete license text.


## 4.0 Credits

See [CREDITS.md](./CREDITS.md)
