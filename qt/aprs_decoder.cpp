// SPDX-License-Identifier: GPL-2.0-or-later
#include "aprs_decoder.h"
#include <cmath>
#include "aprs_position.h"
#include "station_view.h"
#include <QDateTime>
#include <cstdio>

namespace {
bool position(const QByteArray &data, Station &station)
{
  if (data.isEmpty() || data.contains('\0')) return false;
  QByteArray buffer = data;
  char *comment = buffer.data();
  XastirPosition decoded{};
  const bool uncompressed = data[0] >= '0' && data[0] <= '9';
  const int result = uncompressed
      ? xastir_extract_position(&decoded, &comment, 0)
      : xastir_extract_comp_position(&decoded, &comment);
  if (!result) return false;
  station.position = {90 - decoded.coord_lat / 360000.0, decoded.coord_lon / 360000.0 - 180};
  station.symbolTable = QChar(decoded.aprs_symbol.special_overlay
      ? decoded.aprs_symbol.special_overlay : decoded.aprs_symbol.aprs_type);
  station.symbolCode = QChar(decoded.aprs_symbol.aprs_symbol);
  DataRow metadata{};
  std::snprintf(metadata.speed, sizeof(metadata.speed), "%.9s", decoded.speed);
  std::snprintf(metadata.course, sizeof(metadata.course), "%.7s", decoded.course);
  if (xastir_extract_weather(&metadata, comment, !uncompressed, QDateTime::currentSecsSinceEpoch())) {
    station.weather = *metadata.weather_data;
    xastir_delete_weather(&metadata);
    decoded.speed[0] = decoded.course[0] = 0;
  } else if (uncompressed) xastir_extract_speed_course(comment, decoded.speed, decoded.course);
  char extension[32] = {};
  if (xastir_extract_powergain_range(comment, extension)) station.powerGain = QString::fromLatin1(extension);
  else station.powerGain = QString::fromLatin1(decoded.power_gain);
  if (xastir_extract_omnidf(comment, extension)) station.signalGain = QString::fromLatin1(extension);
  char bearing[4] = {}, nrq[4] = {};
  if (comment[0] == '/' && xastir_extract_bearing_NRQ(comment, bearing, nrq)) {
    station.bearing = QString::fromLatin1(bearing); station.nrq = QString::fromLatin1(nrq);
  }
  char altitude[32] = {};
  if (xastir_extract_altitude(comment, altitude))
    station.altitude = QString::number(QByteArray(altitude).toDouble() * 0.3048, 'f', 2);
  else station.altitude = QString::fromLatin1(decoded.altitude);
  station.speed = QString::fromLatin1(decoded.speed);
  station.comment = QString::fromLatin1(comment);
  station.ambiguity = decoded.pos_amb;
  if (decoded.course[0]) {
    const int course = QByteArray(decoded.course).toInt();
    if (course > 0 && course <= 360) station.course = course % 360;
  }
  return true;
}
bool micE(const QByteArray &destination, const QByteArray &data, Station &station)
{
  if (data.size() < 9 || data.contains('\0')) return false;
  char expanded[513];
  int emergency;
  if (!xastir_expand_mice(destination.constData(), data.constData() + 1,
                          expanded, sizeof(expanded), &emergency)) return false;
  station.emergency = emergency;
  return position(QByteArray(expanded), station);
}
}
std::optional<AprsUpdate> decodeAprs(const QByteArray &packet, int depth)
{
  if (depth > 4 || packet.size() > 4096) return {};
  const auto arrow = packet.indexOf('>'), colon = packet.indexOf(':', arrow + 1);
  if (arrow <= 0 || colon <= arrow + 1) return {};
  const QByteArray destination = packet.mid(arrow + 1, colon - arrow - 1).split(',').first();
  QByteArray data = packet.mid(colon + 1);
  if (data.isEmpty()) return {};
  if (data[0] == '}') return decodeAprs(data.mid(1), depth + 1);
  AprsUpdate update;
  update.station.callsign = QString::fromLatin1(packet.left(arrow)).trimmed().toUpper();
  update.station.path = QString::fromLatin1(packet.mid(arrow + 1, colon - arrow - 1));
  update.station.direct = !update.station.path.contains('*') && depth == 0;
  auto timestamp = [](const QByteArray &value) {
    if (value.size() != 7 || !QByteArray("zh/").contains(value[6])) return false;
    for (int i = 0; i < 6; ++i) if (value[i] < '0' || value[i] > '9') return false;
    return true;
  };
  bool valid = false;
  switch (data[0]) {
    case '!': case '=': valid = position(data.mid(1), update.station); break;
    case '/': case '@': valid = timestamp(data.mid(1, 7)) && position(data.mid(8), update.station); break;
    case '\'': case '`': valid = micE(destination, data, update.station); break;
    case ';':
      if (data.size() < 11 || (data[10] != '*' && data[10] != '_')) return {};
      update.station.kind = StationKind::Object;
      update.station.sourceStation = update.station.callsign;
      update.station.callsign = QString::fromLatin1(data.mid(1, 9)).trimmed();
      update.removed = data[10] == '_'; valid = update.removed || (timestamp(data.mid(11, 7)) && position(data.mid(18), update.station)); break;
    case ')': {
      int marker = 4;
      while (marker < data.size() && marker <= 10 && data[marker] != '!' && data[marker] != '_') ++marker;
      if (marker > 10 || marker >= data.size()) return {};
      update.station.kind = StationKind::Item;
      update.station.sourceStation = update.station.callsign;
      update.station.callsign = QString::fromLatin1(data.mid(1, marker - 1)).trimmed();
      update.removed = data[marker] == '_'; valid = update.removed || position(data.mid(marker + 1), update.station); break;
    }
    default: return {};
  }
  if (!valid || update.station.callsign.isEmpty()) return {};
  if (!update.removed && !StationModel::validate(update.station, nullptr)) return {};
  return update;
}
std::optional<AprsWeatherUpdate> decodeAprsWeather(const QByteArray &packet, int depth)
{
  if (depth > 4 || packet.size() > 4096 || packet.contains('\0')) return {};
  const int arrow = packet.indexOf('>'), colon = packet.indexOf(':', arrow + 1);
  if (arrow <= 0 || colon <= arrow + 1) return {};
  const auto data = packet.mid(colon + 1);
  if (data.startsWith('}')) return decodeAprsWeather(data.mid(1), depth + 1);
  if (data.size() < 10 || data[0] != '_') return {};
  for (int i = 1; i <= 8; ++i) if (data[i] < '0' || data[i] > '9') return {};
  auto info = data.mid(9); DataRow row{};
  if (!xastir_extract_weather(&row,info.data(),0,QDateTime::currentSecsSinceEpoch())) return {};
  AprsWeatherUpdate result{QString::fromLatin1(packet.left(arrow)).trimmed().toUpper(), *row.weather_data,
    depth == 0 && !packet.mid(arrow + 1,colon - arrow - 1).contains('*')};
  xastir_delete_weather(&row); return result;
}
std::optional<AprsStatusUpdate> decodeAprsStatus(const QByteArray &packet, int depth)
{
  if (depth > 4 || packet.size() > 4096 || packet.contains('\0')
      || packet.contains('\r') || packet.contains('\n')) return {};
  const int arrow = packet.indexOf('>'), colon = packet.indexOf(':', arrow + 1);
  if (arrow <= 0 || colon <= arrow + 1) return {};
  const auto data = packet.mid(colon + 1);
  if (data.startsWith('}')) return decodeAprsStatus(data.mid(1), depth + 1);
  if (!data.startsWith('>') || data.size() > 513) return {};
  const auto path = packet.mid(arrow + 1, colon - arrow - 1);
  return AprsStatusUpdate{QString::fromLatin1(packet.left(arrow)).trimmed().toUpper(),
    QString::fromLatin1(path), data.mid(1), depth == 0 && !path.contains('*')};
}

QByteArray decodeAgwpeAx25(const QByteArray &payload)
{
  int offset = 1;
  QList<QByteArray> addresses;
  bool last = false;
  while (!last && addresses.size() < 10) {
    if (offset + 7 > payload.size()) return {};
    QByteArray name;
    for (int i = 0; i < 6; ++i) {
      const unsigned char encoded = payload[offset + i];
      if (encoded & 1) return {};
      const char c = char(encoded >> 1);
      if (!(c == ' ' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return {};
      name += c;
    }
    name = name.trimmed();
    if (name.isEmpty() || name.contains(' ')) return {};
    const unsigned char flags = payload[offset + 6];
    const int ssid = (flags >> 1) & 15;
    if (ssid) name += '-' + QByteArray::number(ssid);
    if (addresses.size() >= 2 && (flags & 0x80)) name += '*';
    addresses.append(name); last = flags & 1; offset += 7;
  }
  if (!last || addresses.size() < 2 || offset + 2 > payload.size()
      || (static_cast<unsigned char>(payload[offset]) & 0xef) != 3
      || static_cast<unsigned char>(payload[offset + 1]) != 0xf0) return {};
  QByteArray packet = addresses[1] + '>' + addresses[0];
  for (int i = 2; i < addresses.size(); ++i) packet += ',' + addresses[i];
  return packet + ':' + payload.mid(offset + 2);
}
