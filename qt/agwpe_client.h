// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xastir_config.h"
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

// AGWPE raw AX.25 monitor with explicitly enabled unproto message transmission.
class AgwpeClient : public QObject {
  Q_OBJECT
public:
  AgwpeClient(AgwpeSettings settings, StationModel *stations, QObject *parent = nullptr);
  ~AgwpeClient() override;
  void start();
  void stop();
  bool transmit(const QByteArray &information, const QString &path = QString(), QString *error = nullptr);
  const AgwpeSettings &settings() const { return settings_; }
  bool connected() const { return socket_.state() == QAbstractSocket::ConnectedState; }
  quint64 receivedPackets() const { return packets_; }
  quint64 positionUpdates() const { return updates_; }
signals:
  void statusChanged(const QString &status);
  void packetTransmitted(const QByteArray &packet);
  void packetReceived(const QByteArray &packet);
private:
  void connectToServer();
  void fail(const QString &reason);
  void receive();
  void send(char kind, const QByteArray &payload = {});
  AgwpeSettings settings_;
  QPointer<StationModel> stations_;
  QTcpSocket socket_;
  QTimer retry_, timeout_;
  QByteArray input_;
  bool running_ = false;
  bool processing_ = false;
  int retryDelay_ = 2000;
  quint64 packets_ = 0, updates_ = 0;
};
