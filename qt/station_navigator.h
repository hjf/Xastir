// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "station_model.h"
#include <QPointer>
class QMainWindow;
class MapWidget;
class QDialog;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class StationNavigator : public QObject {
  Q_OBJECT
public:
  StationNavigator(QMainWindow *window, StationModel *stations, MapWidget *map);
  bool locate(const QString &name, bool follow);
  QString followedName() const { return followed_; }
  void stopFollowing();
  void open(bool track = false);
signals:
  void followingChanged(const QString &name);
private:
  void refresh();
  void updateFollowing();
  QMainWindow *window_;
  StationModel *stations_;
  MapWidget *map_;
  QPointer<QDialog> dialog_;
  QLineEdit *filter_ = nullptr;
  QListWidget *list_ = nullptr;
  QLabel *status_ = nullptr;
  QPushButton *center_ = nullptr, *follow_ = nullptr;
  QString followed_;
  StationKind followedKind_ = StationKind::Station;
};
