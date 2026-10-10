// SPDX-License-Identifier: GPL-2.0-or-later
#include "agwpe_client.h"
#include "aprs_decoder.h"
#include "aprsis_client.h"
#include "beacon_control.h"
#include "interface_control.h"
#include "main_window.h"
#include "message_service.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
class ConnectionTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settings_;
private slots:
  void initTestCase()
  {
    QCoreApplication::setOrganizationName("XastirConnectionsTests");
    QCoreApplication::setApplicationName("Connections");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
  }
  void init() { QSettings().clear(); }
  void positionBeaconAndTransmitControls()
  {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ApplicationOptions options;
    options.historyEnabled = true;
    options.history.path = settings_.filePath("beacon-history.sqlite");
    options.config.callsign = "LOCAL";
    Station local;
    local.callsign = "LOCAL";
    local.position = {49 + 3.5 / 60, -(72 + 1.75 / 60)};
    local.symbolCode = '>';
    local.comment = "Qt beacon";
    options.config.localStation = local;
    AgwpeSettings device;
    device.deviceIndex = 3;
    device.host = "127.0.0.1";
    device.port = server.serverPort();
    device.callsign = "LOCAL";
    device.transmitEnabled = true;
    device.radioPort = 1;
    device.path = "WIDE1-1";
    options.config.agwpe.append(device);
    MainWindow window(options);
    QTRY_VERIFY(server.hasPendingConnections());
    auto *peer = server.nextPendingConnection();
    QTRY_VERIFY(peer->bytesAvailable() >= 108);
    peer->readAll();
    auto *beacon = window.findChild<BeaconControl *>();
    QVERIFY(beacon);
    QString error;
    QVERIFY(!beacon->sendNow(3, &error));
    QVERIFY(error.contains("Position transmission is disabled"));
    QCOMPARE(peer->bytesAvailable(), qint64(0));
    window.findChild<QAction *>("PULDNTNT05")->trigger();
    QVERIFY(beacon->sendNow(3, &error));
    QTRY_VERIFY(peer->bytesAvailable() >= 36);
    QByteArray frame = peer->readAll();
    const int payloadSize = qFromLittleEndian<quint32>(frame.constData() + 28);
    while (frame.size() < 36 + payloadSize)
    {
      QVERIFY(peer->waitForReadyRead(1000));
      frame += peer->readAll();
    }
    QCOMPARE(frame[4], char('V'));
    QCOMPARE(qFromLittleEndian<quint32>(frame.constData()), quint32(1));
    QCOMPARE(frame.mid(8, 5), QByteArray("LOCAL"));
    QCOMPARE(frame.mid(18, 6), QByteArray("APXQT1"));
    QCOMPARE(frame.mid(37, 7), QByteArray("WIDE1-1"));
    const auto info = frame.mid(47);
    QCOMPARE(info, QByteArray("=4903.50N/07201.75W>Qt beacon"));
    window.historyStore()->flush();
    const auto history = window.historyStore()->packets();
    QCOMPARE(history.size(), 1);
    QVERIFY(history.first().outgoing);
    QCOMPARE(history.first().packet, QByteArray("LOCAL>APXQT1,WIDE1-1:") + info);
    const auto decoded = decodeAprs("LOCAL>APXQT1:" + info);
    QVERIFY(decoded);
    QCOMPARE(decoded->station.comment, QString("Qt beacon"));
    window.findChild<QAction *>("PULDNTNT03")->trigger();
    QVERIFY(!beacon->sendNow(3, &error));
    QVERIFY(!window.messageService()->sendQuery("?APRS?", 3, &error));
    QVERIFY(error.contains("All transmission"));
    QCOMPARE(peer->bytesAvailable(), qint64(0));
    window.findChild<QAction *>("PULDNTNT03")->trigger();
    window.findChild<QAction *>("PULDNTNT05")->trigger();
    QVERIFY(!beacon->sendNow(3, &error));
    window.findChild<QAction *>("PULDNTNT01")->trigger();
    auto *dialog = window.findChild<QDialog *>("positionBeacon");
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
  }
  void internetReception()
  {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    StationModel stations;
    AprsisSettings settings;
    settings.host = "127.0.0.1";
    settings.port = server.serverPort();
    settings.filter = "r/49/-72/100";
    settings.reconnect = false;
    AprsisClient client(settings, "LOCAL", &stations);
    QSignalSpy packets(&client, &AprsisClient::packetReceived);
    client.start();
    QTRY_VERIFY(server.hasPendingConnections());
    auto *peer = server.nextPendingConnection();
    peer->write("# test server\r");
    QTest::qWait(10);
    QCOMPARE(peer->bytesAvailable(), qint64(0));
    peer->write("\n");
    QTRY_VERIFY(peer->canReadLine());
    const auto login = peer->readLine();
    QVERIFY(login.contains("user LOCAL pass -1"));
    QVERIFY(login.contains("filter r/49/-72/100\r\n"));
    peer->write("# logresp LOCAL unverified\r\nREMOTE>APRS,qAR,GATE:!4903.50N/07201.75W>hello");
    QTest::qWait(10);
    QVERIFY(!stations.record("REMOTE"));
    peer->write("\r\n");
    QTRY_VERIFY(stations.record("REMOTE"));
    QCOMPARE(stations.record("REMOTE")->data_via, char('I'));
    QVERIFY(!(stations.record("REMOTE")->flag & ST_VIATNC));
    QCOMPARE(packets.size(), 1);
    peer->write("OWNER>APRS,qAR,GATE:;Aid Point*092345z4903.50N/07201.75Wh\r\n");
    QTRY_VERIFY(stations.record("Aid Point"));
    QVERIFY(stations.record("Aid Point")->flag & ST_OBJECT);
    QCOMPARE(peer->bytesAvailable(),
             qint64(0)); // Receive-only client emits no APRS frames or RF traffic.
    peer->write(QByteArray(512, 'x'));
    QTRY_VERIFY(!client.connected());
    client.stop();
  }
  void internetMessagesAndAcks()
  {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ApplicationOptions options;
    options.config.callsign = "LU3GHF-3";
    options.config.aprsis.enabled = true;
    options.config.aprsis.host = "127.0.0.1";
    options.config.aprsis.port = server.serverPort();
    options.config.aprsis.reconnect = false;
    options.config.aprsis.transmitEnabled = true;
    options.config.aprsis.passcode = "12345";
    MainWindow window(options);
    QTRY_VERIFY(server.hasPendingConnections());
    auto *peer = server.nextPendingConnection();
    peer->write("# test server\r\n");
    QTRY_VERIFY(peer->canReadLine());
    QVERIFY(peer->readLine().contains("user LU3GHF-3 pass 12345"));
    auto *client = window.findChild<AprsisClient *>();
    QVERIFY(client);
    QString error;
    QVERIFY(!client->transmit(":LU3GHF-5 :ack1", &error));
    peer->write("# logresp LU3GHF-3 unverified, server TEST\r\n");
    QTest::qWait(20);
    QVERIFY(!client->canTransmit());
    peer->write("LU3GHF-5>APDR16,TCPIP*,qAC,T2ROMANIA::LU3GHF-3 :aprs{1\r\n");
    QTRY_COMPARE(window.messageService()->messages().size(), 1);
    auto *dialog = window.findChild<QDialog *>("messageConversation");
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    auto *interfaces = dialog->findChild<QComboBox *>("messageInterface");
    QVERIFY(interfaces);
    QCOMPARE(interfaces->currentData().toInt(), 256);
    QCOMPARE(peer->bytesAvailable(), qint64(0));
    peer->write("# logresp LU3GHF-3 verified, server TEST\r\n");
    QTRY_VERIFY(client->canTransmit());
    peer->write("LU3GHF-5>APDR16,TCPIP*,qAC,T2ROMANIA::LU3GHF-3 :second{2\r\n");
    QTRY_VERIFY(peer->canReadLine());
    QCOMPARE(peer->readLine(), QByteArray("LU3GHF-3>APXQT1,TCPIP*::LU3GHF-5 :ack2\r\n"));
    QCOMPARE(window.messageService()->messages().size(), 2);
    dialog->findChild<QLineEdit *>("messageText")->setText("reply");
    dialog->findChild<QPushButton *>("sendMessageButton")->click();
    QTRY_VERIFY(peer->canReadLine());
    const auto outgoing = peer->readLine();
    QVERIFY(outgoing.startsWith("LU3GHF-3>APXQT1,TCPIP*::LU3GHF-5 :reply{"));
    const auto id = window.messageService()->messages().last().id;
    peer->write("LU3GHF-5>APDR16,TCPIP*::LU3GHF-3 :ack" + id.toLatin1() + "\r\n");
    QTRY_COMPARE(window.messageService()->messages().last().status, QString("Acknowledged"));
    window.findChild<QAction *>("PULDNTNT03")->trigger();
    peer->write("LU3GHF-5>APDR16,TCPIP*::LU3GHF-3 :blocked{3\r\n");
    QTRY_COMPARE(window.messageService()->messages().size(), 4);
    QCOMPARE(peer->bytesAvailable(), qint64(0));
    window.findChild<QAction *>("PULDNTNT03")->trigger();
    QVERIFY(!client->transmit(":LU3GHF-5 :bad\r\n", &error));
  }
  void invalidInternetLogin()
  {
    AprsisSettings settings;
    QString error;
    QVERIFY(!AprsisClient::validate(settings, "BAD\nCALL", &error));
    settings.filter = "r/0/0/50\r\nuser OTHER";
    QVERIFY(!AprsisClient::validate(settings, "LOCAL", &error));
  }
  void configureInterface()
  {
    ApplicationOptions options;
    options.config.callsign = "LOCAL";
    AgwpeSettings device;
    device.callsign = "LOCAL";
    device.host = "localhost";
    device.connectOnStartup = false;
    options.config.agwpe.append(device);
    MainWindow window(options);
    window.findChild<QAction *>("PULDNTNT04")->trigger();
    auto *dialog = window.findChild<QDialog *>("interfaceControl");
    QVERIFY(dialog);
    auto *table = dialog->findChild<QTableWidget *>("agwpeInterfaces");
    QCOMPARE(table->rowCount(), 1);
    table->item(0, 1)->setText("192.0.2.1");
    table->item(0, 3)->setText("2");
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    XastirConfig saved;
    readQtConfiguration(saved);
    QCOMPARE(saved.callsign, QString("LOCAL"));
    QCOMPARE(saved.agwpe.size(), 1);
    QCOMPARE(saved.agwpe.first().host, QString("192.0.2.1"));
    QCOMPARE(saved.agwpe.first().radioPort, quint32(1));
    QVERIFY(!saved.agwpe.first().transmitEnabled);
    const auto clients = window.findChildren<AgwpeClient *>();
    QCOMPARE(clients.size(), 1);
    QCOMPARE(clients.first()->settings().host, QString("192.0.2.1"));
    QVERIFY(!clients.first()->connected());
    QVERIFY(saved.localStation);
  }
};
QTEST_MAIN(ConnectionTest)
#include "test_connections.moc"
