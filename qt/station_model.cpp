// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_model.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <cmath>

namespace
{
bool fail(QString *error, const QString &message)
{
  if (error)
    *error = message;
  return false;
}
}

bool StationModel::validate(Station &station, QString *error)
{
  station.callsign = station.callsign.trimmed().toUpper();
  if (station.callsign.isEmpty() || station.callsign.size() > 32)
    return fail(error, tr("Station name must contain 1–32 characters"));
  for (const QChar character : station.callsign)
    if (!character.isPrint())
      return fail(error, tr("Station name contains a control character"));
  if (!std::isfinite(station.position.latitude) || std::abs(station.position.latitude) > 90
      || !std::isfinite(station.position.longitude) || std::abs(station.position.longitude) > 180)
    return fail(error, tr("Station coordinates are outside the latitude/longitude limits"));
  const ushort table = station.symbolTable.unicode();
  if (table != '/' && table != '\\' && !(table >= '0' && table <= '9')
      && !(table >= 'A' && table <= 'Z'))
    return fail(error, tr("Symbol table must be /, \\, or an overlay character 0–9/A–Z"));
  if (station.symbolCode.unicode() < '!' || station.symbolCode.unicode() > '~')
    return fail(error, tr("Symbol code must be a printable APRS symbol character"));
  if (station.course && (!std::isfinite(*station.course) || *station.course < 0 || *station.course >= 360))
    return fail(error, tr("Course must be between 0 (inclusive) and 360 (exclusive) degrees"));
  return true;
}

bool StationModel::upsert(Station station, QString *error)
{
  if (!validate(station, error))
    return false;
  stations_.insert(station.callsign, station);
  emit stationsChanged();
  return true;
}

bool StationModel::remove(const QString &callsign)
{
  if (!stations_.remove(callsign.trimmed().toUpper()))
    return false;
  emit stationsChanged();
  return true;
}

void StationModel::clear()
{
  if (!stations_.isEmpty())
  {
    stations_.clear();
    emit stationsChanged();
  }
}

bool StationModel::loadFile(const QString &path, QString *error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return fail(error, tr("Cannot open %1: %2").arg(path, file.errorString()));
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError)
    return fail(error, tr("Invalid station JSON: %1").arg(parseError.errorString()));
  if (!document.isArray())
    return fail(error, tr("Station file must contain a JSON array"));

  QMap<QString, Station> next;
  const QJsonArray records = document.array();
  for (qsizetype index = 0; index < records.size(); ++index)
  {
    const auto object = records[index].toObject();
    const QString symbol = object.value("symbol").toString();
    if (!records[index].isObject() || !object.value("callsign").isString()
        || !object.value("latitude").isDouble() || !object.value("longitude").isDouble()
        || symbol.size() != 2
        || (object.contains("course") && !object.value("course").isDouble()))
      return fail(error, tr("Station %1 needs a callsign, numeric latitude/longitude, a two-character symbol, and an optional numeric course").arg(index + 1));
    Station station{object.value("callsign").toString(),
                    {object.value("latitude").toDouble(), object.value("longitude").toDouble()},
                    symbol[0], symbol[1], std::nullopt};
    if (object.contains("course"))
      station.course = object.value("course").toDouble();
    QString reason;
    if (!validate(station, &reason))
      return fail(error, tr("Station %1: %2").arg(index + 1).arg(reason));
    next.insert(station.callsign, station);
  }
  stations_ = std::move(next);
  emit stationsChanged();
  return true;
}
