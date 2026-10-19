// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
class QMainWindow;
class QDialog;
class QStandardItemModel;
class QTableView;
class MessageService;
class BulletinUi : public QObject
{
  Q_OBJECT
public:
  BulletinUi(QMainWindow *, MessageService *);
private:
  void open();
  void refresh();
  QMainWindow *window_;
  QPointer<MessageService> service_;
  QPointer<QDialog> dialog_;
  QStandardItemModel *model_ = nullptr;
  QTableView *table_ = nullptr;
  QTimer timer_;
  bool dirty_ = true;
};
