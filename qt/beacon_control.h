// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xastir_config.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
class AgwpeClient;
class QMainWindow;
class QDialog;
class QLabel;
class QComboBox;
class BeaconControl : public QObject
{
  Q_OBJECT
public:
  using Interfaces = std::function<QList<AgwpeClient *>()>;
  BeaconControl(QMainWindow *, XastirConfig, Interfaces);
  void configure(const XastirConfig &);
  QByteArray information(QString *error = nullptr) const;
  bool sendNow(int device, QString *error = nullptr);
  void open();
  bool transmitDisabled() const { return allDisabled_; }
  bool positionDisabled() const { return positionDisabled_; }
signals:
  void transmitDisabledChanged(bool disabled);
  void statusChanged(const QString &status);

private:
  void schedule();
  void refresh();
  QMainWindow *window_;
  XastirConfig config_;
  Interfaces interfaces_;
  QTimer timer_;
  bool allDisabled_ = false, positionDisabled_ = true, periodic_ = false;
  int interval_ = 300, device_ = -1;
  QPointer<QDialog> dialog_;
  QLabel *preview_ = nullptr, *status_ = nullptr;
  QComboBox *devices_ = nullptr;
};
