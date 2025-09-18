# Order list serialisation reference
This is a comprehensive list for all fields supported in order list import export. <br>
All time values are expressed in ticks. <br>
If you have any questions and/or wish to edit this document submit a PR, contact @lucafiorini on Discord or email luca.fio45@gmail.com.
## Common Top level Fields
|   Field                      |   Type         |   Notes / Legal Values                                                                                                                                          |
| ---------------------------- | -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `version`                    | int            | Export format version number.                                                                                                                                   |
| `source`                     | string         | OpenTTD revision string.                                                                                                                                        |
| `vehicle-type`               | enum           | `train`, `road`, `ship`, `aircraft`                                                                                                                             |
| `vehicle-group-name`         | string         | Name of the vehicle group (export only).                                                                                                                        |
| `nested-vehicle-group-names` | array\<string> | Ascending list of vehicle group names (export only).                                                                                                            |
| `route-overlay-colour`       | enum           | `dark-blue`, `pale-green`, `pink`, `yellow`, `red`, `light-blue`, `green`, `dark-green`, `blue`, `cream`, `mauve`, `purple`, `orange`, `brown`, `grey`, `white` |
| `game-properties`            | object         | [Game properties](#game-properties) simulates order-relevant game settings. (see below).                                                                        |
| `schedules`                  | array\<object> | [Dispatch Schedules](#dispatch-schedules) (see below).                                                                                                          |
| `orders`                     | array\<object> | List of [Orders](#orders) (see below).                                                                                                                          |
| `jump-from`                  | string         | Links jumps (see `jump-to` below in [Conditional](#conditionaltype-conditional)))                                                                               |

-----
## Game Properties
| Field                   | Type | Notes / Legal Values                                                  |
| ----------------------- | ---- | --------------------------------------------------------------------- |
| `default-stop-location` | enum | `near-end`, `middle`, `far-end`, `through`                            |
| `new-nonstop`           | bool | `true`, `false`                                                       |
| `ticks-per-minute`      | int  | Number of ticks in a minute. Mutually exclusive with `ticks-per-day`. |
| `ticks-per-day`         | int  | Number of ticks in a day. Mutually exclusive with `ticks-per-minute`. |

---
## Dispatch Schedules
| Field                 | Type              | Notes / Legal Values                               |
| --------------------- | ----------------- | -------------------------------------------------- |
| `duration`            | int               | **Required.** Length of the schedule in ticks.     |
| `name`                | string            | Schedule name.                                     |
| `relative-start-time` | int               | Start offset within repeating cycle.               |
| `absolute-start-time` | int               | Absolute tick start (incompatible with relative).  |
| `max-delay`           | int               | Optional max delay (ticks).                        |
| `re-use-all-slots`    | bool              | Whether all slots can be reused.                   |
| `renamed-tags`        | object            | Maps stringified tag IDs with corresponding name   |
| `slots`               | array<int/object> | Can be an int representing an offset or an object. |
#### Slots
When they represent a complex slot.
| Field         | Type  | Notes / Legal Values |
| ------------- | ----- | -------------------- |
| `offset`      | int   | Required.            |
| `tags`        | array | Numbers 1–4.         |
| `re-use-slot` | bool  | Reuse this slot.     |

---
## Orders
Fields can depend on the `type` of the order
#### Common fields
| Field              | Type | Notes / Legal Values                                                                                                                        |
| ------------------ | ---- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `type`             | enum | **Required**, can be: `go-to-station`, `go-to-depot`, `go-to-waypoint`, `conditional`, `implicit`, `slot`, `slot-group`, `counter`, `label` |
| `colour`           | enum | Same as `route-overlay-colour`                                                                                                              |
| `travel-time`      | int  | Timetable travel time.                                                                                                                      |
| `wait-time`        | int  | Timetable wait time.                                                                                                                        |
| `travel-fixed`     | bool | `true` if travel time is fixed / locked.                                                                                                    |
| `wait-fixed`       | bool | `true` if wait time is fixed / locked.                                                                                                      |
| `stopping-pattern` | enum | `go-to`, `go-nonstop-to`, `go-via`, `go-nonstop-via`                                                                                        |
| `stop-location`    | enum | `near-end`, `middle`, `far-end`, `through`                                                                                                  |
| `stop-direction`   | enum | `north-east`, `south-east`, `north-west`, `south-west`                                                                                      |
#### Go to (any) common fields (`type: go-to-*`)
| Field              | Type | Notes / Legal Values                                                                                                                        |
| ------------------ | ---- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `max-speed`        | int  | Maximum speed to this destination                                                                                                           |

#### Go to station(`type: go-to-station`)
| Field                  | Type        | Notes / Legal Values                                                                      |
| ---------------------- | ----------- | ----------------------------------------------------------------------------------------- |
| `destination-id`       | int         | Station ID                                                                                |
| `destination-name`     | string      | Export only.                                                                              |
| `destination-location` | obj         | `{X:int, Y:int}` tile location.                                                           |
| `load`                 | enum        | `load`, `full-load`, `full-load-any`, `no-load`                                           |
| `unload`               | enum        | `unload`, `unload-and-leave-empty`, `transfer`, `no-unload`                               |
| `load-by-cargo-type`   | array\<obj> | `{"<cargo-id>": {"load": <load>, "unload": <unload>}}`                                    |
| `timetable-leave-type` | enum        | `normal`, `leave-early`, `leave-early-if-any-cargo-full`, `leave-early-if-all-cargo-full` |
#### Go to Depot (`type: go-to-depot`)
| Field          | Type    | Nortes / Legal Values                                  |
| -------------- | ------- | ------------------------------------------------------ |
| `depot-id`     | int/str | Depot ID or `"nearest"`                                |
| `depot-action` | enum    | `service-only`, `stop`, `sell`, `unbunch`, `always-go` |
#### Go to Waypoint (`type: go-to-waypoint`)
| Field                  | Type   | Notes / Legal Values            |
| ---------------------- | ------ | ------------------------------- |
| `destination-id`       | int    | Waypoint ID.                    |
| `destination-name`     | string | Export only.                    |
| `destination-location` | obj    | `{X:int, Y:int}` tile location. |
| `waypoint-reverse`     | bool   | Reverse direction at waypoint.  |
#### Conditional(`type: conditional`)
| Field                  | Type   | Notes / Legal Values                                                                                                                                                                                                                                                                                                                                                            |
| ---------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `condition-variable`   | enum   | **Required**<br>`always`, `load-percentage`, `cargo-load-percentage`, `reliability`, `max-reliability`, `requires-service`, `age`, `remaining-lifetime`, `cargo-waiting`, `cargo-waiting-amount`, `cargo-waiting-amount-percentage`, `free-platforms`, `slot-occupancy`, `vehicle-in-slot`, `vehicle-in-slot-group`, `counter-value`, `timetable`, `time-date`, `dispatch-slot` |
| `jump-to`              | string | Label of order to jump to.                                                                                                                                                                                                                                                                                                                                                      |
| `condition-comparator` | enum   | `==`, `!=`, `<`, `<=`, `>`, `>=`, `true`, `false`                                                                                                                                                                                                                                                                                                                               |
| `condition-value[1-4]` | int    | Depends on variable. Values can be easy to parse but often are not (may include raw binary data).                                                                                                                                                                                                                                                                               |
| `condition-station`    | int    | Station ID if station-based condition.                                                                                                                                                                                                                                                                                                                                          |
#### Completely parsed `condition-variables`
In cases where commonly used`condition-values` are hard to parse there exists a dedicated import/export pipeline which replaces the raw values with named fields.
If you wish to add a `condition-variable` to this list, please submit a PR or ask Luca (contact info above).
######  `condition-variable = dispatch-slot`:
|   Field                 | Type     | Notes / Legal Values                                                 |
| ----------------------- | -------- | -------------------------------------------------------------------- |
| `condition-slot-source` | enum     | `next`, `last`, `vehicle`                                            |
| `condition-check-slot`  | str      | `"first"` or `"last"`<br>Incompatible with other `condition-check-*` |
| `condition-check-tag`   | int      | `[1–4]`<br>Incompatible with other `condition-check-*`               |
| `condition-check-route` | int      | ID of route<br>Incompatible with other `condition-check-*`           |
