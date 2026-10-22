// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "radio_interface.h"
#include "xastir_config.h"
#include "kiss_core.h"
#include <QPointer>
#include <QSerialPort>
#include <QTimer>
class KissClient : public RadioInterface
{
  Q_OBJECT
public:
  KissClient(KissSettings, StationModel *, QObject *parent = nullptr);
  ~KissClient() override;
  static bool validate(const KissSettings &, QString *error = nullptr);
  const KissSettings &settings() const { return settings_; }
  void start() override;
  void stop() override;
  bool connected() const override { return serial_.isOpen(); }
  int deviceIndex() const override { return settings_.deviceIndex; }
  QString callsign() const override { return settings_.callsign; }
  QString path() const override { return settings_.path; }
  bool transmitEnabled() const override { return settings_.transmitEnabled; }
  QString transportName() const override { return QStringLiteral("serial KISS"); }
  QString description() const override;
  bool transmit(const QByteArray &, const QString &path = QString(), QString *error = nullptr) override;
  void setTransmitBlocked(bool) override;
  // Explicitly queue stored parameters; never called on connection/startup.
  bool applyParameters(QString *error = nullptr) { return applyParameters(settings_, error); }
  bool applyParameters(const KissSettings &, QString *error = nullptr);
  qsizetype pendingBytes() const;
  quint64 receivedPackets() const { return packets_; }
  quint64 rejectedFrames() const { return rejected_; }
private:
  void open();
  void fail(const QString &);
  void receive();
  void pump();
  void discardOutput();
  struct Output { QByteArray bytes; qsizetype offset = 0; };
  KissSettings settings_;
  QPointer<StationModel> stations_;
  QSerialPort serial_;
  QTimer retry_, reader_, writer_;
  XastirKissDecoder decoder_{};
  QList<Output> output_;
  qsizetype queuedBytes_ = 0;
  bool running_ = false, processing_ = false;
  int retryDelay_ = 2000;
  quint64 packets_ = 0, rejected_ = 0;
};
