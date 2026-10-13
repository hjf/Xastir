// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xastir_config.h"
#include <QObject>
#include <QThread>
#include <QTimer>
class QMainWindow;
class QWidget;
class QAction;

class SnapshotControl : public QObject
{
  Q_OBJECT
public:
  SnapshotControl(QMainWindow *, QWidget *map, const XastirConfig &);
  ~SnapshotControl() override;
  bool enabled() const { return enabled_; }
  bool busy() const { return busy_; }
  int intervalMinutes() const { return intervalMinutes_; }
  QString directory() const { return directory_; }
  void configure(const QString &directory, int intervalMinutes);
  void setEnabled(bool enabled);
public slots:
  void captureNow();
signals:
  void saved(const QString &path);
  void errorOccurred(const QString &error);
protected:
  bool eventFilter(QObject *, QEvent *) override;
private:
  void schedule();
  QMainWindow *window_;
  QWidget *map_;
  QAction *action_ = nullptr;
  QObject *worker_;
  QThread thread_;
  QTimer timer_;
  QString directory_;
  int intervalMinutes_ = 5;
  bool enabled_ = false, busy_ = false;
  quint64 generation_ = 0;
};
