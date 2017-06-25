## JGR's Patchpack version 0.20.0

This is a collection of patches applied to [OpenTTD](http://www.openttd.org/)

* * *

OpenTTD is a transport simulation game based upon the popular game Transport
Tycoon Deluxe, written by Chris Sawyer. It attempts to mimic the original
game as closely as possible while extending it with new features.

OpenTTD is licensed under the GNU General Public License version 2.0,
but includes some 3rd party software under different licenses. See the
section "Licensing" in readme.txt for details.

* * *

See [readme.txt](readme.txt) for the original OpenTTD readme.

The thread for this patchpack can be found [here](http://www.tt-forums.net/viewtopic.php?f=33&t=73469).

See [jgrpp-changelog.md](jgrpp-changelog.md) for changelog.


#### This patchpack contains the following

* Routing restrictions: [thread](http://www.tt-forums.net/viewtopic.php?f=33&t=73397)  
  As of v0.11.0 this includes the Long Reserve feature from [here](http://www.tt-forums.net/viewtopic.php?f=33&t=74365).  
  A version of this feature rebased onto [Cirdan's new map features branch](http://repo.or.cz/w/openttd/fttd.git) is in the *tracerestrict-cirdan* branch, see [this thread](http://www.tt-forums.net/viewtopic.php?f=33&t=58420)

* Programmable signals: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=47690)  
  This includes additions to the patch from the [Spring 2013 Patch Pack](http://www.tt-forums.net/viewtopic.php?f=33&t=66892)

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

* 255 GRFs in single player mode: [imported](http://www.tt-forums.net/viewtopic.php?p=894743#p894743)

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

* Increase number of available rail track types from 16 to 32 [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=74365) (added in v0.13.0)

* Cargo type orders, this allows order load/unload types to be set per cargo type [imported](https://www.tt-forums.net/viewtopic.php?p=1047749) (added in v0.15.0)  
  This has been modified so that cargo dest can follow orders with different load/unload types.

* Random town road reconstruction [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=36438) (added in v0.15.0)  
  This has been modified to change the setting scale. This defaults to off.

* When building tunnels, open new viewports at the far end of the tunnel [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=72639) (added in v0.15.0)

* Add a setting to increase the station catchment radius (added in v0.16.0)

* Custom bridge heads for road bridges (added in v0.17.0)

* Chunnels (tunnels under bodies of water) [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=41775) (added in v0.18.0)  
  This is enabled by a setting (off by default).

* Give money to company, instead of player [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=63899) (added in v0.18.0)  
  This has been modified to broadcast money transfer notifications to all players.

* Minimum town distance [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=33625) (added in v0.18.0)

* Add setting for alternative transfer payment mode. (added in v0.19.0)  
  Calculate leg payment as a journey from the source to the transfer station, minus previous transfers.  
  This is to more fairly distribute profits between transfer vehicles and avoid large negative payments on the final leg.

* Level crossing improvements (added in v0.19.0)  
  * Prevent road vehicles from being stopped on level crossings.  
  * Add setting to enable improved level crossing safety.

* Scheduled dispatch [imported](https://github.com/innocenat/OpenTTD-patches/tree/scheduled-dispatch-sx) (added in v0.20.0)  

* Performance improvements  
  * Improve dedicated server performance. Up to approximately 2.5x faster. (added in v0.8.1)  
  * Improve cargodest link graph calculation performance. Up to approximately 2x faster. (~1.3x faster in v0.8.1, further improvements in v0.17.2)  
  * Add a 32bpp SSE2 palette animator. This is ~4x faster than the non-accelerated palette animator. (added in v0.9.0)  
  * Various minor changes (see changelog).

* Save/load and savegame format changes  
  * Various changes to improve handling of savegames which use features not in trunk.  
  * Savegames from this patchpack are not loadable in trunk.  
  * Savegames from trunk up to the last savegame version which has been merged into this branch (*jgrpp*) should be loadable in this patchpack.  
  * Savegames from other branches which use the save/load code in the *save_ext* branch (usually suffixed: *-sx*) which are also merged into this branch (*jgrpp*), or where the added feature is marked as discardable/ignorable, should be loadable in this patchpack.  
  * Savegames from other patched versions are not loadable in this patchpack except for savegames from the *tracerestrict* branch ([routing restrictions patch](http://www.tt-forums.net/viewtopic.php?f=33&t=73397)),
    savegames from the [Spring 2013 Patch Pack](http://www.tt-forums.net/viewtopic.php?f=33&t=66892) v2.0 - v2.4 (subject to caveats, see below).

* Miscellaneous  
  * Various improvements to the crash logger.  
  * Label threads with a descriptive name on supported Unixy platforms. (added in v0.8.1)  
  * Adjust cargo dest link graph job scheduling algorithm to improve responsiveness. (added in v0.16.0)  
  * Add hover tool-tips, and improve visual contrast of cargo labels, in cargo dest graph legend window. (added in v0.16.0)  
  * Add shift-clicking on vehicle depot button to select specific depot. (added in v0.16.1)  
  * Increase maximum setting limits for per-company vehicle-type limits. (added in v0.17.0)  
  * Increase maximum permitted vehicle name length (added in v0.17.0), vehicle group name length (added in v0.17.2), and depot/station name lengths (added in v0.20.0).  
  * Trains break down after colliding with a road vehicle.  
  * Various minor fixes, see changelog.

* Translations  
  * German (by Auge)  
  * Korean (by kiwitreekor and TELK)


#### Caveats for loading savegames from the [Spring 2013 Patch Pack](http://www.tt-forums.net/viewtopic.php?f=33&t=66892):  
* This is not guaranteed to be bug free  
* Savegames with huge airports are rejected  
* Map sizes greater than 16k x 16k are rejected  
* PAX signals/stations and traffic lights are cleared, leaving ordinary signals/stations/roads  
* Rail ageing/grass on tracks, trip histories, leave order/wait for cargo, auto advertising campaigns, base cost multipliers and other features not in this patch pack are dropped/ignored.  
* SpringPP v2.0.102/103 only:  
  * Savegames which have aircraft approaching, landing, taking off or landed at an oil rig are rejected  
  * The inflation cost multiplier is adjusted on load


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

Sufficienty up-to-date versions of other compiler toolchains including MSVC and ICC should also work.
