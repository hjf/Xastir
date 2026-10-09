# Xastir Qt map milestone

An independent Qt 6 Widgets frontend, built with CMake. The first milestone is
a main window, File → Exit, an interactive OpenStreetMap raster map, and APRS
station icons with callsign labels. Station state can be loaded from JSON
snapshots. It currently has no live APRS interfaces or connection to the legacy station database.
AGWPE can be added as a network interface in a later milestone.

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
at latitude 20°, longitude 0°, zoom 3. To start over Buenos Aires, for example:

```sh
./build/qt/qt/xastir-qt --lat -34.6037 --lon -58.3816 --zoom 12
```

Drag with the left mouse button to pan. Scroll to zoom around the mouse pointer;
`+`/`−` zoom around the center, and arrow keys pan. File → Exit or `Ctrl+Q` closes
the window (`Cmd+Q` on macOS).

## APRS station icons

To see the bundled **synthetic** station examples over Buenos Aires:

```sh
./build/qt/qt/xastir-qt --stations qt/examples/stations.json --lat -34.6037 --lon -58.3816 --zoom 14
```

File → Open Stations (`Ctrl+O`) loads a JSON snapshot, replaces the current
station set, and centers on the first station in callsign order. It increases
zoom to 12 if the map is zoomed farther out. On the command line, `--stations`
defaults to zoom 12 and centers on the first station unless an explicit
latitude or longitude is supplied.

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

Station names are normalized to uppercase; subsequent records for the same
name replace its state. A malformed file leaves the existing station set intact.
`StationModel::upsert()`, `remove()`, and `clear()` provide the integration API
for a future packet decoder or AGWPE transport. Call them on the GUI thread,
using queued connections when receiving data from another thread.

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
navigation, File → Exit, shutdown, embedded APRS artwork, overlays and headings,
station validation and updates, icon pixels and labels, and world wrapping.
They never download tiles from the public OSM service.
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
A separate AGWPE client can feed the station model without changing tile loading
or rendering. The legacy map renderer and Xt event loop are not linked into this
executable.
