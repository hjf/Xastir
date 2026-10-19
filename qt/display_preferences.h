// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "map_projection.h"
#include "station_view.h"
#include <QObject>
struct PresentationSettings {
  int coordinateSystem = 0; // Legacy 0 decimal degrees, 1 degrees/minutes, 2 DMS.
  bool englishUnits = false;
  bool distanceBearing = false;
};
class DisplayPreferences : public QObject
{
  Q_OBJECT
public:
  explicit DisplayPreferences(QObject *parent = nullptr);
  const PresentationSettings &value() const { return value_; }
  void restore(const PresentationSettings &defaults);
  bool save(const PresentationSettings &value);
  void setValue(PresentationSettings value);
  QString latitude(double degrees) const;
  QString longitude(double degrees) const;
  QString position(map::Coordinate coordinate) const;
  double convert(double value, XastirDisplayQuantity quantity) const;
  QString unit(XastirDisplayQuantity quantity) const;
  QString measure(double value, XastirDisplayQuantity quantity, int precision = 1) const;
  QString distance(double meters, int precision = 2, bool adaptive = false) const;
signals:
  void changed();
private:
  QString angle(double value, bool latitude) const;
  PresentationSettings value_;
};
