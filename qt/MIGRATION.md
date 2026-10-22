# Qt migration status

Reviewed on 2026-10-22 on branch `qt`. This is a current implementation overview;
the README also contains historical milestone descriptions.

## Implemented boundaries

| Area | Shared C behavior | Qt integration |
| --- | --- | --- |
| Position reports | Original uncompressed/compressed, DAO and Mic-E decoding | TNC2 dispatch, AGWPE and APRS-IS reception |
| Stations | Original database, comment/status history, weather, expiry and bounded trails | Station projection, navigation, details, map interaction and scheduled cleanup |
| View menu | Original station-list membership and traversal, ALOHA calculation | Live station tables, own weather, details/navigation, ALOHA statistics and session uptime |
| Messages | History storage, IDs, delivery ordering, retry/ACK policy, groups and auto reply | Conversations, queue controls and transport adapters |
| Objects/items | Legacy name validation, normal formatting, kill markers, comment limits and decaying repeat calculations | Saved local drafts, create/edit/send/kill management and explicit AGWPE transmission |
| Position beacons | Fixed-position packet formatting | Explicit TX controls and periodic scheduling |
| Map overlays | Station selection, distance/bearing, PHG/DF, weather and dead reckoning calculations | Raster/vector painting, labels, display controls and offline maps |
| History | Station and message restoration APIs | Worker-thread SQLite writes, bounded traffic retention, permanent message archive and safe replay |
| File operations | Shared station state for replay/export | Printing, KML export, Defaults/Timing dialogs and periodic PNG/KML snapshots |

The native Qt application and tests build through CMake without Motif/Xlib
application dependencies. Qt owns presentation, sockets and storage adapters.
Serial support remains outside this migration's current scope; AGWPE supplies
radio/TNC integration.

## File menu: station configuration, save and exit

File → Configure → Station opens the same station/interface settings dialog as
Interface → Interface Control. Save Config Now flushes the applied Qt settings
to disk and reports write failures; unapplied dialog edits still require their
own Save/OK action. Exit closes the main window through the normal Qt shutdown
path. Offscreen checks exercise all three actions, including persisted settings
bytes and closing the visible window.

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
ALOHA statistics with Internet-only records excluded. CAD polygons,
weather alerts and GPS Status still require their unfinished backends.

## Coordinate and unit preferences

File → Configure → Coordinate System selects decimal degrees, degrees/minutes,
or degrees/minutes/seconds, with a preview and apply/cancel behavior. The shared
original C coordinate converters supply the display strings. The format applies
to the map status and border, station details, station lists, Find/Track Station,
and the preferred-format preview in the coordinate calculator and Center & Zoom.
Coordinate entry remains explicitly decimal degrees; UTM/MGRS input and display
remain unfinished.

Enable English Units now switches distance, altitude, station speed, temperature,
wind, pressure and rainfall across map labels, details, tables, and ALOHA
statistics. The shared C display service uses the legacy conversion rules.
Metric is the default; English uses miles/yards, feet, mph, °F, inHg and inches.
Raw APRS records, numeric table sort values, exports and transmitted packets keep
their storage/protocol units. Open views update immediately without replaying
traffic or changing station timestamps.

Dist/Bearing Status follows the original mouse-pointer behavior. Moving over the
map shows pointer coordinates and, when enabled, distance and true bearing from
the configured local position. Short distances use meters or yards; longer ones
use kilometers or miles. Leaving the map restores the center-coordinate status.
No distance origin is invented when the local position is unavailable.

Qt preferences persist and override imported `COORDINATE_SYSTEM`,
`DISPLAY_UNITS_ENGLISH`, and `DISPLAY_DIST_BEAR_STATUS`. Invalid saved values
fall back to imported/default values. Unsupported legacy coordinate systems
fall back to decimal degrees. These controls affect presentation only.
Linux tests cover conversions, hemispheres and boundaries, apply/cancel/restart,
invalid settings, live unit/header updates, retained raw values and cursor status.

## Bulletin browser

View → Bulletins now shows general/group bulletins and announcements, with
sender/destination/text filtering and sortable columns. It receives live AGWPE
and APRS-IS traffic, including third-party packets within the existing depth
limit, and restores archived bulletin records without transmitting.

The shared message core recognizes `BLN` destinations with a numeric bulletin
slot or alphabetic announcement slot. It uses the original sender/destination
storage key and `msg_data_add` update rule: changed text replaces the line;
identical repeats preserve its timestamp until more than eight hours have
elapsed. Out-of-order older reports do not overwrite newer state. Empty lines
are accepted, and ACK-like bulletin text is treated as text. Bulletin reception
does not enter ACK, retry, auto-reply, private-message popup, or peer-route state.

The browser refreshes at most twice per second from the bounded 2,000-message
working set. Bulletin state participates in the permanent message archive,
separately from seven-day traffic retention. Original packet bytes remain in
the permanent message-packet archive. Restart preserves bulletin timestamps;
older archive records without a bulletin marker are recognized by destination.
Multiple archived versions of a line consolidate in the shared C store.

Linux fixtures cover actual bulletin/group/announcement formats, malformed and
empty bodies, duplicate/update timing, third-party reception, passive and
bidirectional Internet paths, and restoration with no delivery side effects.
Offscreen tests cover browser filtering, updates, selection and reopen behavior.
Distance filtering, automatic bulletin popups, bulletin transmission and
NWS/BOM alert processing remain unfinished. File log replay still updates station
state only and does not inject traffic into the message archive.

## Periodic KML snapshots

File → Enable KML Snapshots refreshes `snapshot.kml` using the same directory
and interval as PNG snapshots in Configure → Timing. KML captures active,
positioned stations, objects and items even when the map is hidden. The shared
Qt exporter preserves trail segment boundaries, coordinates and altitude units;
manual KML export still includes all retained trail points.

Periodic captures copy records in batches of 16, retain at most the most recent
1,000 trail points per station, and stop with an explicit error above 10,000
stations or 250,000 trail points. One capture/write may be outstanding. Encoding
and atomic file replacement run on a worker thread. Write failure disables the
KML toggle; superseded failures cannot disable a newer configuration. Imported
`KMLSNAPSHOTS_ENABLED` and Qt settings restore the toggle independently of PNG.

Linux offscreen checks cover hidden-window capture, periodic replacement,
station/object/item selection, trail boundaries and limits, XML escaping,
persistence, failure handling and configuration changes during a capture.

## Traffic logging controls

File now enables TNC, Net, Message and WX logging independently. Live reception
and transport-accepted outgoing packets write replayable JSONL files containing
original packet bytes, timestamp, device and direction. Message classification
uses the shared C TNC2/third-party parser, also used by delivery, passive Internet
reception and the permanent message archive. ACKs and bulletins are included.
Weather classification uses the existing APRS decoders, including positionless
reports. Queued TX records indicate transport acceptance, not RF reception or ACK.

Configure → Timing selects the traffic log directory. Files use UTC dates,
8 MiB rotations, a 32 MiB limit per category and seven calendar days of retention.
The pending worker queue is limited to 1,024 records and 8 MiB; overflow drops
records with a status notification. Disk writes, rotation and pruning run off the
UI thread. A write error disables the affected category. Shutdown drains accepted
work. These logs are independent of permanent message retention and SQLite history.

Qt settings override imported `TNC_LOG_DATA`, `NET_LOG_DATA`, `LOG_MESSAGE` and
`LOG_WX`. Legacy filenames are not reused; the default is the Qt app-data `logs`
directory. All toggles default off. File replay preserves reception timestamps,
labels outgoing records as queued TX and does not apply them to station state.
Replay never re-enters logging, message delivery, auto-reply or transmission.
IGate and WX Alert Logging remain explicitly unavailable pending their backends.

Linux tests cover exact bytes, directions and classifications, shared envelope
validation, replay isolation, rotation/retention, queue bounds and shutdown,
configuration apply/cancel/restart and superseded write failures.

## Own object/item transmission

Station → Manage Own Objects/Items creates and edits fixed, uncompressed reports
at the map center or entered decimal coordinates. The editor previews the actual
information field. Save creates a live local draft and pauses repeats; Send now
queues one report through the selected AGWPE interface. Kill queues an inactive
report and removes the local map entity. Editing a killed definition prepares a
live draft for a later explicit send. Names remain case sensitive. Items require
3–9 characters and exclude `!` and `_`; objects permit 1–9 printable ASCII
characters. Comments accept 43 printable ASCII bytes without silent truncation.

The normal formatter, name validators, kill marker and comment append routines
are extracted from original C code and shared by Qt and legacy adapters. The
repeat service uses the legacy initial 20-second interval, doubling and up to
20% jitter, bounded by each definition's maximum interval. Repeating kill
reports stop after 20 accepted transmissions, including the initial kill.
Rejected transport attempts consume no reports. At most four due reports are
attempted per timer pass; failures wait 20 seconds before another attempt.

Interface → Disable Transmit: Objects/Items is implemented, independent of My
Position. Objects/items also respect Disable Transmit: ALL, interface Enable TX,
connection state, matching station/interface callsigns and explicit RF paths.
Blank paths inherit the interface route. There is no Internet fallback. Queued
reports indicate AGWPE acceptance, without establishing RF delivery, reception,
digipeater repetition or an ACK. An unsuccessful kill pauses the live schedule
and reports the error, without falsely marking the entity killed.

Up to 200 definitions persist in Qt settings, independently of received station
history. Startup restores local drafts with their saved timestamps and leaves
all object schedules paused. Newer archived reception is preserved. Restoration
never queues packets or enters message delivery. Imported `DISABLE_OBJECT_TX`
and `OBJECT_RATE` supply defaults; new installations disable object TX initially.
Regular station-name collisions and objects/items now owned by another station
block transmission rather than overwriting their state. Forget killed removes a
definition without transmitting. Received-only objects are not automatically
adopted as definitions.

Linux fixtures verify exact live/kill wire formats, timestamp fields, name and
comment limits, coordinate boundaries, decay and kill limits. Offscreen tests
exercise draft editing, actual local AGWPE frames, separate transmit controls,
receive-only rejection, ownership conflicts, repeat/kill transitions and restart
without transmission. The existing 119 object utility and 42 object constructor
compatibility cases still pass. Moving/compressed/area/DF object creation,
automatic adoption and APRS-IS object transmission remain unfinished.

## Shared KISS and AX.25 codecs

The serial migration starts with frontend-independent codecs extracted from
`interface.c` and `db.c`. AX.25 address and UI-frame assembly/decoding now use
explicit binary lengths and bounds checks. AGWPE raw reception and the legacy
adapters call the same C decoder. KISS framing retains command/channel metadata,
preserves escaped bytes and CR/LF payloads, bounds frame storage, and recovers
at a delimiter after invalid escapes or oversize frames. Data and configuration
commands share one escaping routine, including their command/channel byte.
Legacy transmit adapters now use the configured channel instead of hardcoded 0.

C fixtures verify actual AX.25 bytes, SSIDs and repeated-hop flags, truncated and
invalid frames, all 256 byte values on all 16 KISS channels, configuration values
192/219, frame limits and malformed-stream recovery. The Qt radio adapters now share a transport interface and C-backed outgoing
AX.25 validation. Messages, beacons and objects select radios through that
interface, retaining device IDs and explicit RF routes. The serial adapter and
configuration integration are the next increment.

## Remaining migration work

Unfinished menu entries still need implementation, including audio/speech,
SmartBeaconing and alert/gateway logging.
Other gaps include GPS/NMEA integration, station ghosting,
advanced object modes, telemetry, and NWS processing. Reception
does not provide an implicit RF/Internet gateway.

The Linux tests cover shared C behavior, actual local AGWPE/APRS-IS fixture
traffic, message transitions and transmit controls, history/replay safety, and
offscreen UI interactions. Status coverage includes malformed input, optional
timestamps, duplicate and history bounds, retained coordinates, live details
updates, and restoration without transmission. These checks do not establish
live RF reception or digipeater behavior. Windows builds remain deferred at the
user's request.
