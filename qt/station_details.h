// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
class QMainWindow;
class StationModel;
class MapWidget;
class StationNavigator;
class MessageUi;
class QDialog;
class QLabel;
class StationDetails : public QObject
{
  Q_OBJECT
public:
  StationDetails(QMainWindow *, StationModel *, MapWidget *, StationNavigator *, MessageUi *,
                 QString ownCallsign);
  void open(const QString &name);

private:
  void refresh();
  QMainWindow *window_;
  StationModel *stations_;
  MapWidget *map_;
  StationNavigator *navigator_;
  MessageUi *messages_;
  QString own_, name_;
  QPointer<QDialog> dialog_;
  QLabel *text_ = nullptr;
};
