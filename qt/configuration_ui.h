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
class TrafficLogControl;

class ConfigurationUi : public QObject
{
  Q_OBJECT
public:
  ConfigurationUi(QMainWindow *, MapWidget *, PacketMonitor *, BeaconControl *, SnapshotControl *, TrafficLogControl *, const XastirConfig &);
private:
  void defaults();
  void timing();
  void coordinates();
  void applyPresentation(PresentationSettings value);
  bool save();
  QMainWindow *window_;
  MapWidget *map_;
  PacketMonitor *monitor_;
  BeaconControl *beacons_;
  SnapshotControl *snapshots_;
  TrafficLogControl *logs_;
  QPointer<QDialog> defaults_, timing_, coordinates_;
};
