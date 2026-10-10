// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "map_projection.h"
#include "database.h"
#include <QMap>
#include <QObject>
#include <optional>
#include <memory>
struct XastirStationDb;

enum class StationKind { Station, Object, Item };

// Decoded station or object state; packet transports do not need to know about the map widget.
struct Station
{
  QString callsign;
  map::Coordinate position{0, 0};
  QChar symbolTable = '/';
  QChar symbolCode = '-';
  std::optional<double> course;
  StationKind kind = StationKind::Station;
  bool emergency = false;
  QString speed{}, altitude{}, comment{};
  unsigned char ambiguity = 0;
  bool local = false;
  QString sourceStation{}; // Originator of an object/item report.
  QString powerGain{}, signalGain{}, bearing{}, nrq{}, path{};
  std::optional<WeatherRow> weather{};
  char dataVia = 'F';
  bool direct = false;
  qint64 heardAt = 0;
};

class StationModel : public QObject
{
  Q_OBJECT

public:
  explicit StationModel(QObject *parent = nullptr);
  ~StationModel() override;
  const XastirStationDb *database() const { return database_.get(); }
  const QMap<QString, Station> &stations() const { return stations_; }
  bool upsert(Station station, QString *error = nullptr);
  bool remove(const QString &name, StationKind kind = StationKind::Station);
  void clear();
  // A file is a snapshot. Validate every record before replacing existing state.
  bool loadFile(const QString &path, QString *error = nullptr);
  static bool validate(Station &station, QString *error);
  const DataRow *record(const QString &name) const;
  void clearTrails();
  void clearTactical();
  void setTactical(const QMap<QString, QString> &names);
  bool importTrail(const QList<Station> &points, QString *error = nullptr);
  void updateWeather(const QString &name, const WeatherRow &weather, char dataVia, bool direct);
  void setOwnCallsign(const QString &callsign) { ownCallsign_ = callsign; }

signals:
  void stationsChanged();

private:
  void refreshProjection();
  std::unique_ptr<XastirStationDb> database_;
  QMap<QString, Station> stations_; // Read-only Qt projection of the C database.
  QMap<QString, QString> tactical_;
  QString ownCallsign_;
};
