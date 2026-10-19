// SPDX-License-Identifier: GPL-2.0-or-later
#include "kml_snapshot.h"
#include "snapshot_control.h"
#include "station_model.h"
#include <QAction>
#include <QDir>
#include <QMainWindow>
#include <QSettings>
#include <QStatusBar>

KmlSnapshotControl::KmlSnapshotControl(QMainWindow *window, StationModel *stations, SnapshotControl *settings, const XastirConfig &config)
  : QObject(window), stations_(stations), settings_(settings), worker_(new QObject), timer_(this), batch_(this)
{
  enabled_ = QSettings().value("snapshots/kmlEnabled", config.kmlSnapshotsEnabled).toBool();
  worker_->moveToThread(&thread_); connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater); thread_.start();
  timer_.setObjectName("kmlSnapshotTimer"); batch_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, &KmlSnapshotControl::captureNow);
  connect(&batch_, &QTimer::timeout, this, &KmlSnapshotControl::collect);
  connect(settings, &SnapshotControl::configurationChanged, this, [this] { ++generation_; schedule(); });
  connect(this, &KmlSnapshotControl::saved, window, [window](const QString &path) { window->statusBar()->showMessage(tr("KML snapshot saved: %1").arg(path), 5000); });
  connect(this, &KmlSnapshotControl::errorOccurred, window, [window](const QString &error) { window->statusBar()->showMessage(error, 30000); });
  action_ = window->findChild<QAction *>("PULDNFI016");
  if (action_) {
    disconnect(action_, nullptr, window, nullptr); action_->setProperty("implemented", true); action_->setChecked(enabled_);
    action_->setToolTip(tr("Refresh snapshot.kml using the snapshot directory and interval in Configure → Timing."));
    connect(action_, &QAction::triggered, this, &KmlSnapshotControl::setEnabled);
  }
  schedule(); if (enabled_) QTimer::singleShot(0, this, &KmlSnapshotControl::captureNow);
}
KmlSnapshotControl::~KmlSnapshotControl()
{
  timer_.stop(); batch_.stop();
  QMetaObject::invokeMethod(worker_, [] {}, Qt::BlockingQueuedConnection); thread_.quit(); thread_.wait();
}
void KmlSnapshotControl::schedule()
{
  timer_.stop(); if (enabled_ && settings_) timer_.start(settings_->intervalMinutes() * 60000);
}
void KmlSnapshotControl::setEnabled(bool value)
{
  ++generation_; enabled_ = value; QSettings().setValue("snapshots/kmlEnabled", value);
  if (action_) action_->setChecked(value);
  schedule(); if (value) captureNow();
}
void KmlSnapshotControl::captureNow()
{
  if (!enabled_ || busy_ || !stations_ || !settings_) return;
  busy_ = true; records_.clear(); after_.clear(); points_ = 0; captureGeneration_ = generation_; batch_.start(0);
}
void KmlSnapshotControl::fail(const QString &error)
{
  busy_ = false; records_.clear(); setEnabled(false); emit errorOccurred(tr("KML snapshots disabled: %1").arg(error));
}
void KmlSnapshotControl::collect()
{
  if (!enabled_ || !stations_ || !settings_ || captureGeneration_ != generation_) {
    busy_ = false; records_.clear(); if (enabled_) captureNow(); return;
  }
  auto *db = const_cast<XastirStationDb *>(stations_->database());
  DataRow *row = db->first;
  if (!after_.isEmpty() && xastir_search_station_name(db, &row, after_.data(), 1)) row = row->n_next;
  int remaining = 16;
  while (row && remaining-- > 0) {
    after_ = row->call_sign;
    if ((row->flag & ST_ACTIVE) && row->position_valid) {
      auto copy = copyKmlStation(row, 1000); points_ += copy.trail.size(); records_.append(std::move(copy));
      if (records_.size() > 10000 || points_ > 250000) { fail(tr("Snapshot exceeds 10,000 stations or 250,000 trail points; use manual KML export.")); return; }
    }
    row = row->n_next;
  }
  if (row) { batch_.start(1); return; }
  const auto records = std::move(records_);
  const auto directory = settings_->directory(); const auto generation = generation_;
  QMetaObject::invokeMethod(worker_, [this, records, directory, generation] {
    const auto path = QDir(directory).filePath("snapshot.kml"); QString error;
    if (directory.isEmpty() || !QDir().mkpath(directory)) error = tr("Could not create snapshot directory: %1").arg(directory);
    else writeKmlFile(path, records, &error, tr("Periodic snapshot; up to the most recent 1000 trail points per station."));
    QMetaObject::invokeMethod(this, [this, path, error, generation] {
      busy_ = false;
      if (generation != generation_) { if (enabled_) captureNow(); return; }
      if (!error.isEmpty()) fail(error); else emit saved(path);
    }, Qt::QueuedConnection);
  }, Qt::QueuedConnection);
}
