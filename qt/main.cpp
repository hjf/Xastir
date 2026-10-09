// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <cmath>

int main(int argc, char *argv[])
{
  QApplication application(argc, argv);
  QApplication::setOrganizationName(QStringLiteral("Xastir"));
  QApplication::setApplicationName(QStringLiteral("XastirQt"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Xastir Qt — APRS stations on OpenStreetMap"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOptions({
    {{"latitude", "lat"}, "Initial center latitude (−85.0511 to 85.0511).", "degrees", "20"},
    {{"longitude", "lon"}, "Initial center longitude (−180 to 180).", "degrees", "0"},
    {{"zoom", "z"}, "Initial zoom level (0–19).", "level", "3"},
    {"tile-url", "XYZ URL template for 256-pixel raster tiles.", "url", TileSourceOptions{}.urlTemplate},
    {"cache-dir", "Override the tile HTTP cache directory.", "directory"},
    {"stations", "Load a JSON snapshot of decoded APRS stations.", "file"}
  });
  parser.process(application);

  ApplicationOptions options;
  bool latitudeOk, longitudeOk, zoomOk;
  options.center.latitude = parser.value("latitude").toDouble(&latitudeOk);
  options.center.longitude = parser.value("longitude").toDouble(&longitudeOk);
  options.zoom = parser.value("zoom").toInt(&zoomOk);
  if (parser.isSet("stations") && !parser.isSet("zoom"))
    options.zoom = 12;
  options.tiles.urlTemplate = parser.value("tile-url");
  options.tiles.cacheDirectory = parser.value("cache-dir");
  QString error;
  if (!latitudeOk || !std::isfinite(options.center.latitude)
      || std::abs(options.center.latitude) > map::MaxLatitude)
    error = QStringLiteral("Latitude must be between −85.0511 and 85.0511.");
  else if (!longitudeOk || !std::isfinite(options.center.longitude)
           || std::abs(options.center.longitude) > 180.0)
    error = QStringLiteral("Longitude must be between −180 and 180.");
  else if (!zoomOk || options.zoom < map::MinZoom || options.zoom > map::MaxZoom)
    error = QStringLiteral("Zoom must be an integer between 0 and 19.");
  else
  {
    QString expanded = options.tiles.urlTemplate;
    expanded.replace("{z}", "0").replace("{x}", "0").replace("{y}", "0");
    const QUrl url(expanded);
    if (!options.tiles.urlTemplate.contains("{z}") || !options.tiles.urlTemplate.contains("{x}")
        || !options.tiles.urlTemplate.contains("{y}") || !url.isValid() || url.host().isEmpty()
        || (url.scheme() != "https" && url.scheme() != "http"))
      error = QStringLiteral("Tile URL must be an HTTP(S) URL containing {z}, {x}, and {y}.");
  }
  if (!error.isEmpty())
  {
    QTextStream(stderr) << error << '\n';
    return 1;
  }

  MainWindow window(options);
  if (parser.isSet("stations"))
  {
    const bool centerOnStations = !parser.isSet("latitude") && !parser.isSet("longitude");
    if (!window.loadStations(parser.value("stations"), &error, centerOnStations))
    {
      QTextStream(stderr) << error << '\n';
      return 1;
    }
  }
  window.show();
  return application.exec();
}
