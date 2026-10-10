// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "interface_control.h"
#include "agwpe_client.h"
#include "aprsis_client.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
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
  void internetReception() {
    QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));StationModel stations;
    AprsisSettings settings;settings.host="127.0.0.1";settings.port=server.serverPort();settings.filter="r/49/-72/100";settings.reconnect=false;
    AprsisClient client(settings,"LOCAL",&stations);QSignalSpy packets(&client,&AprsisClient::packetReceived);
    client.start();QTRY_VERIFY(server.hasPendingConnections());auto *peer=server.nextPendingConnection();peer->write("# test server\r");QTest::qWait(10);QCOMPARE(peer->bytesAvailable(),qint64(0));peer->write("\n");
    QTRY_VERIFY(peer->canReadLine());const auto login=peer->readLine();QVERIFY(login.contains("user LOCAL pass -1"));QVERIFY(login.contains("filter r/49/-72/100\r\n"));
    peer->write("# logresp LOCAL unverified\r\nREMOTE>APRS,qAR,GATE:!4903.50N/07201.75W>hello");QTest::qWait(10);QVERIFY(!stations.record("REMOTE"));peer->write("\r\n");QTRY_VERIFY(stations.record("REMOTE"));QCOMPARE(stations.record("REMOTE")->data_via,char('I'));QVERIFY(!(stations.record("REMOTE")->flag&ST_VIATNC));QCOMPARE(packets.size(),1);
    peer->write("OWNER>APRS,qAR,GATE:;Aid Point*092345z4903.50N/07201.75Wh\r\n");QTRY_VERIFY(stations.record("Aid Point"));QVERIFY(stations.record("Aid Point")->flag&ST_OBJECT);
    QCOMPARE(peer->bytesAvailable(),qint64(0)); // Receive-only client emits no APRS frames or RF traffic.
    peer->write(QByteArray(512,'x'));QTRY_VERIFY(!client.connected());client.stop();
  }
  void invalidInternetLogin() {
    AprsisSettings settings;QString error;QVERIFY(!AprsisClient::validate(settings,"BAD\nCALL",&error));settings.filter="r/0/0/50\r\nuser OTHER";QVERIFY(!AprsisClient::validate(settings,"LOCAL",&error));
  }
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
