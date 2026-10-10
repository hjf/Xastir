// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "interface_control.h"
#include "agwpe_client.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
class ConnectionTest : public QObject {
  Q_OBJECT
  QTemporaryDir settings_;
private slots:
  void initTestCase() {QCoreApplication::setOrganizationName("XastirConnectionsTests");QCoreApplication::setApplicationName("Connections");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings_.path());}
  void init() {QSettings().clear();}
  void configureInterface() {
    ApplicationOptions options;options.config.callsign="LOCAL";AgwpeSettings device;device.callsign="LOCAL";device.host="localhost";device.connectOnStartup=false;options.config.agwpe.append(device);
    MainWindow window(options);window.findChild<QAction *>("PULDNTNT04")->trigger();
    auto *dialog=window.findChild<QDialog *>("interfaceControl");QVERIFY(dialog);
    auto *table=dialog->findChild<QTableWidget *>("agwpeInterfaces");QCOMPARE(table->rowCount(),1);
    table->item(0,1)->setText("192.0.2.1");table->item(0,3)->setText("2");
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    XastirConfig saved;readQtConfiguration(saved);QCOMPARE(saved.callsign,QString("LOCAL"));QCOMPARE(saved.agwpe.size(),1);QCOMPARE(saved.agwpe.first().host,QString("192.0.2.1"));QCOMPARE(saved.agwpe.first().radioPort,quint32(1));QVERIFY(!saved.agwpe.first().transmitEnabled);
    const auto clients=window.findChildren<AgwpeClient *>();QCOMPARE(clients.size(),1);QCOMPARE(clients.first()->settings().host,QString("192.0.2.1"));QVERIFY(!clients.first()->connected());QVERIFY(saved.localStation);
  }
};
QTEST_MAIN(ConnectionTest)
#include "test_connections.moc"
