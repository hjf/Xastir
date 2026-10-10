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
  bool startCapture(const QString &path, QString *error = nullptr);
  void stopCapture();
  bool startReplay(const QString &path, QString *error = nullptr);
  void stopReplay();
  bool capturing() const { return capture_.isOpen(); }
  bool replaying() const { return replay_.isOpen(); }
  QStandardItemModel *model() const { return model_; }
public slots:
  void open();
signals:
  void replayPacket(const QByteArray &packet);
  void errorOccurred(const QString &error);
private:
  void append(const QByteArray &packet, int device, qint64 timestamp, bool replay);
  void replayNext();
  void updateStatus();
  QMainWindow *window_;
  QPointer<QDialog> dialog_;
  QStandardItemModel *model_;
  QLabel *status_ = nullptr;
  QFile capture_, replay_;
  QTimer timer_;
  quint64 received_ = 0, replayed_ = 0;
};
