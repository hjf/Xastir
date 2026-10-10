// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
class QMainWindow;
class QDialog;
class QTableWidget;
class QTabWidget;
class QLabel;
class HistoryStore;
class HistoryUi : public QObject
{
  Q_OBJECT
public:
  HistoryUi(QMainWindow *, HistoryStore *, QString identity);
  void open();

private:
  void refresh(bool older = false);
  QMainWindow *window_;
  HistoryStore *store_;
  QString identity_;
  QPointer<QDialog> dialog_;
  QTableWidget *traffic_ = nullptr, *messages_ = nullptr;
  QTabWidget *tabs_ = nullptr;
  QLabel *status_ = nullptr;
  qint64 packetBefore_ = 0, messageBefore_ = 0, oldestPacket_ = 0, oldestMessage_ = 0;
};
