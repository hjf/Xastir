// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "map_projection.h"
#include "tile_source.h"
#include "xastir_config.h"
#include "history_store.h"
#include <QMainWindow>

class MapWidget;
class StationModel;
class MessageService;
class BeaconControl;
class AprsisClient;
class QLabel;
class MessageUi; class PacketMonitor; class AgwpeClient; class StationMenu;

struct ApplicationOptions
{
  map::Coordinate center{20.0, 0.0};
  int zoom = 3;
  TileSourceOptions tiles;
  bool tileUrlExplicit = false;
  XastirConfig config;
  bool historyEnabled = false;
  HistoryOptions history;
};

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(const ApplicationOptions &options, QWidget *parent = nullptr);
  HistoryStore *historyStore() const { return history_; }
  StationModel *stationModel() const { return stations_; }
  MessageService *messageService() const { return messages_; }
  bool loadStations(const QString &path, QString *error = nullptr, bool centerOnStations = false);

private:
  HistoryStore *history_ = nullptr;
  void configureAgwpe(const XastirConfig &config);
  BeaconControl *beacons_ = nullptr;
  bool transmitDisabled_ = false;
  AprsisClient *aprsis_ = nullptr;
  QLabel *aprsisLabel_ = nullptr;
  QList<AgwpeClient *> clients_;
  QList<QLabel *> interfaceLabels_;
  MessageUi *messageUi_ = nullptr;
  PacketMonitor *monitor_;
  StationMenu *stationMenu_;
  MapWidget *mapWidget_;
  StationModel *stations_;
  MessageService *messages_;
  std::optional<Station> localStation_;
};
