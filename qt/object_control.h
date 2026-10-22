// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xastir_config.h"
#include "object_core.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
class QMainWindow;
class QDialog;
class MapWidget;
class BeaconControl;
class RadioInterface;
class QTableWidget;
struct OwnedEntity {
  QString name, owner, comment, path;
  bool item = false, alive = true;
  map::Coordinate position{0, 0};
  QChar table = '/', symbol = '-';
  int device = -1, maximumSeconds = 1800;
  qint64 updatedAt = 0;
};
class ObjectControl : public QObject
{
  Q_OBJECT
public:
  using Interfaces = std::function<QList<RadioInterface *>()>;
  ObjectControl(QMainWindow *, StationModel *, MapWidget *, BeaconControl *, XastirConfig, Interfaces);
  void configure(const XastirConfig &);
  QList<OwnedEntity> definitions() const;
  QByteArray information(const OwnedEntity &, QString *error = nullptr) const;
  bool saveDefinition(OwnedEntity, QString *error = nullptr);
  bool sendNow(const QString &, QString *error = nullptr);
  bool kill(const QString &, QString *error = nullptr);
  bool setRepeat(const QString &, bool enabled, QString *error = nullptr);
  bool repeating(const QString &) const;
  bool forget(const QString &, QString *error = nullptr);
  void setClock(std::function<qint64()> clock) { clock_ = std::move(clock); }
public slots:
  void open();
  void process();
signals:
  void changed();
  void statusChanged(const QString &status);
private:
  struct State { OwnedEntity value; XastirObjectSchedule schedule{}; qint64 retryAfter = 0; QString status; };
  bool persist(QString *error);
  bool collision(const OwnedEntity &, QString *error) const;
  bool transmit(State &, const OwnedEntity &, QString *error);
  void project(const OwnedEntity &);
  void edit(const QString &name = {});
  void refresh();
  QMainWindow *window_;
  QPointer<StationModel> stations_;
  QPointer<MapWidget> map_;
  BeaconControl *beacons_;
  XastirConfig config_;
  Interfaces interfaces_;
  std::function<qint64()> clock_;
  QMap<QString, State> entries_;
  bool disabled_ = true;
  QTimer timer_;
  QPointer<QDialog> dialog_, editor_;
  QTableWidget *table_ = nullptr;
};
