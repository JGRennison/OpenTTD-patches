## JGR's Patchpack: Low level changes

This document describes low-level changes to the codebase which are not generally visible when actually running/playing the game, this is a non-exhaustive list.
This document does not describe the player-visible changes/additions described in the main readme.

### Crash logger and diagnostics

* Additional logged items: current company ID, map size, configure invocation, detailed OS version (Unix), thread name (Unix, Win32), recently executed commands.
* Better handling of crashes which occur in a non-main thread (ask the main thread to do the crash screenshot and savegame).
* Support logging register values on Unix.
* Support using libbfd for symbol lookup and line numbers (gcc/clang).
* Support using gdb if available to add further detail to the crashlog (Unix).
* Demangle C++ symbols (Unix).
* Handle segfaults which occur within the crashlog handler (Unix).

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

### Data structures

Various data structures have been replaced with B-tree maps/sets (cpp-btree library).
Various lists have been replaced with vectors or deques, etc.
Remove mutexes from SmallStack, only used from the main thread.
Use std::string in CommandContainer instead of a giant static buffer.

### Vehicles

Cache the sprite_seq bounds.
Index the order list in vector.
Observe the operation of the NewGRF when getting the vehicle image/sprite, and elide further calls to the NewGRF if it can be determined that the result will be the same.
Add consist flag for case where no vehicles in consist are on a slope.
Add vehicle flag to mark the last vehicle in a consist with a visual effect.
Index the vehicle list in per type arrays for use by CallVehicleTicks.

### Network/multiplayer

Add supplementary information to find server UDP packets and reply in an extended format with more info/wider fields if detected.
Paginate UDP packets longer than the MTU across multiple packets.

### Sprites/blitter

Add a fast path to Blitter_32bppAnim::Draw.
Replace sprite cache implementation.

### Link graph

Completely change link graph job scheduling to make the duration of a job and the number of jobs per thread instance variable according to the estimated size of the job.
Various use of custom allocators, etc.
Early abort link graph threads if abandoning/quitting the game.
Various forms of caching and incremental updates to the link graph overlay.

### Save and load

Feature versioning, see readme and code.
Extend gamelog to not truncate version strings.
Save/load the map in a single chunk, such that it can be saved/loaded in one pass.
Various other changes to savegame format and settings handling, see readme and code for details.
Replace read/write accessors and buffering.

### Command line

Add switch: -J, quite after N days.
Add savegame feature versions to output of -q.

### Configure/build

Changes to gcc/clang detection and flags
Changes to version detection and the format of the version string.

### Misc

Use of __builtin_expect, byte-swap builtins, and various bitmath builtins.
Add various debug console commands.
Increase the number of file slots.
Cache font heights.
Change inheritance model of class Window to keep UndefinedBehaviorSanitizer happy.
