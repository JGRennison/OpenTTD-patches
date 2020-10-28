## JGR's Patchpack: Low level changes

This document describes low-level changes to the codebase which are not generally visible when actually running/playing the game, this is a non-exhaustive list.
This document does not describe the player-visible changes/additions described in the main readme.

### Crash logger and diagnostics

* Additional logged items: current company ID, map size, configure invocation, thread name, recently executed commands.
* Additional logged platform-specific items: detailed OS version (Unix), signal details (Unix, Mac), exception record data (Windows).
* Better handling of crashes which occur in a non-main thread (ask the main thread to do the crash screenshot and savegame).
* Support logging register values on Unix and Mac.
* Support using libbfd for symbol lookup and line numbers (gcc/clang).
* Support using gdb/lldb if available to add further detail to the crashlog (Unix, Mac).
* Support using sigaction and sigaltstack for more information and correct handling of stack overflow crashes (Unix).
* Attempt to log stack overflow and heap corruption exceptions (Windows).
* Demangle C++ symbols (Unix).
* Attempt to handle segfaults which occur within the crashlog handler (Unix).
* Emit a "crash" log, savegame and screenshot on multiplayer desync.
* Add crash/desync information to output screenshot and savegame files.
* Multiplayer server and client exchange desync logs after a desync occurs.
* Decrease sync frame period when desync occurs.

#### Assertions

Various assertions are extended to log further information on failure.
Various assertions which check the state of a tile are extended to dump the tile state (m1 - m8, etc.) on failure.

#### Scope annotations

Scopes (in the main thread) can be annotated with a functor/lambda which is called in the event of a crash to provide further information to add to the crash log.

#### NewGRF debug window

Add various supplementary non-GRF information, e.g. vehicle variable and flags.

#### Logging

Add yapfdesync, linkgraph and sound log levels.
Extend desync and random logging.

### Map

Store tunnel start/end pairs in a pool, indexed in the start/end tiles.
Set bit in map if level crossing is possibly occupied by a road vehicle.
De-virtualise calls to AnimateTile().

### Viewport

Cache bridge/tunnel start and ends.
Cache station sign bounds.
Split sprite sort regions when more than 60 sprites present.
Reduce unnecessary region redraws when scrolling viewports.
Reduce viewport invalidation region size of track reservation and signal state changes.

### Rendering

Track dirty viewport areas seperately from general screen redraws, using a zoom-level dependant sized grid.
Use a rectangle array for general screen redraws instead of a block grid.
Add a dirty bit to windows and widgets, for redrawing entire windows or widgets.
Clip drawing of window widgets which are not in the redraw area.
Reduce unnecessary status bar redraws.
Filter out tile parts which are entirely outside the drawing area, within DrawTileProc handlers.

### Data structures

Various data structures have been replaced with B-tree maps/sets (cpp-btree library).
Various lists have been replaced with vectors or deques, etc.
Remove mutexes from SmallStack, only used from the main thread.
Use std::string in CommandContainer instead of a giant static buffer.
Add a free bitmap for pool slots.
Maintain free list for text effect entries.

### Vehicles

Cache the sprite_seq bounds.
Index the order list in vector.
Observe the operation of the NewGRF when getting the vehicle image/sprite, and elide further calls to the NewGRF if it can be determined that the result will be the same.
Add consist flag for case where no vehicles in consist are on a slope.
Add vehicle flag to mark the last vehicle in a consist with a visual effect.
Index the vehicle list in per type arrays for use by CallVehicleTicks.
Cache whether the vehicle should be drawn.

### Network/multiplayer

Add supplementary information to find server UDP packets and reply in an extended format with more info/wider fields if detected.
Paginate UDP packets longer than the MTU across multiple packets.
Use larger "packets" where useful in TCP connections.

### Sprites/blitter

Add a fast path to Blitter_32bppAnim::Draw.
Replace sprite cache implementation.

### Link graph

Completely change link graph job scheduling to make the duration of a job and the number of jobs per thread instance variable according to the estimated size of the job.
Various use of custom allocators, etc.
Early abort link graph threads if abandoning/quitting the game.
Various forms of caching and incremental updates to the link graph overlay.
Change FlowStat from an RB-tree to a flat map with small-object optimisation.
Change FlowStatMap from an RB-tree to a B-tree indexed vector.
Replace MCF Dijkstra RB-tree with B-tree.

### Pathfinder

YAPF: Reduce need to scan open list queue when moving best node to closed list

### Save and load

Feature versioning, see readme and code.
Extend gamelog to not truncate version strings.
Save/load the map in a single chunk, such that it can be saved/loaded in one pass.
Various other changes to savegame format and settings handling, see readme and code for details.
Replace read/write accessors and buffering.
Perform savegame decompression in a separate thread.
Pre-filter SaveLoad descriptor arrays for current version/mode, for chunks with many objects.

### AI/GS

[AI/GS script additions](docs/script-additions.html).
Add AI/GS method to get current day length.
Add GS method to create river tiles.
Add workaround for performance issues attempting to create a town when no town names are left.
Fixup a GS otherwise inconsistent with day length.

### NewGRF

[NewGRF specification additions](docs/newgrf-additions.html).
Add workaround for a known buggy NewGRF to avoid desync issues.

### Other performance improvements

Use multiple threads for NewGRF scan MD5 calculations, on multi-CPU machines.
Avoid redundant re-scans for AI and game script files.
Avoid iterating vehicle list to release disaster vehicles if there are none.
Avoid quadratic behaviour in updating station nearby lists in RecomputeCatchmentForAll.
Increase FIO buffer size.

### Command line

Add switch: -J, quit after N days.
Add savegame feature versions to output of -q.

### Configure/build

Changes to gcc/clang detection and flags
Changes to version detection and the format of the version string.
Minor CMake changes.

### Misc

Use of __builtin_expect, byte-swap builtins, overflow builtins, and various bitmath builtins.
Add various debug console commands.
Increase the number of file slots.
Cache font heights.
Cache resolved names for stations, towns and industries.
Change inheritance model of class Window to keep UndefinedBehaviorSanitizer happy.
Various other misc changes to reduce UndefinedBehaviorSanitizer spam.
Add a chicken bits setting, just in case.
