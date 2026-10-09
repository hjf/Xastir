// SPDX-License-Identifier: GPL-2.0-or-later
#include "xastir_config.h"
#include <QFile>
#include <QRegularExpression>
#include <cmath>

bool readXastirConfig(const QString &path, XastirConfig &config, QString *error)
{
  QFile file(path);
  auto fail = [error](const QString &message) { if (error) *error = message; return false; };
  if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Cannot read configuration: %1").arg(path));
  QMap<QString, QString> values;
  while (!file.atEnd()) {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const auto colon = line.indexOf(':');
    if (!line.startsWith('#') && colon > 0) values.insert(line.left(colon), line.mid(colon + 1));
  }
  XastirConfig result;
  result.autoReplyText = values.value("AUTO_MSG_REPLY");
  result.callsign = values.value("STATION_CALLSIGN").trimmed().toUpper();
  auto angle = [](const QString &value, int degrees) -> std::optional<double> {
    const QRegularExpression pattern(QStringLiteral("^(\\d{%1})(\\d{2}\\.\\d+)([NSEW])$").arg(degrees));
    const auto match = pattern.match(value);
    if (!match.hasMatch()) return {};
    const double d = match.captured(1).toDouble(), m = match.captured(2).toDouble();
    const QChar hemisphere = match.captured(3).at(0);
    if (m >= 60 || d + m / 60 > (degrees == 2 ? 90 : 180)
        || !(degrees == 2 ? QStringLiteral("NS") : QStringLiteral("EW")).contains(hemisphere)) return {};
    return (d + m / 60) * (hemisphere == 'S' || hemisphere == 'W' ? -1 : 1);
  };
  const auto lat = angle(values.value("STATION_LAT"), 2), lon = angle(values.value("STATION_LONG"), 3);
  if (lat && lon && !result.callsign.isEmpty()) {
    Station station;
    station.callsign = result.callsign;
    station.position = {*lat, *lon};
    if (values.value("STATION_GROUP", "/").isEmpty() || values.value("STATION_SYMBOL", "-").isEmpty())
      return fail(QStringLiteral("Missing local station symbol."));
    station.symbolTable = values.value("STATION_GROUP", "/").left(1).at(0);
    station.symbolCode = values.value("STATION_SYMBOL", "-").left(1).at(0);
    StationModel validator;
    if (!validator.upsert(station)) return fail(QStringLiteral("Invalid local station in configuration."));
    result.localStation = station;
  }
  const QRegularExpression devicePattern(QStringLiteral("^DEVICE(\\d+)_TYPE$"));
  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    const auto match = devicePattern.match(it.key());
    if (!match.hasMatch() || it.value() != "12") continue;
    AgwpeSettings settings;
    settings.deviceIndex = match.captured(1).toInt();
    const QString prefix = QStringLiteral("DEVICE%1_").arg(settings.deviceIndex);
    settings.host = values.value(prefix + "HOST").trimmed();
    bool ok;
    const int port = values.value(prefix + "SPEED", "8000").toInt(&ok);
    if (!ok || port < 1 || port > 65535 || settings.host.isEmpty())
      return fail(QStringLiteral("Invalid AGWPE host or TCP port for device %1.").arg(settings.deviceIndex));
    settings.port = quint16(port);
    settings.callsign = result.callsign;
    settings.password = values.value(prefix + "PASSWD").toUtf8();
    settings.connectOnStartup = values.value(prefix + "ONSTARTUP", "0") == "1";
    settings.reconnect = values.value(prefix + "RECONN", "1") == "1";
    settings.transmitEnabled = values.value(prefix + "TXMT", "0") == "1"
      && values.value("DISABLE_TRANSMIT", "0") != "1";
    const int radioPort = values.value(prefix + "FILTER_PARAMS", "1").toInt(&ok);
    if (!ok || radioPort < 1 || radioPort > 256)
      return fail(QStringLiteral("Invalid AGWPE radio port for device %1.").arg(settings.deviceIndex));
    settings.radioPort = quint32(radioPort - 1);
    settings.path = values.value(prefix + "UNPROTO1").trimmed().toUpper();
    result.agwpe.append(settings);
  }
  config = result;
  return true;
}
