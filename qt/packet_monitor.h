// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QFile>
#include <QPointer>
#include <QTimer>
class QMainWindow;
class QDialog;
class QStandardItemModel;
class QLabel;
class PacketMonitor : public QObject {
  Q_OBJECT
public:
  explicit PacketMonitor(QMainWindow *window);
  void receive(const QByteArray &packet, int device);
  void recordTransmitted(const QByteArray &packet, int device);
  void setTrafficUtf8(bool enabled);
  bool trafficUtf8() const { return trafficUtf8_; }
  bool startCapture(const QString &path, QString *error = nullptr);
  void stopCapture();
  bool startReplay(const QString &path, QString *error = nullptr);
  void stopReplay();
  bool capturing() const { return capture_.isOpen(); }
  bool replaying() const { return replay_.isOpen(); }
  QStandardItemModel *model() const { return model_; }
public slots:
  void open();
  void openLogFile();
signals:
  void packetObserved(const QByteArray &packet, int device, qint64 timestamp, bool outgoing);
  void replayPacket(const QByteArray &packet, qint64 timestamp);
  void errorOccurred(const QString &error);
private:
  void append(const QByteArray &packet, int device, qint64 timestamp, bool replay, bool outgoing = false);
  void replayNext();
  void updateStatus();
  QMainWindow *window_;
  QPointer<QDialog> dialog_;
  QStandardItemModel *model_;
  QLabel *status_ = nullptr;
  QFile capture_, replay_;
  QTimer timer_;
  bool trafficUtf8_ = true;
  quint64 received_ = 0, replayed_ = 0;
};
