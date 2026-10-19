// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "kml_export.h"
#include "xastir_config.h"
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
class QMainWindow;
class SnapshotControl;
class StationModel;
class QAction;
class KmlSnapshotControl : public QObject
{
  Q_OBJECT
public:
  KmlSnapshotControl(QMainWindow *, StationModel *, SnapshotControl *, const XastirConfig &);
  ~KmlSnapshotControl() override;
  bool enabled() const { return enabled_; }
  bool busy() const { return busy_; }
  void setEnabled(bool);
public slots:
  void captureNow();
signals:
  void saved(const QString &path);
  void errorOccurred(const QString &error);
private:
  void schedule();
  void collect();
  void fail(const QString &);
  QPointer<StationModel> stations_;
  QPointer<SnapshotControl> settings_;
  QAction *action_ = nullptr;
  QObject *worker_;
  QThread thread_;
  QTimer timer_, batch_;
  QByteArray after_;
  QList<KmlStation> records_;
  qsizetype points_ = 0;
  bool enabled_ = false, busy_ = false;
  quint64 generation_ = 0, captureGeneration_ = 0;
};
