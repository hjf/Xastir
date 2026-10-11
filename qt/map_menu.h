// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
class QMainWindow;
class MapWidget;
class TileSource;
class StationNavigator;
class MapMenu : public QObject
{
  Q_OBJECT
public:
  MapMenu(QMainWindow *window, MapWidget *map, TileSource *tiles, StationNavigator *navigator);
private:
  void coordinates(bool calculator);
  void bookmarks();
  void chooseMap();
  QMainWindow *window_;
  MapWidget *map_;
  TileSource *tiles_;
  StationNavigator *navigator_;
};
