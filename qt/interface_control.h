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
  void reportKissResult(const QString &);
signals:
  void configurationChanged(const XastirConfig &config);
  void kissStartRequested(int device);
  void kissStopRequested(int device);
  void kissParametersRequested(const KissSettings &settings);

private:
  QMainWindow *window_;
  XastirConfig config_;
  QPointer<QDialog> dialog_;
};
