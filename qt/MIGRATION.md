# Qt migration status

Reviewed on 2026-10-17 on branch `qt`. This is a current implementation overview;
the README also contains historical milestone descriptions.

## Implemented boundaries

| Area | Shared C behavior | Qt integration |
| --- | --- | --- |
| Position reports | Original uncompressed/compressed, DAO and Mic-E decoding | TNC2 dispatch, AGWPE and APRS-IS reception |
| Stations | Original database, comment/status history, weather, expiry and bounded trails | Station projection, navigation, details, map interaction and scheduled cleanup |
| View menu | Original station-list membership and traversal, ALOHA calculation | Live station tables, own weather, details/navigation, ALOHA statistics and session uptime |
| Messages | History storage, IDs, delivery ordering, retry/ACK policy, groups and auto reply | Conversations, queue controls and transport adapters |
| Position beacons | Fixed-position packet formatting | Explicit TX controls and periodic scheduling |
| Map overlays | Station selection, distance/bearing, PHG/DF, weather and dead reckoning calculations | Raster/vector painting, labels, display controls and offline maps |
| History | Station and message restoration APIs | Worker-thread SQLite writes, bounded traffic retention, permanent message archive and safe replay |
| File operations | Shared station state for replay/export | Printing, KML export, Defaults/Timing dialogs and periodic PNG snapshots |

The native Qt application and tests build through CMake without Motif/Xlib
application dependencies. Qt owns presentation, sockets and storage adapters.
Serial support remains outside this migration's current scope; AGWPE supplies
radio/TNC integration.

## Corrections from this review

- The Qt station projection now reads the resulting C record after updates.
  Weather received before a position, retained PHG metadata, reception flags
  and receive timestamps therefore agree with the shared database.
- Entity deletion now requires the requested station/object/item kind to match
  the stored record. An object or item kill cannot delete a regular station
  with the same name, including the configured local station.

## APRS status reports

AGWPE, APRS-IS, capture replay and startup restoration accept `>` status reports,
including third-party encapsulation within the existing depth limit. For example:

```text
REMOTE>APRS:>171234zReady for traffic
```

The original timestamp extraction from `db.c:extract_time` is now shared through
`xastir_extract_timestamp`. The C station service uses the original
`xastir_add_status` storage: at most 20 unique status strings per station,
case-insensitive duplicate matching, and newest reports first. Optional packet
timestamps are stripped using legacy rules; recorded reception times remain
the station and status-history timestamps.

Click a positioned station to see its status history in the scrollable details
window. Status-only stations are retained without inventing coordinates and
appear on the map after a position arrives. Until then, their packets remain
visible in Incoming Data. Status updates preserve existing coordinates and
course, and do not enter the message delivery/ACK/auto-reply service.

Status packets follow ordinary traffic retention: seven days, subject to the
packet count and byte limits. They are not private messages and are not part of
the indefinitely retained message archive. Restart rebuilds available status
history from retained received packets.

The timestamp helper preserves legacy recognition rules rather than validating
calendar ranges. Maidenhead and beam heading/power interpretation inside status
text remain unfinished, as in the original status-report handler. Telemetry
decoding and status transmission are not added by this feature.

## Station removal and trail timing

File → Configure → Timing now includes station removal time and the maximum
time and latitude/longitude gaps between trail points. Qt passes these settings
to the shared C database. The original trail builder marks a new segment when
either gap exceeds its limit; equality is allowed. A zero limit permits only
equal timestamps or coordinates, rather than disabling that check. Changes
affect new trail points; existing segment boundaries are preserved.

Station clear time also controls trail lifetime, replacing Qt's hardcoded
one-hour trail limit. Periodic cleanup expires trails even when a station stops
reporting or the map is hidden. Station removal deletes stale received records,
including positionless status/weather stations, from memory. As in the original
removal policy, the configured local station and owned objects/items are exempt.
Removing a followed station stops following it and refreshes open details.
Display Old Data bypasses map hiding, not removal or trail expiry.

Cleanup runs every minute in batches of at most 64 station records, yielding
between batches. Changing removal or clear time schedules a sweep immediately.
The shared C sweep retains a name cursor so reception, kills, and station
replacement between batches cannot invalidate it. Cleanup does not transmit,
delete messages, or prune persistent traffic. Retained traffic still restores
with its original reception timestamps, then remains subject to these in-memory
age limits. Historical log replay and Findu imports use the same age limits.

Qt settings override imported `DEFAULT_STATION_REMOVE`, `TRAIL_SEGMENT_TIME`,
and `TRAIL_SEGMENT_DISTANCE`; invalid saved values fall back to the imported or
default values. The defaults are two hours for station removal, 30 minutes
between trail points, and one degree per coordinate. Legacy configuration without
a removal value uses twice its imported clear time. Cancel preserves both live
settings and saved values. The expanded Timing dialog scrolls on small displays.

Linux C fixtures cover cutoff equality, ownership exemptions, positionless
expiry, trail linkage/counters, segment gap limits, and deletion between batches.
Offscreen Qt tests cover apply/cancel/restart, hidden-window cleanup, reception
between batches, station following, snapshot replacement, and archive retention
without transmission.

## View menu: station lists and statistics

View now provides All Stations, Mobile Stations, Local Stations, Last Stations,
Objects & Items, Own Objects & Items, Weather Stations, Own Weather Data,
Uptime, and ALOHA Statistics.

The original `list_gui.c` membership and traversal rules are shared through
`xastir_station_list_contains` and `xastir_station_list_member`. Mobile lists
require a retained trail; the moving flag alone is insufficient. Local lists
use the retained TNC reception flag, so a later Internet update does not remove
that record from the local list. Weather lists include positionless reports.
Lists operate independently of map display filters and clear-time hiding.
The shared object predicate also fixes the original backward-traversal mismatch
that could include regular TNC stations while omitting objects.

Qt tables show reception timestamps, packet counts, ownership, positions,
latest source/path and comment/status. Mobile and weather views add measurements
with explicit units. Own Weather Data selects received weather for the configured
callsign. Columns support sorting; Last Stations initially sorts newest first.
Text filtering searches across columns. Selected names survive refreshes.
Details works for positionless stations; Center on map and Follow require an
active positioned record. This makes status-only and weather-only details
accessible through the View menu.

Open lists refresh at most twice per second. Database records are copied in
batches of 128 with a name cursor, without retaining C row pointers between
event-loop turns. Closed lists do not trigger scans. Reopening refreshes the
current state. As elsewhere in Qt, kill reports remove objects/items from memory;
their retained packets remain accessible through traffic history rather than
inactive rows in these lists. No transmission or archive writes result from
opening, filtering, or navigating a list.

ALOHA Statistics calls the existing shared C calculation around the configured
station position. It shows category counts and the estimated radius, or an
indeterminate result when the available data cannot establish a radius. It uses
retained TNC reception records, excluding Internet-only reports. The dialog is
a snapshot with an explicit Refresh button. Uptime shows this application
session's start, elapsed duration from a monotonic clock, and station-record
count; it updates once per second.

Linux fixtures verify membership, inactive object handling and bidirectional
traversal. Offscreen tests exercise all ten actions, actual APRS position/status/
weather/object/item packets, filtering, selection retention, details/navigation,
updates and kills, replacement during a batch scan, teardown with a selected row,
weather units, uptime, and
ALOHA statistics with Internet-only records excluded. Bulletins, CAD polygons,
weather alerts and GPS Status still require their unfinished backends.

## Remaining migration work

Unfinished menu entries still need implementation, including coordinate-system
preferences, audio/speech, SmartBeaconing and several logging/export controls.
Other gaps include GPS/NMEA integration, station ghosting,
object/item transmission, telemetry, and NWS processing. Reception
does not provide an implicit RF/Internet gateway.

The Linux tests cover shared C behavior, actual local AGWPE/APRS-IS fixture
traffic, message transitions and transmit controls, history/replay safety, and
offscreen UI interactions. Status coverage includes malformed input, optional
timestamps, duplicate and history bounds, retained coordinates, live details
updates, and restoration without transmission. These checks do not establish
live RF reception or digipeater behavior. Windows builds remain deferred at the
user's request.
