// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
class QDialog;
class QMainWindow;
class StationModel;
class StationNavigator;
class StationTableModel;

class StationViews : public QObject
{
  Q_OBJECT
public:
  StationViews(QMainWindow *, StationModel *, StationNavigator *, QString ownCallsign);
signals:
  void detailsRequested(const QString &name);
private:
  void openList(int type, const QString &title);
  void refresh();
  void aloha();
  void uptime();
  QMainWindow *window_;
  QPointer<StationModel> stations_;
  StationNavigator *navigator_;
  StationTableModel *model_;
  QString own_;
  QPointer<QDialog> lists_[8], aloha_, uptime_;
  QTimer refreshTimer_;
  bool dirty_ = true;
  QElapsedTimer elapsed_;
  QDateTime startedAt_;
};
