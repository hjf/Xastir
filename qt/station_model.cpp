// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_model.h"
#include "aprs_decoder.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <cmath>
#include <QDateTime>
#include "station_db.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>

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
  if (!xastir_station_update(database, &report, station.heardAt ? station.heardAt : QDateTime::currentSecsSinceEpoch())) return false;
  DataRow *row = nullptr;
  xastir_search_station_name(database, &row, const_cast<char *>(name.constData()), 1);
  if (!row || ((row->flag & ST_MYSTATION) && !station.local)) return true;
  row->data_via = station.dataVia;
  if (station.dataVia == 'T') row->flag |= ST_VIATNC;
  if (station.direct) { row->flag |= ST_DIRECT; row->direct_heard = row->sec_heard; }
  if (station.weather && xastir_get_weather_record(row)) *row->weather_data = *station.weather;
  auto copy = [](char *out, size_t size, const QString &value) {
    const auto bytes = value.toLatin1(); std::snprintf(out, size, "%s", bytes.constData());
  };
  if (!station.powerGain.isEmpty()) copy(row->power_gain, sizeof(row->power_gain), station.powerGain);
  if (!station.signalGain.isEmpty()) copy(row->signal_gain, sizeof(row->signal_gain), station.signalGain);
  if (!station.bearing.isEmpty()) copy(row->bearing, sizeof(row->bearing), station.bearing);
  if (!station.nrq.isEmpty()) copy(row->NRQ, sizeof(row->NRQ), station.nrq);
  const auto path = station.path.toLatin1();
  if (!path.isEmpty()) {
    auto *replacement = static_cast<char *>(std::malloc(size_t(path.size()) + 1));
    if (replacement) { std::memcpy(replacement, path.constData(), size_t(path.size()) + 1); std::free(row->node_path_ptr); row->node_path_ptr = replacement; }
  }
  return true;
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
    station.powerGain = QString::fromLatin1(row->power_gain);
    station.signalGain = QString::fromLatin1(row->signal_gain);
    station.bearing = QString::fromLatin1(row->bearing);
    station.nrq = QString::fromLatin1(row->NRQ);
    station.path = QString::fromLatin1(row->node_path_ptr ? row->node_path_ptr : "");
    station.dataVia = row->data_via; station.direct = row->flag & ST_DIRECT;
    station.heardAt = row->sec_heard;
    if (row->weather_data) station.weather = *row->weather_data;
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
  if (row && station.kind != StationKind::Station && station.sourceStation == ownCallsign_ && !ownCallsign_.isEmpty()) row->flag |= ST_MYOBJITEM;
  else if (row) row->flag &= ~ST_MYOBJITEM;
  if (row && tactical_.contains(station.callsign)) {
    std::free(row->tactical_call_sign); row->tactical_call_sign = nullptr;
    const auto name = tactical_.value(station.callsign).toUtf8();
    if (!name.isEmpty()) {
      row->tactical_call_sign = static_cast<char *>(std::malloc(size_t(name.size()) + 1));
      if (row->tactical_call_sign) std::memcpy(row->tactical_call_sign, name.constData(), size_t(name.size()) + 1);
    }
  }
  stations_.insert(station.callsign, station);
  emit stationsChanged();
  return true;
}
const DataRow *StationModel::record(const QString &name) const
{
  const auto bytes = name.toUtf8(); DataRow *row = nullptr;
  return xastir_search_station_name(database_.get(), &row, const_cast<char *>(bytes.constData()), 1) ? row : nullptr;
}
void StationModel::clearTrails()
{
  for (DataRow *row = database_->first; row; row = row->n_next) xastir_delete_trail(database_.get(), row);
  emit stationsChanged();
}
bool StationModel::importTrail(const QList<Station> &points, QString *error)
{
  if (points.isEmpty()) return fail(error, tr("No trail positions were supplied."));
  const QString name = points.first().callsign;
  QList<Station> checked = points;
  for (auto &point : checked)
    if (!validate(point, error) || point.callsign != name || point.kind != StationKind::Station || point.heardAt <= 0)
      return fail(error, tr("Invalid station trail."));
  std::stable_sort(checked.begin(), checked.end(), [](const Station &a, const Station &b) { return a.heardAt < b.heardAt; });
  auto *row = const_cast<DataRow *>(record(name));
  if (row && (row->flag & (ST_OBJECT | ST_ITEM))) return fail(error, tr("That name belongs to an object or item."));
  if (!row || (checked.last().heardAt >= row->sec_heard && !(row->flag & ST_MYSTATION))) {
    if (!upsert(checked.last(), error)) return false;
    row = const_cast<DataRow *>(record(name));
  }
  struct Point { long lon, lat; time_t sec; QString altitude, speed, course; };
  QList<Point> combined;
  for (const TrackRow *p = row->oldest_trackpoint; p; p = p->next)
    combined.append({p->trail_long_pos,p->trail_lat_pos,p->sec,p->altitude == -99999 ? QString() : QString::number(p->altitude/10.0),
                     p->speed < 0 ? QString() : QString::number(p->speed/18.52),p->course < 0 ? QString() : QString::number(p->course)});
  for (const auto &point : checked)
    combined.append({long(std::llround((180+point.position.longitude)*360000)),long(std::llround((90-point.position.latitude)*360000)),time_t(point.heardAt),
                     point.altitude,point.speed,point.course ? QString::number(*point.course) : QString()});
  std::stable_sort(combined.begin(),combined.end(),[](const Point &a,const Point &b){return a.sec < b.sec;});
  combined.erase(std::unique(combined.begin(),combined.end(),[](const Point &a,const Point &b){return a.sec==b.sec && a.lon==b.lon && a.lat==b.lat;}),combined.end());
  if (combined.size()>5000) combined=combined.last(5000);
  xastir_delete_trail(database_.get(),row);
  for (const auto &p : combined) {
    auto altitude=p.altitude.toLatin1(),speed=p.speed.toLatin1(),course=p.course.toLatin1();
    if (!xastir_store_trail_point(database_.get(),row,p.lon,p.lat,p.sec,altitude.data(),speed.data(),course.data(),0))
      return fail(error,tr("Cannot allocate trail storage."));
  }
  row->flag |= ST_MOVING;
  emit stationsChanged(); return true;
}
void StationModel::clearTactical() { setTactical({}); }
void StationModel::updateWeather(const QString &name, const WeatherRow &weather, char dataVia, bool direct, qint64 heardAt)
{
  Station check; check.callsign = name;
  if (!validate(check,nullptr)) return;
  const auto bytes = check.callsign.toUtf8();
  auto *row = xastir_station_upsert(database_.get(),bytes.constData(),heardAt ? heardAt : QDateTime::currentSecsSinceEpoch());
  if (!row || !xastir_get_weather_record(row)) return;
  *row->weather_data = weather; row->data_via = dataVia;
  row->flag |= ST_ACTIVE;
  if (dataVia == 'T') row->flag |= ST_VIATNC;
  if (direct) { row->flag |= ST_DIRECT; row->direct_heard = row->sec_heard; }
  ++row->num_packets;
  refreshProjection(); emit stationsChanged();
}
void StationModel::setTactical(const QMap<QString, QString> &names)
{
  tactical_ = names;
  for (DataRow *row = database_->first; row; row = row->n_next) {
    std::free(row->tactical_call_sign); row->tactical_call_sign = nullptr;
    const auto text = names.value(QString::fromUtf8(row->call_sign)).toUtf8();
    if (text.isEmpty()) continue;
    row->tactical_call_sign = static_cast<char *>(std::malloc(size_t(text.size()) + 1));
    if (row->tactical_call_sign) std::memcpy(row->tactical_call_sign, text.constData(), size_t(text.size()) + 1);
  }
  emit stationsChanged();
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

void StationModel::applyPacket(const QByteArray &packet,char dataVia,qint64 heardAt)
{
  const auto update=decodeAprs(packet);
  if(update) {
    auto station=update->station;
    station.dataVia=dataVia;station.heardAt=heardAt;
    if(dataVia=='I')station.direct=false;
    if(station.weather && heardAt) station.weather->wx_sec_time=heardAt;
    if(update->removed)remove(station.callsign,station.kind);
    else upsert(station);
  } else {
    const auto weather=decodeAprsWeather(packet);
    if(weather) {auto value=weather->weather;if(heardAt)value.wx_sec_time=heardAt;
      updateWeather(weather->callsign,value,dataVia,dataVia=='I'?false:weather->direct,heardAt);}
  }
}
