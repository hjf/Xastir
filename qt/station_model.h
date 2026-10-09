// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "map_projection.h"
#include <QMap>
#include <QObject>
#include <optional>

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
  QString sourceStation{}; // Originator of an object/item report.
};

class StationModel : public QObject
{
  Q_OBJECT

public:
  using QObject::QObject;
  const QMap<QString, Station> &stations() const { return stations_; }
  bool upsert(Station station, QString *error = nullptr);
  bool remove(const QString &name, StationKind kind = StationKind::Station);
  void clear();
  // A file is a snapshot. Validate every record before replacing existing state.
  bool loadFile(const QString &path, QString *error = nullptr);

signals:
  void stationsChanged();

private:
  static bool validate(Station &station, QString *error);
  QMap<QString, Station> stations_;
};
