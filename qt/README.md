# Xastir Qt map milestone

An independent Qt 6 Widgets frontend, built with CMake. The first milestone is
a main window, the legacy menu structure, an interactive OpenStreetMap raster map, and APRS
station and object icons with name labels. Station state can be loaded from JSON
snapshots or received live through an AGWPE client. The Message menu supports
APRS conversations and explicitly enabled transmission. The legacy
station database and serial interfaces are not linked into this executable.

## Linux dependencies

On Debian/Ubuntu:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools qt6-qpa-plugins
```

The project requires Qt 6.4 or newer (Widgets and Network; Test when tests are
enabled), CMake 3.21 or newer, and a C++17 compiler. It has no Motif or Xlib
application dependencies. Qt chooses the platform backend.

## Build and run

From the repository root:

```sh
cmake -S . -B build/qt -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/qt
./build/qt/qt/xastir-qt
```

The existing Autotools build remains available for the Motif application.
Use a normal desktop terminal for an interactive launch. The Qt viewer starts
at your configured station position, zoom 12, when a legacy configuration is
available; otherwise it starts at latitude 20°, longitude 0°, zoom 3. To start over Buenos Aires, for example:

```sh
./build/qt/qt/xastir-qt --lat -34.6037 --lon -58.3816 --zoom 12
```

Drag with the left mouse button to pan. Scroll to zoom around the mouse pointer;
`+`/`−` zoom around the center, and arrow keys pan. Close the window with the
window manager's close button.

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
`StationModel` stores decoded station state; `AprsSymbols` loads the original
artwork. `MapWidget::coordinateToScreen()` projects stations into the viewport.
`AgwpeClient` feeds decoded updates into the station model without changing tile
loading or rendering. The legacy map renderer and Xt event loop are not linked into this
executable.
