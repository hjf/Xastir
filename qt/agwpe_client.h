// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xastir_config.h"
#include "radio_interface.h"
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

// AGWPE raw AX.25 monitor with explicitly enabled unproto message transmission.
class AgwpeClient : public RadioInterface {
  Q_OBJECT
public:
  AgwpeClient(AgwpeSettings settings, StationModel *stations, QObject *parent = nullptr);
  ~AgwpeClient() override;
  void start() override;
  void stop() override;
  bool transmit(const QByteArray &information, const QString &path = QString(), QString *error = nullptr) override;
  const AgwpeSettings &settings() const { return settings_; }
  bool connected() const override { return socket_.state() == QAbstractSocket::ConnectedState; }
  int deviceIndex() const override { return settings_.deviceIndex; }
  QString callsign() const override { return settings_.callsign; }
  QString path() const override { return settings_.path; }
  bool transmitEnabled() const override { return settings_.transmitEnabled; }
  QString transportName() const override { return QStringLiteral("AGWPE"); }
  QString description() const override;
  quint64 receivedPackets() const { return packets_; }
  quint64 positionUpdates() const { return updates_; }
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
