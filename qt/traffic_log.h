// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xastir_config.h"
#include <QObject>
#include <QThread>
#include <atomic>
#include <array>
class QMainWindow;
class QAction;
struct TrafficLogLimits {
  qint64 fileBytes = 8 * 1024 * 1024;
  qint64 categoryBytes = 32 * 1024 * 1024;
  int pendingRecords = 1024;
  qint64 pendingBytes = 8 * 1024 * 1024;
};
class TrafficLogControl : public QObject
{
  Q_OBJECT
public:
  enum Category { Tnc = 1, Internet = 2, Messages = 4, Weather = 8 };
  TrafficLogControl(QMainWindow *, const XastirConfig &, TrafficLogLimits = {});
  ~TrafficLogControl() override;
  QString directory() const { return directory_; }
  void configure(const QString &directory);
  bool enabled(Category category) const { return mask_ & category; }
  void setEnabled(Category, bool);
  void flush();
  quint64 dropped() const { return dropped_; }
  int pending() const { return pending_.load(); }
public slots:
  void observe(const QByteArray &packet, int device, qint64 timestamp, bool outgoing);
signals:
  void errorOccurred(const QString &error);
private:
  void write(const QByteArray &, int mask, const QString &, qint64 timestamp, quint64 generation);
  QString directory_;
  int mask_ = 0;
  quint64 generation_ = 0, dropped_ = 0;
  std::array<QAction *, 4> actions_{};
  QObject *worker_;
  QThread thread_;
  TrafficLogLimits limits_;
  std::atomic<int> pending_{0};
  std::atomic<qint64> pendingBytes_{0};
  // Only accessed by the worker, suppresses repeated failures in queued writes.
  quint64 workerGeneration_ = 0;
  int failedMask_ = 0;
};
