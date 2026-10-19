// SPDX-License-Identifier: GPL-2.0-or-later
#include "snapshot_control.h"
#include <QAction>
#include <QDir>
#include <QEvent>
#include <QImage>
#include <QImageWriter>
#include <QMainWindow>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>

SnapshotControl::SnapshotControl(QMainWindow *window, QWidget *map, const XastirConfig &config)
  : QObject(window), window_(window), map_(map), worker_(new QObject)
{
  QSettings settings;
  const auto fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/snapshots";
  directory_ = settings.value("snapshots/directory", fallback).toString().trimmed();
  if (directory_.isEmpty()) directory_ = fallback;
  bool ok = false;
  const int interval = settings.value("snapshots/intervalMinutes", config.snapshotIntervalMinutes).toInt(&ok);
  intervalMinutes_ = ok && interval >= 1 && interval <= 30 ? interval : config.snapshotIntervalMinutes;
  enabled_ = settings.value("snapshots/enabled", config.snapshotsEnabled).toBool();
  worker_->moveToThread(&thread_);
  connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
  thread_.start();
  timer_.setParent(this);
  timer_.setObjectName("pngSnapshotTimer");
  connect(&timer_, &QTimer::timeout, this, &SnapshotControl::captureNow);
  connect(this, &SnapshotControl::saved, window_, [window](const QString &path) {
    window->statusBar()->showMessage(tr("PNG snapshot saved: %1").arg(path), 5000);
  });
  connect(this, &SnapshotControl::errorOccurred, window_, [window](const QString &error) {
    window->statusBar()->showMessage(error, 30000);
  });
  action_ = window->findChild<QAction *>("PULDNFI014");
  if (action_) {
    disconnect(action_, nullptr, window, nullptr);
    action_->setProperty("implemented", true);
    action_->setChecked(enabled_);
    action_->setToolTip(tr("Refresh snapshot.png in the directory configured under File → Configure → Timing."));
    connect(action_, &QAction::triggered, this, &SnapshotControl::setEnabled);
  }
  map_->installEventFilter(this);
  schedule();
  if (enabled_) QTimer::singleShot(0, this, &SnapshotControl::captureNow);
}

SnapshotControl::~SnapshotControl()
{
  timer_.stop();
  // Finish the single outstanding write before destroying its callback target.
  QMetaObject::invokeMethod(worker_, [] {}, Qt::BlockingQueuedConnection);
  thread_.quit();
  thread_.wait();
}

void SnapshotControl::schedule()
{
  timer_.stop();
  if (enabled_) timer_.start(intervalMinutes_ * 60000);
}

void SnapshotControl::configure(const QString &directory, int intervalMinutes)
{
  ++generation_;
  directory_ = directory.trimmed();
  intervalMinutes_ = qBound(1, intervalMinutes, 30);
  QSettings settings;
  settings.setValue("snapshots/directory", directory_);
  settings.setValue("snapshots/intervalMinutes", intervalMinutes_);
  schedule();
  emit configurationChanged();
}

void SnapshotControl::setEnabled(bool enabled)
{
  ++generation_;
  enabled_ = enabled;
  QSettings().setValue("snapshots/enabled", enabled);
  if (action_) { QSignalBlocker blocker(action_); action_->setChecked(enabled); }
  schedule();
  if (enabled_) captureNow();
}

bool SnapshotControl::eventFilter(QObject *object, QEvent *event)
{
  if (object == map_ && event->type() == QEvent::Show && enabled_)
    QTimer::singleShot(0, this, &SnapshotControl::captureNow);
  return QObject::eventFilter(object, event);
}

void SnapshotControl::captureNow()
{
  if (!enabled_ || busy_ || !map_->isVisible()) return;
  const QImage image = map_->grab().toImage();
  const auto directory = directory_;
  const auto generation = generation_;
  busy_ = true;
  QMetaObject::invokeMethod(worker_, [this, image, directory, generation] {
    const QString path = QDir(directory).filePath("snapshot.png");
    QString error;
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
      error = tr("Could not create snapshot directory: %1").arg(directory);
    } else {
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly)) error = file.errorString();
      else {
        QImageWriter writer(&file, "png");
        if (!writer.write(image)) error = writer.errorString();
        else if (!file.commit()) error = file.errorString();
      }
    }
    QMetaObject::invokeMethod(this, [this, path, error, generation] {
      busy_ = false;
      if (generation != generation_) {
        // A directory change or off/on toggle supersedes this write. Refresh
        // the current destination without allowing the old result to disable it.
        if (enabled_) captureNow();
        return;
      }
      if (error.isEmpty()) emit saved(path);
      else {
        setEnabled(false);
        emit errorOccurred(tr("PNG snapshots disabled: %1 (%2)").arg(error, path));
      }
    }, Qt::QueuedConnection);
  }, Qt::QueuedConnection);
}
