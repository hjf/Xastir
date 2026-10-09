// SPDX-License-Identifier: GPL-2.0-or-later
#include "agwpe_client.h"
#include "aprs_decoder.h"
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {
QByteArray address(const QByteArray &call, bool last) {
  QByteArray result;
  for (char c : call.leftJustified(6, ' ')) result += char(c << 1);
  return result + char(0x60 | (last ? 1 : 0));
}
QByteArray raw(const QByteArray &info) {
  return QByteArray(1, '\0') + address("APRS", false) + address("TEST", true)
    + char(3) + char(0xf0) + info;
}
QByteArray frame(const QByteArray &payload, char kind = 'K') {
  QByteArray result(36, '\0'); result[4] = kind;
  qToLittleEndian<quint32>(quint32(payload.size()), result.data() + 28);
  return result + payload;
}
}
class TestAgwpe : public QObject {
  Q_OBJECT
private slots:
  void config();
  void positions();
  void objects();
  void ax25();
  void transport();
  void reconnect();
  void oversizedFrame();
};
void TestAgwpe::config() {
  QTemporaryDir dir;
  QFile file(dir.filePath("config")); QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("STATION_CALLSIGN:TEST-3\nSTATION_LAT:2726.543S\nSTATION_LONG:05859.431W\n"
    "STATION_GROUP:/\nSTATION_SYMBOL:-\nDEVICE0_TYPE:12\nDEVICE0_HOST:localhost\n"
    "DEVICE0_SPEED:8000\nDEVICE0_PASSWD:secret:colon\nDEVICE0_ONSTARTUP:1\nDEVICE0_RECONN:0\n"
    "DEVICE2_TYPE:5\nDEVICE2_HOST:must-not-connect\n"); file.close();
  XastirConfig config; QString error;
  QVERIFY2(readXastirConfig(file.fileName(), config, &error), qPrintable(error));
  QCOMPARE(config.callsign, "TEST-3"); QVERIFY(config.localStation);
  QVERIFY(std::abs(config.localStation->position.latitude + 27.4423833333) < 1e-8);
  QCOMPARE(config.agwpe.size(), 1); QVERIFY(config.agwpe[0].connectOnStartup);
  QVERIFY(!config.agwpe[0].reconnect); QCOMPARE(config.agwpe[0].password, QByteArray("secret:colon"));
  QVERIFY(file.open(QIODevice::Append)); file.write("DEVICE0_SPEED:99999\n"); file.close();
  QVERIFY(!readXastirConfig(file.fileName(), config, &error)); QVERIFY(!error.contains("secret"));
}
void TestAgwpe::positions() {
  auto update = decodeAprs("TEST>APRS:!4903.50N/07201.75W>090/010"); QVERIFY(update);
  QCOMPARE(update->station.callsign, "TEST"); QCOMPARE(update->station.symbolCode, QChar('>'));
  QVERIFY(std::abs(update->station.position.latitude - 49.0583333333) < 1e-8);
  QCOMPARE(*update->station.course, 90.0);
  update = decodeAprs("TEST>APRS:@092345z4903.50N/07201.75W-"); QVERIFY(update);
  update = decodeAprs("TEST>APRS:!4903.  N/07201.  W-"); QVERIFY(update);
  QVERIFY(std::abs(update->station.position.latitude - (49 + 3.5 / 60)) < 1e-8);
  update = decodeAprs("TEST>490SUP:`(_fn\"Oj/"); QVERIFY(update);
  QVERIFY(std::abs(update->station.position.longitude + 112.129) < 1e-8);
  QCOMPARE(*update->station.course, 251.0); QCOMPARE(update->station.symbolCode, QChar('j'));
  auto encode = [](int number) { QByteArray result(4, '!'); for (int i = 3; i >= 0; --i) { result[i] = char(33 + number % 91); number /= 91; } return result; };
  const QByteArray compressed = '/' + encode(int((90 - 49.0) * 380926))
    + encode(int((180 - 72.0) * 190463)) + ">7!9";
  update = decodeAprs("TEST>APRS:!" + compressed); QVERIFY(update);
  QVERIFY(std::abs(update->station.position.latitude - 49) < 1e-5);
  QVERIFY(std::abs(update->station.position.longitude + 72) < 1e-5);
  QCOMPARE(*update->station.course, 88.0);
  QVERIFY(!decodeAprs("TEST>APRS:!4963.50N/07201.75W-"));
  QVERIFY(decodeAprs("TEST>APRS:!4903.5 N/07201.75W-xxx"));
  QVERIFY(!decodeAprs("TEST>APRS:!490 .50N/07201.75W-"));
  QVERIFY(!decodeAprs("TEST>APRS:>status only"));
}
void TestAgwpe::objects() {
  auto update = decodeAprs("TEST>APRS:;OBJECT   *092345z4903.50N/07201.75W-");
  QVERIFY(update); QCOMPARE(update->station.callsign, "OBJECT"); QVERIFY(!update->removed);
  QCOMPARE(update->station.kind, StationKind::Object); QCOMPARE(update->station.sourceStation, "TEST");
  auto mixed = decodeAprs("TEST>APRS:;Aid Point*092345z4903.50N/07201.75Wh");
  QVERIFY(mixed); QCOMPARE(mixed->station.callsign, "Aid Point");
  StationModel model; QVERIFY(model.upsert(mixed->station));
  mixed = decodeAprs("TEST>APRS:;aid point*092345z4903.50N/07201.75Wh");
  QVERIFY(mixed); QVERIFY(model.upsert(mixed->station)); QCOMPARE(model.stations().size(), 2);
  QVERIFY(model.remove("Aid Point", StationKind::Object)); QVERIFY(model.stations().contains("aid point"));
  auto compressed = decodeAprs("TEST>APRS:;LEADER   *092345z/5L!!<*e7>7P[");
  QVERIFY(compressed); QCOMPARE(compressed->station.symbolCode, QChar('>'));
  update = decodeAprs("TEST>APRS:;OBJECT   _092345z"); QVERIFY(update); QVERIFY(update->removed);
  update = decodeAprs("TEST>APRS:)ITEM!4903.50N/07201.75W-"); QVERIFY(update); QCOMPARE(update->station.callsign, "ITEM");
  update = decodeAprs("TEST>APRS:)ITEM_"); QVERIFY(update); QVERIFY(update->removed);
  update = decodeAprs("GATE>APRS:}INNER>APRS:!4903.50N/07201.75W-"); QVERIFY(update); QCOMPARE(update->station.callsign, "INNER");
}
void TestAgwpe::ax25() {
  QCOMPARE(decodeAgwpeAx25(raw("!4903.50N/07201.75W-")), QByteArray("TEST>APRS:!4903.50N/07201.75W-"));
  QByteArray packet = raw("test"); packet[15] = char(0x13); QVERIFY(!decodeAgwpeAx25(packet).isEmpty());
  packet[15] = 0; QVERIFY(decodeAgwpeAx25(packet).isEmpty());
  QVERIFY(decodeAgwpeAx25(QByteArray(7, '\0')).isEmpty());
}
void TestAgwpe::transport() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  StationModel model; AgwpeSettings settings; settings.host = "127.0.0.1"; settings.port = server.serverPort();
  settings.callsign = "TEST-3"; settings.password = "password";
  AgwpeClient client(settings, &model); QSignalSpy packets(&client, &AgwpeClient::packetReceived);
  client.start(); QTRY_VERIFY(server.hasPendingConnections());
  auto *peer = server.nextPendingConnection(); QByteArray handshake;
  QTRY_VERIFY((handshake += peer->readAll()).size() == 654);
  QCOMPARE(handshake[4], 'P'); QCOMPARE(qFromLittleEndian<quint32>(handshake.constData() + 28), 510U);
  QCOMPARE(handshake.mid(36, 5), QByteArray("TEST\0", 5));
  QCOMPARE(handshake.mid(291, 9), QByteArray("password\0", 9));
  QCOMPARE(handshake[550], 'R'); QCOMPARE(handshake[586], 'G'); QCOMPARE(handshake[622], 'k');
  const auto message = frame(raw("!4903.50N/07201.75W>"));
  peer->write(message.left(17)); peer->flush(); QTest::qWait(20); QVERIFY(model.stations().isEmpty());
  peer->write(message.mid(17) + frame(raw("!4904.50N/07201.75W>")) + frame(QByteArray(8, '\0'), 'R'));
  QTRY_COMPARE(packets.size(), 2); QCOMPARE(model.stations().size(), 1);
  QVERIFY(std::abs(model.stations().first().position.latitude - (49 + 4.5 / 60)) < 1e-8);
  QCOMPARE(client.positionUpdates(), 2ULL);
  peer->write(frame(raw(";OBJECT   *092345z4903.50N/07201.75W-")));
  QTRY_COMPARE(model.stations().size(), 2);
  peer->write(frame(raw(";OBJECT   _092345z"))); QTRY_COMPARE(model.stations().size(), 1);
  client.stop(); QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
  QVERIFY(!server.hasPendingConnections());
}
void TestAgwpe::reconnect() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  StationModel model; AgwpeSettings settings; settings.host = "127.0.0.1"; settings.port = server.serverPort();
  AgwpeClient client(settings, &model); client.start(); QTRY_VERIFY(server.hasPendingConnections());
  auto *peer = server.nextPendingConnection(); peer->abort();
  QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000); client.stop();
}
void TestAgwpe::oversizedFrame() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  StationModel model; AgwpeSettings settings; settings.host = "127.0.0.1"; settings.port = server.serverPort(); settings.reconnect = false;
  AgwpeClient client(settings, &model); QSignalSpy status(&client, &AgwpeClient::statusChanged);
  client.start(); QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
  QByteArray header(36, '\0'); qToLittleEndian<quint32>(65537, header.data() + 28); peer->write(header);
  QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
  QVERIFY(status.last().first().toString().contains("Invalid AGWPE frame length")); QVERIFY(model.stations().isEmpty());
}
QTEST_GUILESS_MAIN(TestAgwpe)
#include "test_agwpe.moc"
