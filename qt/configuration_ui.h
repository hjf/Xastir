// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xastir_config.h"
#include <QObject>
#include <QPointer>
class QMainWindow;
class QDialog;
class MapWidget;
class PacketMonitor;
class BeaconControl;
class SnapshotControl;

class ConfigurationUi : public QObject
{
  Q_OBJECT
public:
  ConfigurationUi(QMainWindow *, MapWidget *, PacketMonitor *, BeaconControl *, SnapshotControl *, const XastirConfig &);
private:
  void defaults();
  void timing();
  bool save();
  QMainWindow *window_;
  MapWidget *map_;
  PacketMonitor *monitor_;
  BeaconControl *beacons_;
  SnapshotControl *snapshots_;
  QPointer<QDialog> defaults_, timing_;
};
