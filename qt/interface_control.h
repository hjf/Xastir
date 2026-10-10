// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xastir_config.h"
#include <QObject>
#include <QPointer>
class QMainWindow;
class QDialog;
void readQtConfiguration(XastirConfig &config);
class InterfaceControl : public QObject
{
  Q_OBJECT
public:
  InterfaceControl(QMainWindow *, XastirConfig config);
  void open();
signals:
  void configurationChanged(const XastirConfig &config);

private:
  QMainWindow *window_;
  XastirConfig config_;
  QPointer<QDialog> dialog_;
};
