// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xastir_config.h"
#include <QObject>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
class AprsisClient : public QObject
{
  Q_OBJECT
public:
  AprsisClient(AprsisSettings settings, QString callsign, StationModel *stations,
               QObject *parent = nullptr);
  ~AprsisClient() override;
  void start();
  void stop();
  bool connected() const { return socket_.state() == QAbstractSocket::ConnectedState; }
  bool canTransmit() const { return connected() && verified_ && settings_.transmitEnabled; }
  bool transmitEnabled() const { return settings_.transmitEnabled; }
  bool transmit(const QByteArray &, QString *error = nullptr);
  static bool validate(const AprsisSettings &, const QString &callsign, QString *error = nullptr);
signals:
  void packetTransmitted(const QByteArray &packet);
  void packetReceived(const QByteArray &packet);
  void statusChanged(const QString &status);

private:
  void connectToServer();
  void receive();
  void fail(const QString &reason);
  AprsisSettings settings_;
  QString callsign_;
  QPointer<StationModel> stations_;
  QTcpSocket socket_;
  QTimer retry_, timeout_;
  QByteArray input_;
  bool running_ = false, processing_ = false, loggedIn_ = false;
  bool verified_ = false;
  int retryDelay_ = 2000;
};
