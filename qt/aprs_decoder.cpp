// SPDX-License-Identifier: GPL-2.0-or-later
#include "aprs_decoder.h"
#include <cmath>
#include "aprs_position.h"

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
  if (uncompressed) xastir_extract_speed_course(comment, decoded.speed, decoded.course);
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
