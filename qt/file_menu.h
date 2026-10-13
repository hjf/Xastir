// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPixmap>
class QMainWindow;
class QWidget;
class QPrinter;

class FileMenu : public QObject
{
  Q_OBJECT
public:
  FileMenu(QMainWindow *window, QWidget *map);
  static bool printMap(QPrinter *printer, const QPixmap &image);
private:
  void preview();
  QMainWindow *window_;
  QWidget *map_;
};
