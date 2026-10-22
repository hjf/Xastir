// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
class RadioInterface : public QObject
{
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual bool connected() const = 0;
  virtual int deviceIndex() const = 0;
  virtual QString callsign() const = 0;
  virtual QString path() const = 0;
  virtual bool transmitEnabled() const = 0;
  virtual QString description() const = 0;
  virtual QString transportName() const = 0;
  virtual bool transmit(const QByteArray &, const QString &path = QString(), QString *error = nullptr) = 0;
  virtual void setTransmitBlocked(bool value) { blocked_ = value; }
signals:
  void statusChanged(const QString &status);
  void packetTransmitted(const QByteArray &packet);
  void packetReceived(const QByteArray &packet);
protected:
  QByteArray prepareFrame(const QByteArray &, const QString &, QString *error) const;
  static QByteArray tnc2(const QByteArray &frame);
  bool blocked_ = false;
};
