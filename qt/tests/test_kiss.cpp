// SPDX-License-Identifier: GPL-2.0-or-later
#include "kiss_client.h"
#include "ax25_core.h"
#include "beacon_control.h"
#include "interface_control.h"
#include "main_window.h"
#include "message_service.h"
#include "object_control.h"
#include "packet_monitor.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <fcntl.h>
#include <pty.h>
#include <unistd.h>

namespace {
// A real serial adapter talks to a pseudo-terminal; no physical ports are opened.
struct Pty {
  int master = -1;
  QString path;
  Pty() {
    int slave; char name[256];
    if (openpty(&master, &slave, name, nullptr, nullptr) == 0) {
      path = QString::fromLocal8Bit(name); close(slave);
      fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    }
  }
  void disconnect() { if (master >= 0) { close(master); master = -1; } }
  ~Pty() { disconnect(); }
  QByteArray drain() {
    QByteArray result; char bytes[4096]; ssize_t n;
    while ((n = read(master, bytes, sizeof(bytes))) > 0) result.append(bytes, n);
    return result;
  }
  bool send(const QByteArray &bytes) { return write(master, bytes.constData(), size_t(bytes.size())) == bytes.size(); }
};
KissSettings settings(const Pty &pty, bool tx = false) {
  KissSettings s; s.portName = pty.path; s.callsign = "LOCAL"; s.deviceIndex = 3;
  s.radioPort = 12; s.reconnect = false; s.transmitEnabled = tx; return s;
}
// Fixed AX.25 addresses from the wire, deliberately independent of the encoder.
const QByteArray incomingAddress = QByteArray::fromHex("82a0a4a64040609c60868298986703f0"); // N0CALL-3>APRS
const QByteArray outgoingAddress = QByteArray::fromHex("82a0b0a2a86260989e868298406103f0"); // LOCAL>APXQT1
QByteArray wire(const QByteArray &ax25, unsigned channel = 12, unsigned command = 0) {
  QByteArray data(1, char(channel * 16 + command)); data += ax25;
  data.replace(QByteArray(1, char(0xdb)), QByteArray::fromHex("dbdd"));
  data.replace(QByteArray(1, char(0xc0)), QByteArray::fromHex("dbdc"));
  return QByteArray(1, char(0xc0)) + data + QByteArray(1, char(0xc0));
}
QByteArray information(const QByteArray &bytes) {
  XastirKissDecoder decoder; xastir_kiss_init(&decoder); XastirKissFrame frame;
  for (unsigned char c : bytes) if (xastir_kiss_decode_byte(&decoder, c, &frame) == 1 && frame.command == 0) {
    QByteArray packet(4224, Qt::Uninitialized);
    auto n = xastir_ax25_to_tnc2(frame.data, frame.length, reinterpret_cast<unsigned char *>(packet.data()), packet.size());
    packet.resize(n); return packet.mid(packet.indexOf(':') + 1);
  }
  return {};
}
ApplicationOptions options(const Pty &pty, bool tx = true) {
  ApplicationOptions o; o.config.callsign = "LOCAL"; o.config.kiss.append(settings(pty, tx));
  o.config.kiss[0].connectOnStartup = true;
  Station local; local.callsign = "LOCAL"; local.position = {49+3.5/60, -(72+1.75/60)}; local.symbolCode = '>';
  o.config.localStation = local; return o;
}
}
class KissTest : public QObject {
  Q_OBJECT
  QTemporaryDir dir_;
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("XastirKissTests"); QCoreApplication::setApplicationName("Serial");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir_.path());
  }
  void init() { QSettings().clear(); }
  void entrySequenceValidation() {
    QByteArray bytes; QString error;
    QVERIFY(KissClient::parseEntrySequence(KissSettings{}.kissEntrySequence, &bytes, &error));
    QCOMPARE(bytes, QByteArray::fromHex("1b406b0d"));
    QVERIFY(KissClient::parseEntrySequence("k\\r", &bytes)); QCOMPARE(bytes, QByteArray("k\r"));
    QVERIFY(KissClient::parseEntrySequence("\\x00\\xFF\\\\\\n\\t", &bytes)); QCOMPARE(bytes, QByteArray::fromHex("00ff5c0a09"));
    for (const QString &invalid : {QString(), QString("k\\"), QString("\\x"), QString("\\x0g"), QString("\\q"),
         QString::fromUtf8("é"), QString("k\r"), QString(257, 'x')})
      QVERIFY2(!KissClient::parseEntrySequence(invalid, &bytes, &error), qPrintable(invalid));
    QVERIFY(KissClient::parseEntrySequence(QString("\\x41").repeated(256), &bytes)); QCOMPARE(bytes, QByteArray(256, 'A'));
    auto s = settings(Pty{}); s.portName = "/dev/example"; s.initializeKiss = true; s.kissEntrySequence = "";
    QVERIFY(!KissClient::validate(s)); s.initializeKiss = false; QVERIFY(KissClient::validate(s));
    s.kissEntryDelayMs = 10001; QVERIFY(!KissClient::validate(s));
  }
  void modeEntryOrdersTrafficAndPreservesTransmitGuards() {
    Pty p; QVERIFY(p.master >= 0); auto s = settings(p, true); s.initializeKiss = true; s.kissEntryDelayMs = 180;
    StationModel stations; KissClient client(s, &stations); QSignalSpy received(&client, &RadioInterface::packetReceived);
    QSignalSpy transmitted(&client, &RadioInterface::packetTransmitted); QString error;
    QElapsedTimer elapsed; elapsed.start(); client.start(); client.start(); // Repeated Open does not reinitialize an open port.
    QVERIFY(client.portOpen()); QVERIFY(client.initializing()); QVERIFY(!client.connected());
    QVERIFY(!client.transmit(">too soon", {}, &error)); QVERIFY(error.contains("initialization"));
    QVERIFY(!client.applyParameters(&error)); QVERIFY(error.contains("Entering KISS"));
    client.setTransmitBlocked(true); // This must not cancel the control sequence or its wait.
    QByteArray captured; QTRY_COMPARE((captured += p.drain()), QByteArray::fromHex("1b406b0d"));
    QVERIFY(p.send("TNC2 ready\r\ncmd:" + wire(incomingAddress + ">startup bytes")));
    bool responsive = false; QTimer::singleShot(0, &client, [&] { responsive = true; }); QTRY_VERIFY(responsive);
    QVERIFY(!client.connected()); QTRY_VERIFY(client.connected()); QVERIFY(elapsed.elapsed() >= 180);
    QVERIFY(!client.initializing()); QCOMPARE(received.size(), 0); QVERIFY(!stations.record("N0CALL-3"));
    QVERIFY(p.drain().isEmpty()); QCOMPARE(transmitted.size(), 0); // No implicit parameters or RF data.
    QVERIFY(!client.transmit(">blocked", {}, &error)); QVERIFY(error.contains("All transmission"));
    client.setTransmitBlocked(false); QVERIFY(client.transmit(">ready")); captured.clear();
    QTRY_COMPARE((captured += p.drain()), wire(outgoingAddress + ">ready"));
    QVERIFY(p.send(wire(incomingAddress + ">real KISS"))); QTRY_COMPARE(received.size(), 1);
    QCOMPARE(received[0][0].toByteArray(), QByteArray("N0CALL-3>APRS:>real KISS"));
  }
  void modeEntryReconnectAndCancellation() {
    Pty first, second; QVERIFY(first.master >= 0 && second.master >= 0);
    const auto link = dir_.filePath("entry-link"); QVERIFY(QFile::link(first.path, link));
    auto s = settings(first, true); s.portName = link; s.reconnect = true; s.initializeKiss = true; s.kissEntryDelayMs = 100;
    s.kissEntrySequence = "k\\r"; KissClient client(s, nullptr); client.start();
    QByteArray captured; QTRY_COMPARE((captured += first.drain()), QByteArray("k\r")); QTRY_VERIFY(client.connected());
    first.disconnect(); QVERIFY(client.transmit(">lost")); QTRY_VERIFY(!client.portOpen());
    QVERIFY(QFile::remove(link)); QVERIFY(QFile::link(second.path, link)); QTRY_VERIFY_WITH_TIMEOUT(client.portOpen(), 5000);
    QVERIFY(client.initializing()); QVERIFY(!client.connected()); captured.clear();
    QTRY_COMPARE((captured += second.drain()), QByteArray("k\r"));
    client.stop(); QVERIFY(!client.portOpen()); QVERIFY(!client.initializing()); QCOMPARE(client.pendingBytes(), qsizetype(0));
    QTest::qWait(180); QVERIFY(!client.connected()); QVERIFY(second.drain().isEmpty()); // The canceled delay cannot mark a closed port ready.
    client.start(); client.stop(); QTest::qWait(20); QVERIFY(second.drain().isEmpty()); // Stop also cancels an entry not yet written.
    client.start(); captured.clear(); QTRY_COMPARE((captured += second.drain()), QByteArray("k\r")); QTRY_VERIFY(client.connected());
    client.stop(); QFile::remove(link);
    auto rx = settings(second); rx.initializeKiss = true; KissClient receiveOnly(rx, nullptr); receiveOnly.setTransmitBlocked(true); receiveOnly.start();
    captured.clear(); QTRY_COMPARE((captured += second.drain()), QByteArray::fromHex("1b406b0d")); QTRY_VERIFY(receiveOnly.connected());
    QVERIFY(!receiveOnly.transmit(">receive only")); QVERIFY(second.drain().isEmpty());
    Pty broken; auto failed = settings(broken); failed.initializeKiss = true;
    KissClient interrupted(failed, nullptr); QSignalSpy transmissions(&interrupted, &RadioInterface::packetTransmitted);
    interrupted.start(); QVERIFY(interrupted.initializing()); broken.disconnect(); // Fail while writing entry, before its timer starts.
    QTRY_VERIFY(!interrupted.portOpen()); QTest::qWait(150);
    QVERIFY(!interrupted.initializing()); QVERIFY(!interrupted.connected()); QCOMPARE(interrupted.pendingBytes(), qsizetype(0)); QCOMPARE(transmissions.size(), 0);
  }
  void receiveFramingAndRecovery() {
    Pty p; QVERIFY(p.master >= 0); StationModel stations;
    KissClient client(settings(p), &stations); QSignalSpy received(&client, &RadioInterface::packetReceived);
    client.start(); QVERIFY(client.connected()); QTest::qWait(10); QVERIFY(p.drain().isEmpty()); // No mode-entry or parameter writes.
    const auto packet = wire(incomingAddress + ">OK");
    QVERIFY(p.send(packet.left(7))); QTest::qWait(10); QCOMPARE(received.size(), 0);
    QVERIFY(p.send(packet.mid(7))); QTRY_COMPARE(received.size(), 1);
    QCOMPARE(received[0][0].toByteArray(), QByteArray("N0CALL-3>APRS:>OK"));
    QVERIFY(stations.record("N0CALL-3")); QVERIFY(stations.record("N0CALL-3")->flag & ST_VIATNC);
    const auto binary = QByteArray::fromHex("3ec0db0d0a00");
    const auto escaped = wire(incomingAddress + binary);
    for (char byte : escaped) { QVERIFY(p.send(QByteArray(1, byte))); QCoreApplication::processEvents(); }
    QTRY_COMPARE(received.size(), 2); QCOMPARE(received[1][0].toByteArray(), QByteArray("N0CALL-3>APRS:") + binary);
    QVERIFY(p.send(wire(incomingAddress + ">wrong channel", 0) + wire("ignored", 12, 2) + wire("bad AX25")));
    QVERIFY(p.send(QByteArray::fromHex("c0dbdcdb00c0") + QByteArray::fromHex("c000") + QByteArray(5000, 'x') + QByteArray(1, char(0xc0))));
    QVERIFY(p.send(packet + packet)); QTRY_COMPARE(received.size(), 4);
    QVERIFY(client.rejectedFrames() >= 3); QCOMPARE(client.receivedPackets(), quint64(4));
    QTest::qWait(10); QVERIFY(p.drain().isEmpty()); // RX never echoes or gates traffic.
    bool yielded = false;
    connect(&client, &RadioInterface::packetReceived, &client, [&] {
      if (received.size() == 5) QTimer::singleShot(0, &client, [&] { yielded = received.size() < 104; });
    });
    QVERIFY(p.send(packet.repeated(100))); QTRY_COMPARE(received.size(), 104); QVERIFY(yielded);
  }
  void transmitParametersAndBounds() {
    Pty p; QVERIFY(p.master >= 0); QString error;
    KissClient rx(settings(p), nullptr); rx.start(); QVERIFY(rx.connected());
    QVERIFY(!rx.transmit(">blocked", {}, &error)); QVERIFY(error.contains("disabled"));
    auto parameters = settings(p); parameters.txDelay = 192; parameters.persistence = 219; parameters.slotTime = 10; parameters.txTail = 30; parameters.fullDuplex = true;
    rx.setTransmitBlocked(true); QVERIFY(rx.applyParameters(parameters, &error));
    QByteArray commands;
    QTRY_VERIFY((commands += p.drain()).size() >= 22);
    QCOMPARE(commands, QByteArray::fromHex("c0c1dbdcc0c0c2dbddc0c0c30ac0c0c41ec0c0c501c0"));
    auto other = parameters; other.radioPort = 0; QVERIFY(!rx.applyParameters(other, &error));
    rx.stop();
    KissClient tx(settings(p, true), nullptr); QSignalSpy transmitted(&tx, &RadioInterface::packetTransmitted);
    tx.start(); QVERIFY(tx.connected()); QVERIFY(tx.transmit(QByteArray(">") + char(0xc0) + char(0xdb), {}, &error));
    QByteArray captured; QTRY_VERIFY(!(captured += p.drain()).isEmpty());
    QCOMPARE(captured, wire(outgoingAddress + QByteArray(">") + char(0xc0) + char(0xdb)));
    QCOMPARE(transmitted.size(), 1); QCOMPARE(transmitted[0][0].toByteArray(), QByteArray("LOCAL>APXQT1:>") + char(0xc0) + char(0xdb));
    QVERIFY(!tx.transmit(">bad\n", {}, &error)); QVERIFY(!tx.transmit(QByteArray(257, 'x'), {}, &error));
    QVERIFY(!tx.transmit(">bad path", "NINECALL", &error));
    QVERIFY(tx.transmit(">pending")); tx.setTransmitBlocked(true); QCOMPARE(tx.pendingBytes(), qsizetype(0));
    QTest::qWait(10); QVERIFY(p.drain().isEmpty()); QVERIFY(!tx.transmit(">blocked", {}, &error));
    tx.setTransmitBlocked(false);
    int admitted = 0; while (tx.transmit(QByteArray(256, char(0xc0)), {}, &error)) ++admitted;
    QVERIFY(admitted > 0 && admitted <= 128); QVERIFY(tx.pendingBytes() <= 65536); QVERIFY(error.contains("queue is full"));
    tx.stop(); QCOMPARE(tx.pendingBytes(), qsizetype(0)); tx.start(); QVERIFY(tx.connected());
    QTest::qWait(10); QVERIFY(p.drain().isEmpty()); // Pending TX is never replayed on reconnect.
    for (int i = 0; i < 128; ++i) QVERIFY(tx.transmit(">small"));
    QVERIFY(!tx.transmit(">frame 129", {}, &error)); QVERIFY(error.contains("queue is full")); tx.setTransmitBlocked(true);
  }
  void disconnectAndReconnect() {
    Pty first, second; QVERIFY(first.master >= 0 && second.master >= 0);
    const auto link = dir_.filePath("serial-link"); QVERIFY(QFile::link(first.path, link));
    auto s = settings(first, true); s.portName = link; s.reconnect = true;
    KissClient client(s, nullptr); QSignalSpy received(&client, &RadioInterface::packetReceived);
    client.start(); QVERIFY(client.connected());
    QVERIFY(first.send(wire(incomingAddress + ">partial").left(8))); QTest::qWait(10);
    first.disconnect();
    // PTY hangup is reported by Qt on the next I/O, unlike USB removal's
    // immediate ResourceError. Exercise that asynchronous write-error path.
    QVERIFY(client.transmit(">lost port")); QTRY_VERIFY(!client.connected()); QCOMPARE(client.pendingBytes(), qsizetype(0));
    QVERIFY(QFile::remove(link)); QVERIFY(QFile::link(second.path, link)); QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 5000);
    QVERIFY(second.send(wire(incomingAddress + ">new"))); QTRY_COMPARE(received.size(), 1);
    QCOMPARE(received[0][0].toByteArray(), QByteArray("N0CALL-3>APRS:>new")); QVERIFY(second.drain().isEmpty());
    client.stop(); QVERIFY(!client.connected()); QTest::qWait(20); QVERIFY(!client.connected()); QFile::remove(link);
    // A failed open with reconnect off can be retried explicitly.
    auto manual = s; manual.reconnect = false; KissClient stopped(manual, nullptr); stopped.start(); QVERIFY(!stopped.connected());
    QVERIFY(QFile::link(second.path, link)); stopped.start(); QVERIFY(stopped.connected()); stopped.stop(); QFile::remove(link);
  }
  void messagesBeaconsObjectsAndHistory() {
    Pty p; QVERIFY(p.master >= 0); auto o = options(p); o.historyEnabled = true; o.history.path = dir_.filePath("serial.sqlite");
    {
      MainWindow window(o); auto *client = window.findChild<KissClient *>(); QVERIFY(client); QTRY_VERIFY(client->connected());
      auto *messages = window.messageService(); auto *beacon = window.findChild<BeaconControl *>();
      auto *objects = window.findChild<ObjectControl *>(); QString error;
      QVERIFY(p.send(wire(incomingAddress + "!4903.50N/07201.75W>hello"))); QTRY_VERIFY(window.stationModel()->record("N0CALL-3"));
      QVERIFY(!beacon->sendNow(3, &error)); window.findChild<QAction *>("PULDNTNT05")->trigger();
      QVERIFY2(beacon->sendNow(3, &error), qPrintable(error)); QByteArray bytes;
      QTRY_VERIFY(!(bytes += p.drain()).isEmpty()); QCOMPARE(information(bytes), QByteArray("=4903.50N/07201.75W>"));
      OwnedEntity object; object.name = "Aid Point"; object.position = {49, -72}; object.device = 3;
      QVERIFY(objects->saveDefinition(object, &error)); QVERIFY(!objects->sendNow(object.name, &error));
      window.findChild<QAction *>("PULDNTNT06")->trigger(); QVERIFY(objects->sendNow(object.name, &error)); bytes.clear();
      QTRY_VERIFY(!(bytes += p.drain()).isEmpty()); QVERIFY(information(bytes).startsWith(";Aid Point*"));
      QVERIFY(messages->sendMessage("N0CALL-3", "hello serial", false, 3, "", &error)); bytes.clear();
      QTRY_VERIFY(!(bytes += p.drain()).isEmpty()); const auto message = information(bytes);
      QVERIFY(message.startsWith(":N0CALL-3 :hello serial{")); const auto id = message.mid(message.indexOf('{')+1);
      QVERIFY(p.send(wire(incomingAddress + ":LOCAL    :ack" + id))); QTRY_VERIFY(!messages->messages().last().pending);
      QCOMPARE(messages->messages().last().status, QString("Acknowledged"));
      QVERIFY(p.send(wire(incomingAddress + ":LOCAL    :incoming{A1"))); bytes.clear();
      QTRY_VERIFY(!(bytes += p.drain()).isEmpty()); QCOMPARE(information(bytes), QByteArray(":N0CALL-3 :ackA1"));
      window.findChild<QAction *>("PULDNTNT03")->trigger();
      QVERIFY(!messages->sendQuery("?APRS?", 3, &error)); QVERIFY(!beacon->sendNow(3, &error)); QVERIFY(!objects->sendNow(object.name, &error));
      QVERIFY(p.send(wire(incomingAddress + ":LOCAL    :blocked{A2"))); QTRY_COMPARE(messages->messages().last().text, QString("blocked"));
      QTest::qWait(20); QVERIFY(p.drain().isEmpty());
      window.findChild<QAction *>("PULDNTNT03")->trigger();
      QVERIFY(messages->sendMessage("N0CALL-3", "interrupted", false, 3, "", &error)); bytes.clear(); QTRY_VERIFY(!(bytes += p.drain()).isEmpty());
      window.historyStore()->flush(); const auto history = window.historyStore()->packets();
      QVERIFY(std::any_of(history.begin(), history.end(), [](const auto &row) { return row.outgoing && row.device == 3; }));
    }
    p.drain(); QSettings().clear();
    MainWindow restored(o); auto *client = restored.findChild<KissClient *>(); QTRY_VERIFY(client->connected());
    QVERIFY(restored.stationModel()->record("N0CALL-3"));
    QVERIFY(restored.messageService()->messages().last().retryable); QVERIFY(!restored.messageService()->messages().last().pending);
    restored.messageService()->processRetries(); QTest::qWait(20); QVERIFY(p.drain().isEmpty()); // Replay never sends ACKs or pending TX.
  }
  void receiveOnlyAndGlobalDisabled() {
    Pty p; auto o = options(p, false); MainWindow rx(o); auto *client = rx.findChild<KissClient *>(); QTRY_VERIFY(client->connected());
    QString error; QVERIFY(!rx.messageService()->sendQuery("?APRS?", 3, &error)); QVERIFY(error.contains("disabled"));
    QVERIFY(p.send(wire(incomingAddress + ":LOCAL    :receive only{B1"))); QTRY_VERIFY(!rx.messageService()->messages().isEmpty());
    QTest::qWait(20); QVERIFY(p.drain().isEmpty());
  }
  void interfaceSettingsAndExplicitActions() {
    Pty p; auto o = options(p); o.config.kiss[0].connectOnStartup = false; MainWindow window(o);
    auto *client = window.findChild<KissClient *>(); QVERIFY(!client->connected());
    auto *control = window.findChild<InterfaceControl *>(); control->open();
    auto *d = window.findChild<QDialog *>("interfaceControl"); auto *table = d->findChild<QTableWidget *>("kissInterfaces");
    QVERIFY(table); QCOMPARE(table->rowCount(), 1); table->selectRow(0);
    d->findChild<QPushButton *>("openKissPort")->click(); QVERIFY(client->connected()); QTest::qWait(10); QVERIFY(p.drain().isEmpty());
    qobject_cast<QSpinBox *>(table->cellWidget(0, 8))->setValue(192);
    qobject_cast<QSpinBox *>(table->cellWidget(0, 9))->setValue(219);
    d->findChild<QPushButton *>("applyKissParameters")->click(); QByteArray bytes; QTRY_VERIFY(!(bytes += p.drain()).isEmpty());
    QVERIFY(bytes.startsWith(QByteArray::fromHex("c0c1dbdcc0c0c2dbddc0"))); QCOMPARE(client->settings().txDelay, 40); // Apply doesn't save.
    d->findChild<QPushButton *>("closeKissPort")->click(); QVERIFY(!client->connected());
    table->item(0, 13)->setCheckState(Qt::Checked);
    qobject_cast<QLineEdit *>(table->cellWidget(0, 14))->setText("k\\r");
    qobject_cast<QSpinBox *>(table->cellWidget(0, 15))->setValue(220);
    d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); QTRY_VERIFY(!d->isVisible());
    XastirConfig saved; readQtConfiguration(saved); QCOMPARE(saved.kiss.size(), 1); QCOMPARE(saved.kiss[0].txDelay, 192);
    QCOMPARE(saved.kiss[0].persistence, 219); QCOMPARE(saved.kiss[0].baudRate, 9600); QVERIFY(!saved.kiss[0].connectOnStartup);
    QVERIFY(saved.kiss[0].initializeKiss); QCOMPARE(saved.kiss[0].kissEntrySequence, QString("k\\r")); QCOMPARE(saved.kiss[0].kissEntryDelayMs, 220);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    control->open(); d = window.findChild<QDialog *>("interfaceControl"); table = d->findChild<QTableWidget *>("kissInterfaces");
    table->selectRow(0); d->findChild<QPushButton *>("openKissPort")->click();
    auto *updated = window.findChild<KissClient *>(); QVERIFY(updated->initializing());
    QVERIFY(d->findChild<QLabel *>("kissActionStatus")->text().contains("entering KISS")); bytes.clear();
    QTRY_COMPARE((bytes += p.drain()), QByteArray("k\r"));
    d->findChild<QPushButton *>("applyKissParameters")->click(); QVERIFY(d->findChild<QLabel *>("kissActionStatus")->text().contains("wait"));
    QTRY_VERIFY(updated->connected()); QVERIFY(p.drain().isEmpty()); d->findChild<QPushButton *>("closeKissPort")->click();
    d->findChild<QPushButton *>("addKissInterface")->click(); QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->item(1, 7)->checkState(), Qt::Unchecked); QCOMPARE(table->item(1, 5)->checkState(), Qt::Unchecked);
    QCOMPARE(table->item(1, 13)->checkState(), Qt::Unchecked); table->item(0, 13)->setCheckState(Qt::Unchecked);
    d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click(); XastirConfig cancelled; readQtConfiguration(cancelled); QCOMPARE(cancelled.kiss.size(), 1);
    QVERIFY(cancelled.kiss[0].initializeKiss);
  }
  void legacyImportAndQtUpgrade() {
    QFile file(dir_.filePath("legacy.cnf")); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("STATION_CALLSIGN:LOCAL\nDEVICE0_TYPE:10\nDEVICE0_NAME:/dev/ttyUSB0\nDEVICE0_SPEED:13\nDEVICE0_STYLE:0\nDEVICE0_RADIO_PORT:12\nDEVICE0_UNPROTO1:WIDE1-1,WIDE2-1\nDEVICE0_ONSTARTUP:1\nDEVICE0_TXMT:0\nDEVICE0_TNC_PERSISTENCE:192\nDEVICE0_TNC_INIT_KISSMODE:1\nDEVICE1_TYPE:13\nDEVICE1_NAME:/dev/ttyUSB1\nDEVICE1_SPEED:4098\nDEVICE1_TXMT:1\nDEVICE2_TYPE:10\nDEVICE2_NAME:/dev/ttyUSB2\nDEVICE2_SPEED:9600\nDEVICE3_TYPE:10\nDEVICE3_NAME:/dev/invalid\nDEVICE3_STYLE:1\nDEVICE4_TYPE:10\nDEVICE4_NAME:/dev/invalid2\nDEVICE4_RADIO_PORT:16\n"); file.close();
    XastirConfig config; QString error; QVERIFY(!readXastirConfig(file.fileName(), config, &error));
    QVERIFY(error.contains("8N1")); // Unsupported serial styles fail visibly rather than being misinterpreted.
    QVERIFY(file.open(QIODevice::ReadWrite)); const auto valid = file.readAll().split('\n');
    QByteArray filtered; for (const auto &line : valid) if (!line.startsWith("DEVICE3_") && !line.startsWith("DEVICE4_")) filtered += line + '\n';
    file.resize(0); file.seek(0); file.write(filtered); file.close();
    QVERIFY2(readXastirConfig(file.fileName(), config, &error), qPrintable(error));
    QCOMPARE(config.kiss.size(), 3); QCOMPARE(config.kiss[0].baudRate, 9600); QCOMPARE(config.kiss[0].radioPort, 12u);
    QVERIFY(!config.kiss[0].transmitEnabled); QVERIFY(config.kiss[0].connectOnStartup); QCOMPARE(config.kiss[0].persistence, 192);
    QVERIFY(config.kiss[0].initializeKiss); QCOMPARE(config.kiss[0].kissEntrySequence, QString("\\x1b@k\\r")); QCOMPARE(config.kiss[0].kissEntryDelayMs, 100);
    QVERIFY(!config.kiss[1].initializeKiss); QVERIFY(!config.kiss[2].initializeKiss);
    QCOMPARE(config.kiss[1].baudRate, 115200); QVERIFY(config.kiss[1].transmitEnabled); QCOMPARE(config.kiss[2].baudRate, 9600);
    QSettings().setValue("interfaces/config", QByteArray("{\"callsign\":\"NEW\",\"agwpe\":[]}"));
    readQtConfiguration(config); QCOMPARE(config.kiss.size(), 3); QCOMPARE(config.kiss[0].callsign, QString("NEW"));
    QSettings().setValue("interfaces/config", QByteArray("{\"callsign\":\"NEW\",\"kiss\":[]}"));
    readQtConfiguration(config); QVERIFY(config.kiss.isEmpty());
    auto s = settings(Pty{}); s.portName = "/dev/example"; s.radioPort = 16; QVERIFY(!KissClient::validate(s)); s.radioPort = 0;
    s.txDelay = 256; QVERIFY(!KissClient::validate(s)); s.txDelay = 40; s.path = "BAD-CALL"; QVERIFY(!KissClient::validate(s));
  }
  void duplicateDeviceNumbersAreRejected() {
    Pty p; auto o = options(p); o.config.kiss[0].connectOnStartup = false;
    AgwpeSettings agw; agw.callsign = "LOCAL"; agw.deviceIndex = 2; agw.host = "127.0.0.1"; agw.connectOnStartup = false; o.config.agwpe.append(agw);
    MainWindow window(o); auto *control = window.findChild<InterfaceControl *>(); QSignalSpy saved(control, &InterfaceControl::configurationChanged);
    control->open(); auto *d = window.findChild<QDialog *>("interfaceControl"); auto *table = d->findChild<QTableWidget *>("kissInterfaces");
    table->item(0, 0)->setText("2"); bool warned = false;
    QTimer::singleShot(0, d, [&] { for (auto *box : d->findChildren<QMessageBox *>()) { warned = box->text().contains("unique across"); box->accept(); } });
    d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); QVERIFY(warned); QCOMPARE(saved.size(), 0); QVERIFY(d->isVisible());
    table->item(0, 0)->setText("3"); d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); QCOMPARE(saved.size(), 1);
    XastirConfig config; readQtConfiguration(config); QCOMPARE(config.agwpe[0].deviceIndex, 2); QCOMPARE(config.kiss[0].deviceIndex, 3);
    // Malformed persisted serial rows and IDs reserved by AGWPE cannot steal a route.
    auto json = QJsonDocument::fromJson(QSettings().value("interfaces/config").toByteArray()).object();
    QJsonArray devices = json["kiss"].toArray(); auto duplicate = devices[0].toObject(); duplicate["device"] = 2; devices.append(duplicate);
    auto invalid = duplicate; invalid["device"] = 5; invalid["channel"] = -1; devices.append(invalid); json["kiss"] = devices;
    invalid = duplicate; invalid["device"] = 6; invalid["initializeKiss"] = true; invalid["kissEntrySequence"] = "\\q"; devices.append(invalid);
    invalid = duplicate; invalid["device"] = 7; invalid["kissEntryDelayMs"] = -1; devices.append(invalid); json["kiss"] = devices;
    QSettings().setValue("interfaces/config", QJsonDocument(json).toJson()); readQtConfiguration(config); QCOMPARE(config.kiss.size(), 1); QCOMPARE(config.kiss[0].deviceIndex, 3);
  }
};
QTEST_MAIN(KissTest)
#include "test_kiss.moc"
