// SPDX-License-Identifier: GPL-2.0-or-later
#include "kml_export.h"
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <algorithm>

KmlStation copyKmlStation(const DataRow *row, int maxTrailPoints)
{
  KmlStation copy;
  copy.name = QString::fromUtf8(row->call_sign);
  if (row->comment_data) copy.description = QString::fromLatin1(row->comment_data->text_ptr);
  copy.latitude = row->latitude; copy.longitude = row->longitude; copy.altitude = QByteArray(row->altitude).toDouble();
  for (const TrackRow *point = row->newest_trackpoint; point && (!maxTrailPoints || copy.trail.size() < maxTrailPoints); point = point->prev)
    copy.trail.append({90 - point->trail_lat_pos / 360000.0, point->trail_long_pos / 360000.0 - 180,
                      point->altitude == -99999 ? 0 : point->altitude / 10.0, bool(point->flag & TR_NEWTRK)});
  std::reverse(copy.trail.begin(), copy.trail.end());
  return copy;
}
bool writeKmlFile(const QString &path, const QList<KmlStation> &stations, QString *error, const QString &description)
{
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
  QXmlStreamWriter xml(&file); xml.setAutoFormatting(true); xml.writeStartDocument();
  xml.writeStartElement("kml"); xml.writeDefaultNamespace("http://www.opengis.net/kml/2.2"); xml.writeStartElement("Document");
  xml.writeTextElement("name", "APRS stations and trails");
  if (!description.isEmpty()) xml.writeTextElement("description", description);
  auto coordinate = [](double lat, double lon, double alt) { return QString("%1,%2,%3").arg(lon, 0, 'f', 7).arg(lat, 0, 'f', 7).arg(alt, 0, 'f', 1); };
  for (const auto &station : stations) {
    xml.writeStartElement("Placemark"); xml.writeTextElement("name", station.name);
    if (!station.description.isEmpty()) xml.writeTextElement("description", station.description);
    xml.writeStartElement("MultiGeometry"); xml.writeStartElement("Point");
    xml.writeTextElement("coordinates", coordinate(station.latitude, station.longitude, station.altitude)); xml.writeEndElement();
    QStringList segment;
    auto flush = [&] {
      if (segment.size() >= 2) { xml.writeStartElement("LineString"); xml.writeTextElement("tessellate", "1"); xml.writeTextElement("coordinates", segment.join(' ')); xml.writeEndElement(); }
      segment.clear();
    };
    for (const auto &point : station.trail) {
      if (point.newTrack) flush();
      segment << coordinate(point.latitude, point.longitude, point.altitude);
    }
    flush(); xml.writeEndElement(); xml.writeEndElement();
  }
  xml.writeEndElement(); xml.writeEndElement(); xml.writeEndDocument();
  if (xml.hasError()) { if (error) *error = QStringLiteral("Could not encode KML document."); return false; }
  if (!file.commit()) { if (error) *error = file.errorString(); return false; }
  return true;
}
