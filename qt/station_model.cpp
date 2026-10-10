// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_model.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <cmath>
#include <QDateTime>
#include "station_db.h"

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
  station.callsign = station.callsign.trimmed();
  if (station.kind == StationKind::Station)
    station.callsign = station.callsign.toUpper();
  station.sourceStation = station.sourceStation.trimmed().toUpper();
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

namespace {
bool store(XastirStationDb *database, const Station &station)
{
  const auto name = station.callsign.toUtf8(), source = station.sourceStation.toUtf8();
  const auto speed = station.speed.toLatin1(), altitude = station.altitude.toLatin1(), comment = station.comment.toLatin1();
  const XastirStationReport report{name.constData(), source.constData(),
    station.position.latitude, station.position.longitude, station.course.value_or(0),
    int(station.kind), bool(station.course), station.emergency,
    station.symbolTable.toLatin1(), station.symbolCode.toLatin1(),
    speed.constData(), altitude.constData(), comment.constData(), station.ambiguity, station.local};
  return xastir_station_update(database, &report, QDateTime::currentSecsSinceEpoch());
}
}
StationModel::StationModel(QObject *parent) : QObject(parent), database_(std::make_unique<XastirStationDb>())
{
  xastir_station_db_init(database_.get(), QDateTime::currentSecsSinceEpoch());
}
StationModel::~StationModel() { xastir_delete_all_stations(database_.get()); }
void StationModel::refreshProjection()
{
  stations_.clear();
  for (const DataRow *row = database_->first; row; row = row->n_next) {
    if (!(row->flag & ST_ACTIVE) || !row->position_valid) continue;
    Station station;
    station.callsign = QString::fromUtf8(row->call_sign);
    station.position = {row->latitude, row->longitude};
    station.symbolCode = QChar(row->aprs_symbol.aprs_symbol);
    station.symbolTable = QChar(row->aprs_symbol.special_overlay
        ? row->aprs_symbol.special_overlay : row->aprs_symbol.aprs_type);
    if (row->heading_valid) station.course = row->heading;
    station.kind = row->flag & ST_OBJECT ? StationKind::Object : row->flag & ST_ITEM ? StationKind::Item : StationKind::Station;
    station.sourceStation = QString::fromUtf8(row->origin);
    station.local = row->flag & ST_MYSTATION;
    station.emergency = row->emergency;
    station.speed = QString::fromLatin1(row->speed);
    station.altitude = QString::fromLatin1(row->altitude);
    station.ambiguity = row->pos_amb;
    if (row->comment_data) station.comment = QString::fromLatin1(row->comment_data->text_ptr);
    stations_.insert(station.callsign, station);
  }
}
bool StationModel::upsert(Station station, QString *error)
{
  if (!validate(station, error)) return false;
  if (!store(database_.get(), station)) return fail(error, tr("Cannot store station in the APRS database."));
  // A configured local position has priority over an RF echo of that callsign.
  const auto name = station.callsign.toUtf8(); DataRow *row = nullptr;
  xastir_search_station_name(database_.get(), &row, const_cast<char *>(name.constData()), 1);
  if (row && (row->flag & ST_MYSTATION) && !station.local) return true;
  stations_.insert(station.callsign, station);
  emit stationsChanged();
  return true;
}
bool StationModel::remove(const QString &name, StationKind kind)
{
  const auto key = (kind == StationKind::Station ? name.trimmed().toUpper() : name.trimmed()).toUtf8();
  DataRow *row = nullptr;
  if (!xastir_search_station_name(database_.get(), &row, const_cast<char *>(key.constData()), 1)) return false;
  xastir_station_del_ptr(database_.get(), row);
  stations_.remove(QString::fromUtf8(key));
  emit stationsChanged();
  return true;
}
void StationModel::clear()
{
  if (!database_->count) return;
  xastir_delete_all_stations(database_.get());
  refreshProjection();
  emit stationsChanged();
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
    const QString kind = object.value("kind").toString("station");
    if (kind == "object") station.kind = StationKind::Object;
    else if (kind == "item") station.kind = StationKind::Item;
    else if (kind != "station")
      return fail(error, tr("Record %1: kind must be station, object, or item").arg(index + 1));
    if (object.contains("kind") && !object.value("kind").isString())
      return fail(error, tr("Record %1: kind must be a string").arg(index + 1));
    if (object.contains("source") && !object.value("source").isString())
      return fail(error, tr("Record %1: source must be a string").arg(index + 1));
    station.sourceStation = object.value("source").toString();
    if (object.contains("course"))
      station.course = object.value("course").toDouble();
    QString reason;
    if (!validate(station, &reason))
      return fail(error, tr("Station %1: %2").arg(index + 1).arg(reason));
    next.insert(station.callsign, station);
  }
  auto replacement = std::make_unique<XastirStationDb>();
  xastir_station_db_init(replacement.get(), QDateTime::currentSecsSinceEpoch());
  for (const auto &station : next) {
    if (!store(replacement.get(), station)) {
      xastir_delete_all_stations(replacement.get());
      return fail(error, tr("Cannot store station snapshot in the APRS database."));
    }
  }
  xastir_delete_all_stations(database_.get());
  database_ = std::move(replacement);
  refreshProjection();
  emit stationsChanged();
  return true;
}
