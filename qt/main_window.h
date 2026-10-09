// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "map_projection.h"
#include "tile_source.h"
#include <QMainWindow>

class MapWidget;
class StationModel;

struct ApplicationOptions
{
  map::Coordinate center{20.0, 0.0};
  int zoom = 3;
  TileSourceOptions tiles;
};

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(const ApplicationOptions &options, QWidget *parent = nullptr);
  StationModel *stationModel() const { return stations_; }
  bool loadStations(const QString &path, QString *error = nullptr, bool centerOnStations = false);

private:
  MapWidget *mapWidget_;
  StationModel *stations_;
};
