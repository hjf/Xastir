# Xastir Qt map milestone

A Qt 6 Widgets frontend with a shared legacy C decoding core, built with CMake. The first milestone is
a main window, the legacy menu structure, an interactive OpenStreetMap raster map, and APRS
station and object icons with name labels. Station state can be loaded from JSON
snapshots or received live through an AGWPE client. The Message menu supports
APRS conversations and explicitly enabled transmission. The legacy C station database and message backend are shared with this frontend.
Serial interfaces are not linked into this executable.

## Linux dependencies

On Debian/Ubuntu:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools qt6-qpa-plugins libsqlite3-dev
```

The project requires Qt 6.4 or newer (Widgets and Network; Test when tests are
enabled), SQLite 3.24 or newer, CMake 3.21 or newer, a C99 compiler, and a C++17 compiler. It has no Motif or Xlib
application dependencies. Qt chooses the platform backend.

## Build and run

From the repository root:

```sh
cmake -S . -B build/qt -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/qt
./build/qt/qt/xastir-qt
```

The Autotools source lists also include the extracted core. The old Motif
message dialogs and queue controls need adaptation to the new delivery service.
The legacy build requires a clean rebuild because the record layouts changed.
Use a normal desktop terminal for an interactive launch. The Qt viewer starts
at your configured station position, zoom 12, when a legacy configuration is
available; otherwise it starts at latitude 20°, longitude 0°, zoom 3. To start over Buenos Aires, for example:

```sh
./build/qt/qt/xastir-qt --lat -34.6037 --lon -58.3816 --zoom 12
```

Drag with the left mouse button to pan. Scroll to zoom around the mouse pointer;
`+`/`−` zoom around the center, and arrow keys pan. Close the window with the
window manager's close button.

## Windows portable build

On Debian, install `g++-mingw-w64-x86-64-posix`, `curl`, `zip`, `python3`, CMake,
Ninja and native Qt **6.8.2** host tools. Then run:

```sh
./scripts/build-qt-windows.sh
```

The script verifies the official Qt source checksum, builds a static Windows
x64 Qt SDK, builds the application and tests, and packages
`build/xastir-qt-windows-x64.zip`. The first build takes considerably longer
than subsequent builds; set `XASTIR_BUILD_JOBS` to control parallelism.
`XASTIR_QT_HOST` and `XASTIR_QT_HOST_CMAKE` select alternate native host tools.

The EXE embeds Qt, compiler runtimes, APRS symbols and plugins. It uses Windows
Schannel for HTTPS, so no OpenSSL DLLs are needed. Only Windows system libraries
remain imported. The ZIP also includes license notices and a station demo.
See [Windows instructions](windows/README-Windows.txt) for configuration paths.
The application test fixture is copied into the build tree so tests can run
on Windows without accessing Linux source paths.

## Legacy menus

The menu bar reproduces File, View, Map, Station, Message, Interface and Help,
including nested menus, separators, mnemonics and toggle entries. The definitions
in `qt/legacy_menus.json` follow `create_appshell` in `src/main.c` and the English
labels in `config/language-English.sys`; optional Motif build features are
included in the scaffold.

The Message menu is implemented (see below). Other leaf actions open a
**Not implemented yet** dialog, including Exit and interface controls.
Placeholder toggles do not change settings. Submenu entries
open their nested menus. Map navigation and configured AGWPE startup reception
continue independently of these placeholders. JSON snapshots remain available
through `--stations`.

## APRS station icons

To see the bundled **synthetic** station examples over Buenos Aires:

```sh
./build/qt/qt/xastir-qt --no-config --stations qt/examples/stations.json --lat -34.6037 --lon -58.3816 --zoom 14
```

On the command line, `--stations` loads a JSON snapshot, defaults to zoom 12 and
centers on the first entry unless an explicit latitude or longitude is supplied.
The configured local station is retained.

Each record describes an already decoded station:

```json
[
  {"callsign": "N0CALL-1", "latitude": -34.6037, "longitude": -58.3816, "symbol": "/-"},
  {"callsign": "N0CALL-9", "latitude": -34.6125, "longitude": -58.3690, "symbol": "/>", "course": 90},
  {"callsign": "N0CALL-2", "latitude": -34.5980, "longitude": -58.4130, "symbol": "A#"}
]
```

`symbol` is the two-character APRS table/code pair: `/` for primary, a backslash
for alternate, or `0–9`/`A–Z` for an overlay on the alternate table. JSON strings
must escape backslashes; the example file includes an alternate-table station.
Optional `course` is degrees clockwise from north, from 0 to less than 360.
Directional symbols use the original artwork's four cardinal orientations;
stationary symbols keep their orientation. Missing/reserved artwork uses
Xastir's unknown-symbol image.

The original `symbols/symbols.dat` is embedded in the executable, including both
tables and overlay glyphs. Icons remain 20 logical pixels wide at every zoom;
callsigns have a white outline for readability. Stations wrap across the
antimeridian and repeat with the map at low zoom. Updates repaint the overlay
without reloading map tiles. Positions beyond Web Mercator's latitude limit
are clamped to the map edge.

Station callsigns are normalized to uppercase; object/item names retain their
case as required by the [APRS specification](https://www.aprs.org/doc/APRS101.PDF).
Snapshots may set `"kind": "object"` or `"kind": "item"` and an optional
`"source": "N0CALL"` originator; `callsign` holds the displayed name for all kinds.
The bundled examples include a park object and a first-aid item.
For received objects/items, the symbol, overlay and course come from the report,
the label shows the object/item name, and a kill report removes it from the map.
The status bar counts stations and objects/items separately.
Subsequent records for the same name replace its state. A malformed file leaves the existing station set intact.
`StationModel::upsert()`, `remove()`, and `clear()` provide the integration API
for packet decoders and transports. Call them on the GUI thread,
using queued connections when receiving data from another thread.

## AGWPE reception and legacy configuration

By default the application reads `~/.xastir/config/xastir.cnf` without modifying
it. It imports the station callsign, location and symbol, plus each AGWPE device
(`DEVICE<n>_TYPE:12`): `HOST`, `SPEED` (TCP port), `PASSWD`, `ONSTARTUP`, `RECONN`,
`TXMT`, `FILTER_PARAMS` (radio port), and `UNPROTO1` (path). Other interface types, including APRS-IS, are ignored. An absent default
configuration is allowed; an explicit unreadable configuration is an error.

```sh
./build/qt/qt/xastir-qt --config /path/to/xastir.cnf
./build/qt/qt/xastir-qt --no-agwpe   # import local station, without connecting
./build/qt/qt/xastir-qt --no-config  # standalone viewer / synthetic examples
```

Explicit `--lat`, `--lon` and `--zoom` override the configured initial view.
Devices marked
`ONSTARTUP:1` connect automatically; reconnection uses a 2–30 second backoff
when `RECONN:1`. Connection state and packet/position counts appear in the status
bar. The network client runs asynchronously on Qt's event loop.

The client optionally logs in with the configured password and base callsign,
requests server version and port information, and enables raw monitoring (`k`).
It receives UI frames with PID `F0` from all radio ports, matching the legacy
raw-monitor behavior. Reception itself does not register a callsign or transmit. Message transmission
uses the configured radio port when explicitly enabled (see below); beaconing,
digipeating and gating remain unimplemented.
Configuration passwords are never displayed or logged.

Position decoding supports uncompressed (including ambiguity), timestamped,
compressed Base91 and Mic-E positions, objects/items and their deletion, and
third-party encapsulation. Updates replace station icons by callsign or object
name; packets without a supported position leave the map unchanged. This is an
MVP decoder, not the full legacy APRS engine: GPS/NMEA, grid-only reports, DAO
precision extensions, trails, aging and weather details are not yet implemented.
Message decoding is handled by the separate message service. No serial or kernel AX.25 setup is required.

Protocol references: [APRS 1.0.1](https://www.aprs.org/doc/APRS101.PDF) and
[Dire Wolf's AGW server](https://github.com/wb2osz/direwolf/blob/master/src/server.c).

## Message menu

All ten Message menu entries are functional:

- **Send Message To** opens a conversation with recipient, interface and path
  controls. Sent and received messages show their time, text and delivery status.
  Enter or Send queues a message. Text is limited to 67 printable ASCII characters
  excluding `|`, `~` and `{`. Recipients include the full SSID.
- **Show Pending Messages** lists queued messages and their attempts/next retry.
  Select a row to cancel or retry; uncheck “Show only pending messages” to inspect
  completed outgoing history. Messages to the same station are sent in order.
- **Open Group Messages** opens group conversations. ALL, CQ and QST are built in;
  enter another group and click Join Group to subscribe. Subscriptions are saved.
  Group messages are sent once, without a message ID or ACK request. Group traffic
  is never acknowledged or automatically answered.
- **Clear All Outgoing Messages** confirms before cancelling the queue and
  clearing outgoing history. Received history remains.
- **General Stations Query**, **IGate Stations Query**, and **WX Stations Query**
  open an interface selector and send `?APRS?`, `?IGATE?`, or `?WX?` once.
- **Modify Auto Reply Message** edits the reply text (up to 64 characters), using
  the legacy `AUTO_MSG_REPLY` as its initial value. **Enable Auto Reply Message**
  toggles it for the current session. Replies use `AA:`, have no ACK request or
  forced retries, suppress duplicates and answer at most once per minute per
  sender. Incoming `AA:` replies do not trigger another auto reply. Auto reply
  starts disabled and is disabled when you manually send a message or query.
- **Satellite Ack Mode** suppresses standalone ACK packets. Reply-ACKs remain
  available in outgoing replies to capable peers. This mode is saved.

Incoming messages addressed to your exact callsign/SSID or a subscribed group
open the corresponding conversation. ACK/rejection matching uses both sender
and message ID; duplicate incoming packets do not create duplicate history.
Messages support Reply-ACK suffixes and third-party encapsulation. Numbered
private messages retry after 7 seconds with exponential backoff capped at 10
minutes, up to 18 transmission attempts. Disconnections do not consume attempts.
Automatic ACKs are limited to one per message ID per 30 seconds.

Sending uses AGWPE unproto `M`/`V` frames, PID `F0`, your configured callsign and
radio port (`FILTER_PARAMS`, one-based in the configuration). `UNPROTO1` supplies
the default path; an empty conversation path sends direct. `TXMT` and the global
`DISABLE_TRANSMIT` setting are respected. Receive-only interfaces are labelled
and their Send buttons disabled. To explicitly enable message transmission for
one run, without editing the legacy configuration:

```sh
./build/qt/qt/xastir-qt --agwpe-tx
```

This enables RF message/query transmission and automatic ACKs on configured
AGWPE interfaces. No beaconing, digipeating or IGating is implemented. Settings
for reply text, groups and satellite mode use Qt's application settings; the
legacy configuration remains untouched. Message history and the outgoing queue
are session-only (up to 2,000 history records and 100 pending messages).

Protocol references: [APRS message formats](https://www.aprs.org/doc/APRS101.PDF),
[Reply-ACK algorithm](https://www.aprs.org/aprs11/replyacks.txt), and
[auto-answer conventions](https://www.aprs.org/aprs11.html).

## Tile provider and cache

`--tile-url 'https://your-server/{z}/{x}/{y}.png'` selects a different XYZ provider.
Tiles must be 256 × 256 pixels. HTTP URLs are also supported for local servers.
The attribution is for OpenStreetMap data; a custom provider's additional
attribution requirements must be addressed before distributing that configuration.

The HTTP cache uses Qt's application cache directory (typically
`~/.cache/Xastir/XastirQt/tiles` on Linux), capped at 256 MiB. Override it with
`--cache-dir /path/to/cache`. Decoded tiles have a separate 64 MiB memory budget.

Only visible tiles are requested, with at most six concurrent downloads.
Downloads are asynchronous, duplicate requests are suppressed, and pending
requests for obsolete viewports are cancelled. View changes debounce requests
to avoid issuing downloads for every drag event. HTTP cache headers are respected.
Errors appear in the status bar; failed tiles have a 30-second retry cooldown,
and a later view change can retry them. There is no background prefetch or bulk
download facility. The default endpoint, attribution, and application User-Agent
follow the [OSM tile usage policy](https://operations.osmfoundation.org/policies/tiles/).

## Tests

```sh
ctest --test-dir build/qt --output-on-failure
```

Tests use Qt's offscreen platform and a local HTTP fixture server. They exercise
Web Mercator coordinates, wrapping and polar limits, downloads, persistent cache
reuse, request limits and cancellation, error reporting, rendered pixels,
navigation, the legacy menu structure and placeholder dialogs, shutdown, embedded APRS artwork, overlays and headings,
station validation and updates, icon pixels and labels, and world wrapping.
Message tests cover all ten menu actions, delivery/ACK/rejection matching,
retry timing, cancellation, ordered delivery, group handling, duplicate suppression,
auto reply, satellite mode, queries and AGWPE transmit frames using local fixtures.
Additional AGWPE tests cover configuration import, APRS formats, AX.25 framing,
login/subscription bytes, fragmented and coalesced TCP frames, reconnection and
invalid frame lengths using a local TCP fixture. They never download tiles from the public OSM service.
For a window-system smoke check, `xvfb` and `xauth` can be installed and the test
binary run with `xvfb-run -a ./build/qt/qt/test_qt_map`.

For a production build without tests:

```sh
cmake -S . -B build/qt-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/qt-release
```

## Next integration boundary

`MapWidget` owns viewport interaction and painting; `TileSource` owns HTTP and
tile caches; `map_projection` provides the shared coordinate conversions.
The renderer uses Qt logical pixels, letting Qt handle display scaling.
`StationModel` projects the shared C station database into Qt; `AprsSymbols` loads the original
artwork. `MapWidget::coordinateToScreen()` projects stations into the viewport.
`AgwpeClient` feeds decoded updates into the station model without changing tile
loading or rendering. The legacy map renderer and Xt event loop are not linked into this
executable.

## Shared legacy C core

`xastir_core` builds without Qt, Motif, Xlib, or application globals. Each
frontend owns independent station/message instances, and provides time and
transport callbacks explicitly. All mutations run on the owning thread.

The implementation reuses these original sources:

- `aprs_position.c`: coordinate, ambiguity, DAO, compressed/uncompressed,
  symbol overlay, speed/course and altitude routines moved from `db.c`/`util.c`.
- `aprs_mice.c`: the original Mic-E expansion algorithm from `db.c`, including
  rig identification, status, speed/course and altitude. Emergency is returned
  as data; presentation stays in the frontend adapter.
- `station_db.c`: original `DataRow` storage, name/time ordered lists, shortcut
  index, weather/trail allocation and ownership, comment/status history and
  deletion routines. Geographic imports retain precise doubles alongside the
  legacy centisecond coordinates. Moving stations accumulate bounded trails.
  Configured local positions take priority over received echoes.
- `message_store.c`: original history allocation/recycling and binary lookup,
  refactored to explicit context and correctly sized index allocation. Lookup
  keys separate sender/recipient/sequence and prefer the newest reused ID.
- `message_core.c`: the legacy transmit record and two-character counter, and
  a refactoring of the legacy ordering/retry/ACK policy into a service without
  dialog calls. The MVP's transport acceptance checks, duplicate suppression,
  groups, queries, auto reply and satellite mode live here. Failed transport
  attempts consume no retries. FIFO ordering handles identifier rollover.

`StationModel` and `MessageService` are Qt adapters. Their maps/lists are UI
projections, not independent database or delivery engines. JSON snapshot
parsing, AGWPE networking, TNC2 packet dispatch and map rendering remain in Qt.
This migration does not implement additional legacy features such as NMEA,
weather packet decoding, NWS alert processing, igating or object transmission.
The C database can already own their ancillary records for later integration.
Motif message dialogs still need adaptation to the new service; their queue
controls/history are not guaranteed to work after this refactor.

The position API takes writable NUL-terminated data, advances the comment
pointer, and removes DAO metadata. The Mic-E expansion API reads immutable
input and returns a bounded standard position report. Delivery callbacks must
not mutate the same message engine while it is executing.

To build and test just the C core (no Qt or C++ compiler required):

```sh
cmake -S . -B build/core -G Ninja -DXASTIR_BUILD_QT=OFF -DBUILD_TESTING=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

C tests cover truncation/malformed coordinates, Mic-E metadata and ambiguity,
independent database instances, list/index invariants, ancillary cleanup,
comment retention, movement trails, expiry, peer/history bounds, ordering,
ACK/Reply-ACK, retries, groups, and identifier rollover. Qt tests exercise
these same backends through the existing menus and local AGWPE fixtures.

View → Incoming Data (Visor → Datos Entrantes) opens the AGWPE packet monitor.
It retains the latest 2,000 packets, including non-position packets, identifies
receiving devices, and filters by packet text or callsign. Clear only clears the
visible history. Monitoring continues while the window is closed.

Capture writes incoming live packets to JSON Lines, preserving packet bytes as
base64 along with reception timestamps and device numbers. Stop capture closes
the file. Replay accepts these captures or plain TNC2 logs (one packet per line,
with optional blank lines and `#` comments). It streams ten packets per second
rather than reproducing the original timing, and can be stopped at any time.
Replay uses the same APRS decoder and shared C station database as live packets,
including object/item updates and deletions. It bypasses message delivery and
never sends acknowledgments or auto replies. Live reception remains active;
replayed packets update the current map database and are marked Replay in the
monitor. Replay is not included in live captures.

Station → Find Station and Station → Track Station now open a searchable live
list of stations, objects, and items. Search matches names, callsigns, and object
owners without regard to case. Center on map (or double-click a result) centers
the selected position once; Follow keeps it centered as live or replayed
positions change. Both preserve the current zoom level. Follow continues after
closing the dialog and appears in the main status bar. Stop following, dragging
the map, or using arrow keys ends tracking. Removing the target or replacing it
with a different kind of entity also ends tracking. Zooming keeps it centered.
These controls use the existing projection of the shared C station database;
they do not maintain another station store or enable any transmissions.

The remaining Station menu actions now control the Qt map. Filter Data uses the
original `ok_to_draw_station` policy extracted to `src/station_view.c`, with
explicit selection settings and clock/expiry parameters. RF/direct/digipeated,
network/file, own-station/object, tactical, fixed/moving/weather/CWOP, and
object-category filters affect rendering without discarding received records.
Include Expired Data shows reports older than the default one-hour display
expiry. Symbol rotation, labels, trailpoints, trails, course/speed/altitude,
weather text/temperature/wind barbs, ambiguity boxes, PHG/RNG, DF/DFS, Aloha,
dead-reckoning, distance/bearing, and report-age switches are connected.
Display and selection defaults come from the legacy `DISPLAY_*` configuration
keys; later Qt changes persist in Qt's application settings without rewriting
`xastir.cnf`. Optional display features have parent switches: for example,
weather text needs Display Weather Info, and DR Arc needs Enable Dead-Reckoning.

The shared C core owns the original APRS weather and PHG/DF extraction routines,
radio range calculations, station-selection structs/predicate, Aloha channel-load
classification/weights, and rhumb-line dead-reckoning routine. Legacy entry
points call this same core. Qt owns the painter and file/network adapters.
Weather updates with and without positions are supported; a positionless weather
report waits for a station position before appearing on the map. This does not
add the legacy proprietary weather station drivers or NWS alert handling.
Dead-reckoning is a visual prediction for reports up to ten minutes old and does
not alter the received position. Metric labels are used. Range/DF/Aloha overlays
require their corresponding metadata, and Aloha remains indeterminate until the
original channel-load threshold is reached. Qt's geographic painter replaces
X11 drawing calls rather than wrapping the old pixmap renderer.

Export All → Export to KML File writes active positions and stored trail segments
using escaped KML/XML and atomic file replacement. Store to open databases is
disabled, as it was in the original menu, because this frontend has no external
database interfaces. Fetch Findu Trail uses the documented
[raw.cgi interface](https://www.aprs.net/cgi.html), accepts timestamped HTML/raw
packet responses, and merges up to 5,000 trail points in time order using the
original C trail store. It preserves a newer live position, supports cancellation
and timeouts, and never passes downloaded packets to message delivery. The
response parser is tested with fixtures; live findU availability is not part of
the automated checks.

Configure → Reload Object/Item History reads `~/.xastir/config/object.log` in the
original information-field format and restores objects/items for display only.
It does not start the old periodic object transmitter. Clear Object/Item History
empties that file without removing current map objects. Tactical calls are read
from the original comma-separated `tactical_calls.log` on startup. Clear All
Tactical Calls removes current labels and comments out active history lines;
Clear Tactical Call History removes both labels and the file's contents. Clear
All Trails clears C-owned trails; Clear All Stations resets received data while
restoring the configured local station. Destructive menu actions ask for
confirmation in the application.

## Station details and interface control

Click a visible station, object, or item icon to inspect its live shared C
station record. Details include position, last heard, owner, course/speed,
altitude, comment, path, weather, and distance/bearing from your local station.
The dialog offers messaging, tracking, and trail display.

Interface → Interface Control configures the local station and AGWPE devices.
Save applies connection changes immediately and stores them in Qt application
settings. New message windows use the updated devices; message history and
pending delivery state are retained. Connections start when their Startup
checkbox is enabled. Removing an interface stops its connection. Changing the
station callsign takes effect after restarting, preserving queued message
identity. Saved Qt settings override imported CNF station/interface values;
`--no-config` skips both configuration sources. Transmission starts disabled
on new devices and requires explicitly checking Enable TX.

## APRS-IS reception

Interface Control also offers an APRS-IS connection, receive-only by default. Enter a server
host, TCP port (normally 14580), and server-side filter, for example
`r/-34.60/-58.38/100` for a 100 km radius. The checkbox enables startup reception;
saving applies connection changes immediately when the station identity is unchanged.
Receive-only login uses `pass -1` as specified by
[APRS-IS connection documentation](https://www.aprs-is.net/Connecting.aspx).
Internet reports use the same shared C station database and packet monitor,
including objects, items, and weather. Reception is not gated onto RF. Optional message transmission and ACKs require
checking Enable APRS-IS message transmission and entering your callsign passcode
in Interface Control. Sending waits for a matching verified server login.
Messages addressed to your exact callsign/SSID open the conversation even in
receive-only mode; the APRS-IS interface is selected for Internet conversations.
Verified connections use the shared C message engine for ACKs, retries, and
delivery status. Internet replies never select a prior RF route. Disable
Transmit ALL also blocks APRS-IS messages and ACKs. Position beacons remain AGWPE-only. Incoming lines and buffering
are bounded and processed in batches, with timeout and reconnect handling.

## Position beacons and transmit controls

Interface → Transmit Now opens a position preview and AGWPE device selector.
Enable TX on the chosen device in Interface Control and clear Disable My
Position before sending. Disable Transmit ALL blocks position beacons, messages,
and acknowledgments. New installations keep position transmission disabled.

Send now transmits a fixed uncompressed position using the original C coordinate
converters and extracted packet formatter. The formatter preserves the legacy
63-character information-field limit, truncating long comments. Optional periodic
beacons require explicitly enabling and applying a schedule (60–86,400 seconds);
the first automatic transmission waits one full interval. Settings persist across
restarts. Every send checks the current transmit controls and device connection.
APRS-IS supports explicitly enabled messages/ACKs, but not position beacons.


## Persistent history

History is enabled by default. Received AGWPE and APRS-IS traffic, plus packets
accepted for transmission by AGWPE, are saved to SQLite on a background writer
thread. Traffic is retained for seven days, with additional limits of 100,000
packets and 64 MiB of packet payload. Age cleanup runs at startup and every
minute; busy feeds also run cleanup every 256 writes. These limits apply to
traffic, not the permanent message archive, and exclude SQLite/index overhead.
Deleted storage is reused and gradually reclaimed with incremental vacuum.

On startup, retained received packets are applied in reception order through the
same decoder and shared C station database as live traffic. Last-heard and
weather timestamps retain their original reception times. Objects/items, kill
reports, comments, and trails are restored where supported by the existing
packet decoder. Outgoing records are history only and are never replayed into
station state. Replay cannot send packets, ACKs, auto replies, or station queries.

Messages, delivery status, and raw message/ACK/rejection packets have no automatic
expiry. The most recent 2,000 structured messages for the configured local
callsign populate the existing message windows; older messages remain accessible
in View → History using Older. Archives are separated by local callsign. Pending
messages become “Interrupted by restart” and require an explicit manual retry.
The outgoing message ID counter is persisted. Clear outgoing removes the current
queue/view but does not delete the permanent archive.

View → History displays recent traffic and paginated message history, and shows
the database path. On Linux the default path is normally
`~/.local/share/Xastir/XastirQt/history.sqlite`; other platforms use Qt's local
application-data location. Override it with `--history-db /path/history.sqlite`,
or use `--no-history` to disable both storage and startup restoration for a run.
`--no-config` does not disable history. Loading `--stations` replaces restored
station state with the supplied snapshot.

SQLite uses [write-ahead logging](https://www.sqlite.org/wal.html) and FULL
synchronous commits. Clean shutdown drains queued writes. A crash may lose
packets still waiting in the bounded writer queue; committed data survives an
unclean process exit. Write/overload errors appear in the status bar and stderr,
and reception continues. One application instance owns each database at a time
to prevent message-key/counter collisions. The permanent message archive can grow without limit
by design. Store the database on a local filesystem.

Linux builds use the system SQLite library. For static packaging, CMake accepts
`-DXASTIR_SQLITE_AMALGAMATION=/path/to/sqlite3.c` (with `sqlite3.h` alongside it).
The Windows packaging script selects Qt's bundled SQLite source so SQLite does
not introduce an extra DLL. This history change was built and tested on Linux;
no Windows build was performed.
