// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "station_db.h"
#include <QList>
#include <QString>
struct KmlPoint { double latitude, longitude, altitude; bool newTrack = false; };
struct KmlStation {
  QString name, description;
  double latitude = 0, longitude = 0, altitude = 0;
  QList<KmlPoint> trail;
};
KmlStation copyKmlStation(const DataRow *, int maxTrailPoints = 0);
bool writeKmlFile(const QString &path, const QList<KmlStation> &, QString *error = nullptr, const QString &description = {});
