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
  Selections selection{};
  What_to_display display{};
  auto flag = [&values](const char *key, int fallback) {
    const auto value = values.value(QString::fromLatin1(key)).trimmed();
    return value == "0" ? 0 : value == "1" ? 1 : fallback;
  };
  selection.mine = flag("DISPLAY_MY_STATION", 1);
  selection.tnc = flag("DISPLAY_TNC_STATIONS", 1);
  selection.direct = flag("DISPLAY_TNC_DIRECT_STATIONS", 1);
  selection.via_digi = flag("DISPLAY_TNC_VIADIGI_STATIONS", 1);
  selection.net = flag("DISPLAY_NET_STATIONS", 1);
  selection.tactical = flag("DISPLAY_TACTICAL_STATIONS", 0);
  selection.old_data = flag("DISPLAY_OLD_STATION_DATA", 0);
  selection.stations = flag("DISPLAY_STATIONS", 1);
  selection.fixed_stations = flag("DISPLAY_FIXED_STATIONS", 1);
  selection.moving_stations = flag("DISPLAY_MOVING_STATIONS", 1);
  selection.weather_stations = flag("DISPLAY_WEATHER_STATIONS", 1);
  selection.CWOP_wx_stations = flag("DISPLAY_CWOP_WX_STATIONS", 1);
  selection.objects = flag("DISPLAY_OBJECTS", 1);
  selection.weather_objects = flag("DISPLAY_STATION_WX_OBJ", 1);
  selection.gauge_objects = flag("DISPLAY_WATER_GAGE_OBJ", 1);
  selection.other_objects = flag("DISPLAY_OTHER_OBJECTS", 1);
  selection.aircraft_objects = flag("DISPLAY_AIRCRAFT_OBJECTS", 1);
  selection.vessel_objects = flag("DISPLAY_VESSEL_OBJECTS", 1);
  display.callsign = flag("DISPLAY_CALLSIGN", 1);
  display.label_all_trackpoints = flag("DISPLAY_LABEL_ALL_TRACKPOINTS", 0);
  display.symbol = flag("DISPLAY_SYMBOL", 1);
  display.symbol_rotate = flag("DISPLAY_SYMBOL_ROTATE", 1);
  display.phg = flag("DISPLAY_STATION_PHG", 0);
  display.default_phg = flag("DISPLAY_DEFAULT_PHG", 0);
  display.phg_of_moving = flag("DISPLAY_MOBILES_PHG", 0);
  display.altitude = flag("DISPLAY_ALTITUDE", 0);
  display.course = flag("DISPLAY_COURSE", 0);
  display.speed = flag("DISPLAY_SPEED", 1);
  display.speed_short = flag("DISPLAY_SPEED_SHORT", 0);
  display.dist_bearing = flag("DISPLAY_DIST_COURSE", 0);
  display.weather = flag("DISPLAY_WEATHER", 1);
  display.weather_text = flag("DISPLAY_STATION_WX", 1);
  display.temperature_only = flag("DISPLAY_TEMP_ONLY", 0);
  display.wind_barb = flag("DISPLAY_WIND_BARB", 1);
  display.trail = flag("DISPLAY_STATION_TRAILS", 1);
  display.last_heard = flag("DISPLAY_LAST_HEARD", 0);
  display.ambiguity = flag("DISPLAY_POSITION_AMB", 1);
  display.df_data = flag("DISPLAY_DF_INFO", 1);
  display.df_beamwidth_data = flag("DISPLAY_DF_BEAMWIDTH_INFO", 1);
  display.df_bearing_data = flag("DISPLAY_DF_BEARING_INFO", 0);
  display.dr_data = flag("DISPLAY_DEAD_RECKONING_INFO", 1);
  display.dr_arc = flag("DISPLAY_DEAD_RECKONING_ARC", 1);
  display.dr_course = flag("DISPLAY_DEAD_RECKONING_COURSE", 1);
  display.dr_symbol = flag("DISPLAY_DEAD_RECKONING_SYMBOL", 1);
  result.selection = selection; result.display = display;
  result.transmitDisabled = flag("DISABLE_TRANSMIT",0);
  result.positionTransmitDisabled = flag("DISABLE_POSIT_TX",1);
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
    station.comment = values.value("STATION_COMMENTS");
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
