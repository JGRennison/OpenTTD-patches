## JGR's Patchpack version 0.5.1

This is a collection of patches applied to [OpenTTD](http://www.openttd.org/)

* * *

OpenTTD is a transport simulation game based upon the popular game Transport
Tycoon Deluxe, written by Chris Sawyer. It attempts to mimic the original
game as closely as possible while extending it with new features.

OpenTTD is licensed under the GNU General Public License version 2.0,
but includes some 3rd party software under different licenses. See the
section "Licensing" in readme.txt for details.

* * *

See readme.txt for the original OpenTTD readme.

The thread for this patchpack can be found [here](http://www.tt-forums.net/viewtopic.php?f=33&t=73469).


#### This patchpack contains the following

* Routing restrictions: [thread](http://www.tt-forums.net/viewtopic.php?f=33&t=73397)  
  This is developed in the *tracerestrict* branch.  
  A version of this feature rebased onto [Cirdan's new map features branch](http://repo.or.cz/w/openttd/fttd.git) is in the *tracerestrict-cirdan* branch, see [this thread](http://www.tt-forums.net/viewtopic.php?f=33&t=58420)

* Programmable signals: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=47690)  
  This includes additions to the patch from the [Spring 2013 Patch Pack](http://www.tt-forums.net/viewtopic.php?f=33&t=66892)

* Upgrade airports: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=35867)

* Vehicle group info: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=35867)

* Close adjacent level crossings: [imported](http://www.tt-forums.net/viewtopic.php?p=836749)

* Zoning: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=33701)  
  This is modified to remove unimplemented modes, implement station ownership checks and implement station facility checks for industries.

* Departure boards: [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=49956)  
  Fixed departure boards with orders with timetabled 0 travel times, e.g. those with depot service orders.  
  Fixed memory leak.  
  Made modifications to work with day length greater than 1.

* Town cargo generation factor: [imported](http://www.tt-forums.net/viewtopic.php?t=46399)

* Vehicles visible in tunnels (transparency setting): [imported](http://dev.openttdcoop.org/projects/clientpatches/repository/changes/VehicelsInTunnels.diff)

* Signals in tunnels and on bridges: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=41260)

* Measurement tools: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=49212)

* 255 GRFs in single player mode: [imported](http://www.tt-forums.net/viewtopic.php?p=894743#p894743)

* Improved breakdowns: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=39518)  
  Fixed minor bugs involving breakdown smoke.

* Timetabling waiting time in depots: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=70969)

* Picking and placing single houses in scenario editor: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=68894)  
  As of v0.5.0, allow only one instance of house picker window.

* Smallmap screenshots: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=44596)  
  This is modified to use an extra button in the smallmap window, instead of a console command, and use the current zoom level and display mode of the smallmap window.

* Automated timetables and separation: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=46391)  
  * Auto timetabling: Bias timetable adjustment to favour negative adjustments; this is to avoid positive feedback between congestion delays and increased timetable length. Reduce jam detection threshold.  
  * Auto separation: Fix handling of non-station orders (e.g. waypoints and depots). Add setting to scale vehicle lateness adjustments. No longer set vehicle lateness to 0 if separation fails, instead leave it as it was.  
  * Timetable GUI: Allow clearing of timetable time fields which are at 0. Allow explicitly setting timetable time fields to 0 without clearing them.  
  * Add a company setting to enable automatic timetabling for new vehicles (added in v0.4.0).

* Vehicle repair cost: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=45642)

* Enhanced viewport: [imported](https://www.tt-forums.net/viewtopic.php?f=33&t=53394)  
  Fixed crash when looking at aqueducts in viewport map mode.

* Infrastructure sharing: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=42254)  
  Fixed various issues with additions to the settings GUI.
  
* Rating in town label: [imported](http://www.tt-forums.net/viewtopic.php?f=33&t=42598)  
  Fixed small labels using wrong colour.

* Day length: [imported](http://www.tt-forums.net/viewtopic.php?p=1148227#p1148227)  
  * Minor tweak to timetable lateness calculation.  
  * Fixed dates/times in timetable window.  
  * As of v0.3.0, time in minutes is no longer scaled by the day length factor.  
  * As of v0.5.0, cargodest link graph timeout and compression intervals are scaled by the day length factor.  
  * As of v0.5.0, fix status bar date when time in minutes and show date with time are both on.

* Order occupancy  
  Add column to orders GUI to show occupancy running average.

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
  As of v0.5.0, fix selecting/editing conditional orders in non-train orders window.

* Include the train length and group name in the vehicle details window (added in v0.3.0)  
  This are each controlled by a setting (on by default).

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

* Save/load and savegame format changes  
  * Various changes to improve handling of savegames which use features not in trunk.  
  * Savegames from this patchpack are not loadable in trunk.  
  * Savegames from trunk up to the last savegame version which has been merged into this branch (*jgrpp*) should be loadable in this patchpack.  
  * Savegames from other branches which use the save/load code in the *save_ext* branch (usually suffixed: *-sx*) which are also merged into this branch (*jgrpp*), or where the added feature is marked as discardable/ignorable, should be loadable in this patchpack.  
  * Savegames from other patched versions are not loadable in this patchpack except for savegames from the *tracerestrict* branch ([routing restrictions patch](http://www.tt-forums.net/viewtopic.php?f=33&t=73397)),
    and as of v0.3.0 savegames from the [Spring 2013 Patch Pack](http://www.tt-forums.net/viewtopic.php?f=33&t=66892) v2.0, v2.1, v2.2 (subject to caveats, see below).

* Changes to the crash log (added in v0.5.0)  
  Enable stack traces on MinGW, try to demangle C++ symbol names, try to use libbfd for better symbol lookup, handle SIGSEGV while backtracing.


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
