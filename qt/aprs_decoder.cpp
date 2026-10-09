// SPDX-License-Identifier: GPL-2.0-or-later
#include "aprs_decoder.h"
#include <cmath>
#include "aprs_position.h"

namespace {
std::optional<double> angle(QByteArray value, int degrees)
{
  if (value.size() != degrees + 6 || value[degrees + 2] != '.') return {};
  const char hemisphere = value.back();
  if (!(degrees == 2 ? QByteArray("NS") : QByteArray("EW")).contains(hemisphere)) return {};
  QByteArray digits = value.mid(degrees, 2) + value.mid(degrees + 3, 2);
  const int blank = digits.indexOf(' ');
  for (int i = 0; i < digits.size(); ++i) {
    if (blank >= 0 && i >= blank) { if (digits[i] != ' ') return {}; digits[i] = '0'; }
    else if (digits[i] < '0' || digits[i] > '9') return {};
  }
  if (blank == 0) digits = "3000";
  else if (blank > 0) digits[blank] = '5';
  for (int i = 0; i < degrees; ++i) if (value[i] < '0' || value[i] > '9') return {};
  const double minutes = digits.left(2).toInt() + digits.mid(2).toInt() / 100.0;
  const double result = value.left(degrees).toInt() + minutes / 60;
  if (minutes >= 60 || result > (degrees == 2 ? 90 : 180)) return {};
  return result * (hemisphere == 'S' || hemisphere == 'W' ? -1 : 1);
}
bool position(const QByteArray &data, Station &station)
{
  if (data.isEmpty() || data.contains('\0')) return false;
  QByteArray buffer = data;
  char *comment = buffer.data();
  XastirPosition decoded{};
  const bool uncompressed = data[0] >= '0' && data[0] <= '9';
  // Retain strict boundary/ambiguity validation before legacy conversion.
  if (uncompressed && (!angle(data.left(8), 2) || !angle(data.mid(9, 9), 3))) return false;
  const int result = uncompressed
      ? xastir_extract_position(&decoded, &comment, 0)
      : xastir_extract_comp_position(&decoded, &comment);
  if (!result) return false;
  station.position = {90 - decoded.coord_lat / 360000.0, decoded.coord_lon / 360000.0 - 180};
  station.symbolTable = QChar(decoded.aprs_symbol.special_overlay
      ? decoded.aprs_symbol.special_overlay : decoded.aprs_symbol.aprs_type);
  station.symbolCode = QChar(decoded.aprs_symbol.aprs_symbol);
  if (decoded.course[0]) station.course = QByteArray(decoded.course).toInt() % 360;
  else if (uncompressed && data.size() >= 26 && data[22] == '/') {
    bool ok; const int course = data.mid(19, 3).toInt(&ok);
    if (ok && course > 0 && course <= 360) station.course = course % 360;
  }
  return true;
}
bool micE(QByteArray destination, const QByteArray &data, Station &station)
{
  destination = destination.split('-').first();
  if (destination.size() != 6 || data.size() < 9) return false;
  QByteArray digits;
  for (char c : destination) {
    if (c >= '0' && c <= '9') digits += c;
    else if (c >= 'A' && c <= 'J') digits += char('0' + c - 'A');
    else if (c >= 'P' && c <= 'Y') digits += char('0' + c - 'P');
    else if (c == 'K' || c == 'L' || c == 'Z') digits += ' ';
    else return false;
  }
  const auto lat = angle(digits.left(4) + '.' + digits.mid(4) + (destination[3] >= 'P' ? 'N' : 'S'), 2);
  if (!lat) return false;
  int d = static_cast<unsigned char>(data[1]) - 28;
  int m = static_cast<unsigned char>(data[2]) - 28;
  int h = static_cast<unsigned char>(data[3]) - 28;
  if (d < 0 || d > 99 || m < 0 || m > 99 || h < 0 || h > 99) return false;
  if (destination[4] >= 'P') d += 100;
  if (d >= 180 && d <= 189) d -= 80;
  else if (d >= 190 && d <= 199) d -= 190;
  if (m >= 60) m -= 60;
  const int blank = digits.indexOf(' ');
  if (blank == 5) h = h / 10 * 10 + 5;
  else if (blank == 4) h = 50;
  else if (blank == 3) { m = m / 10 * 10 + 5; h = 0; }
  else if (blank == 2) { m = 30; h = 0; }
  if (d > 180 || m >= 60) return false;
  station.position = {*lat, (d + (m + h / 100.0) / 60) * (destination[5] >= 'P' ? -1 : 1)};
  station.symbolCode = QChar(data[7]); station.symbolTable = QChar(data[8]);
  const int second = static_cast<unsigned char>(data[5]) - 28;
  const int third = static_cast<unsigned char>(data[6]) - 28;
  if (second < 0 || second > 99 || third < 0 || third > 99) return false;
  int course = second % 10 * 100 + third;
  if (course >= 400) course -= 400;
  if (course <= 360) station.course = course % 360;
  return true;
}
}
std::optional<AprsUpdate> decodeAprs(const QByteArray &packet, int depth)
{
  if (depth > 4) return {};
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
  StationModel validator;
  if (!update.removed && !validator.upsert(update.station)) return {};
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
