# Qt migration status

Reviewed on 2026-10-17 on branch `qt`. This is a current implementation overview;
the README also contains historical milestone descriptions.

## Implemented boundaries

| Area | Shared C behavior | Qt integration |
| --- | --- | --- |
| Position reports | Original uncompressed/compressed, DAO and Mic-E decoding | TNC2 dispatch, AGWPE and APRS-IS reception |
| Stations | Original database, comment/status history, weather, expiry and bounded trails | Station projection, navigation, details, map interaction and scheduled cleanup |
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
