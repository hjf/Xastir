// SPDX-License-Identifier: GPL-2.0-or-later
#include "message_service.h"
#include "main_window.h"
#include "agwpe_client.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {
QByteArray received(const QByteArray &sender, const QByteArray &to, const QByteArray &body) {
  return sender + ">APRS::" + to.leftJustified(9, ' ') + ':' + body;
}
QByteArray rawFrame(const QByteArray &info) {
  QByteArray payload(1, '\0');
  for (const auto &call : {QByteArray("APRS  "), QByteArray("REMOTE")}) {
    for (char c : call) payload += char(c << 1);
    payload += char(call == "REMOTE" ? 0x61 : 0x60);
  }
  payload += char(3); payload += char(0xf0); payload += info;
  QByteArray header(36, '\0'); header[4] = 'K';
  qToLittleEndian<quint32>(quint32(payload.size()), header.data() + 28);
  return header + payload;
}
}
class MessageTest : public QObject {
  Q_OBJECT
  QTemporaryDir settingsDir_;
private slots:
  void initTestCase();
  void init();
  void validationAndDelivery();
  void retriesAndCancellation();
  void incomingAndDuplicates();
  void replyAckAndSatelliteMode();
  void autoReplyAndGroups();
  void queries();
  void queueOrderAndAutoReplyRules();
  void agwpeTransmitFrames();
  void completeMessageMenu();
  void enterSendsOnce();
};
void MessageTest::initTestCase() {
  QVERIFY(settingsDir_.isValid());
  QCoreApplication::setOrganizationName("XastirTests"); QCoreApplication::setApplicationName("MessageTests");
  QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir_.path());
}
void MessageTest::init() { QSettings settings; settings.clear(); }
void MessageTest::validationAndDelivery() {
  qint64 now = 1000; MessageService service("LOCAL-3", nullptr, [&] { return now; });
  QList<QByteArray> sent;
  service.setTransport([&](const QByteArray &info, int device, const QString &path, QString *) {
    sent.append(info); return device == 7 && path == "WIDE1-1";
  });
  QString error;
  QVERIFY(!service.sendMessage("TOOLONGCALL", "hello", false, 7, "WIDE1-1", &error));
  QVERIFY(!service.sendMessage("REMOTE", QString(68, 'x'), false, 7, "WIDE1-1", &error));
  QVERIFY(!service.sendMessage("REMOTE", "hello{bad", false, 7, "WIDE1-1", &error));
  QVERIFY(!service.sendMessage("REMOTE", QString::fromUtf8("café"), false, 7, "WIDE1-1", &error));
  QVERIFY(service.sendMessage("remote", "hello", false, 7, "WIDE1-1", &error));
  QCOMPARE(sent.first(), QByteArray(":REMOTE   :hello{00}"));
  QCOMPARE(service.messages().first().attempts, 1); QVERIFY(service.messages().first().pending);
  service.receivePacket(received("OTHER", "LOCAL-3", "ack00"), 7); QVERIFY(service.messages().first().pending);
  service.receivePacket(received("REMOTE", "LOCAL-4", "ack00"), 7); QVERIFY(service.messages().first().pending);
  service.receivePacket(received("REMOTE", "LOCAL-3", "ack00"), 7);
  QCOMPARE(service.messages().first().status, QString("Acknowledged")); QVERIFY(!service.messages().first().pending);
  QVERIFY(service.sendMessage("REMOTE", "second", false, 7, "WIDE1-1"));
  service.receivePacket(received("REMOTE", "LOCAL-3", "rej01"), 7);
  QCOMPARE(service.messages().last().status, QString("Rejected"));
}
void MessageTest::retriesAndCancellation() {
  qint64 now = 1000; bool online = false; int sends = 0;
  MessageService service("LOCAL", nullptr, [&] { return now; });
  service.setTransport([&](const QByteArray &, int, const QString &, QString *error) {
    if (!online) { if (error) *error = "Disconnected"; return false; } ++sends; return true;
  });
  QVERIFY(service.sendMessage("REMOTE", "test", false, 0, {}));
  QCOMPARE(service.messages().first().attempts, 0);
  now += 7000; service.processRetries(); QCOMPARE(sends, 0);
  online = true; now += 7000; service.processRetries(); QCOMPARE(sends, 1);
  QCOMPARE(service.messages().first().nextRetry, now + 7000);
  now += 6999; service.processRetries(); QCOMPARE(sends, 1);
  ++now; service.processRetries(); QCOMPARE(sends, 2);
  QCOMPARE(service.messages().first().nextRetry, now + 14000);
  for (int n = 0; n < 17; ++n) {
    now = service.messages().first().nextRetry; service.processRetries();
  }
  QCOMPARE(sends, 18); QVERIFY(!service.messages().first().pending);
  QCOMPARE(service.messages().first().status, QString("Timed out — no ACK"));
  service.retry(service.messages().first().key); QCOMPARE(sends, 19);
  service.cancel(service.messages().first().key); QVERIFY(!service.messages().first().pending);
  now += 1000000; service.processRetries(); QCOMPARE(sends, 19);
  service.clearOutgoing(); QVERIFY(service.messages().isEmpty());
}
void MessageTest::incomingAndDuplicates() {
  qint64 now = 1000; MessageService service("LOCAL-3", nullptr, [&] { return now; });
  QList<QByteArray> sent; QSignalSpy incoming(&service, &MessageService::incomingMessage);
  service.setTransport([&](const QByteArray &info, int, const QString &, QString *) { sent.append(info); return true; });
  const auto packet = received("REMOTE", "LOCAL-3", "Hello{A1");
  service.receivePacket(packet, 0); QCOMPARE(sent.last(), QByteArray(":REMOTE   :ackA1"));
  QCOMPARE(service.messages().size(), 1); QCOMPARE(incoming.size(), 1);
  service.receivePacket(packet, 0); QCOMPARE(sent.size(), 1); QCOMPARE(service.messages().size(), 1);
  now += 30000; service.receivePacket(packet, 0); QCOMPARE(sent.size(), 2); QCOMPARE(incoming.size(), 1);
  service.receivePacket(received("REMOTE", "LOCAL-4", "Not yours{B2"), 0); QCOMPARE(sent.size(), 2);
  service.receivePacket(received("REMOTE", "CQ", "Group{C3"), 0); QCOMPARE(sent.size(), 2);
  QVERIFY(service.messages().last().group);
  service.receivePacket(received("REMOTE", "LOCAL-3", "No ID"), 0); QCOMPARE(sent.size(), 2);
  service.receivePacket(received("REMOTE", "LOCAL-3", "Broken{!"), 0); QCOMPARE(service.messages().size(), 4);
}
void MessageTest::replyAckAndSatelliteMode() {
  MessageService service("LOCAL-3"); QList<QByteArray> sent;
  service.setTransport([&](const QByteArray &info, int, const QString &, QString *) { sent.append(info); return true; });
  service.setSatelliteMode(true);
  QVERIFY(service.sendMessage("REMOTE", "one", false, 0, {}));
  service.receivePacket(received("REMOTE", "LOCAL-3", "reply{A1}00"), 0);
  QCOMPARE(sent.size(), 1); QCOMPARE(service.messages().first().status, QString("Acknowledged"));
  QVERIFY(service.sendMessage("REMOTE", "two", false, 0, {}));
  QCOMPARE(sent.last(), QByteArray(":REMOTE   :two{01}A1"));
  service.receivePacket("GATE>APRS:}" + received("REMOTE", "LOCAL-3", "third{A2}01"), 0);
  QCOMPARE(service.messages()[2].status, QString("Acknowledged"));
  service.setSatelliteMode(false);
  service.receivePacket(received("REMOTE", "LOCAL-3", "fourth{A3}"), 0);
  QCOMPARE(sent.last(), QByteArray(":REMOTE   :ackA3}"));
}
void MessageTest::autoReplyAndGroups() {
  qint64 now = 1000; MessageService service("LOCAL", nullptr, [&] { return now; }); QList<QByteArray> sent;
  service.setTransport([&](const QByteArray &info, int, const QString &, QString *) { sent.append(info); return true; });
  QVERIFY(!service.setAutoReplyText(QString(65, 'x'))); QVERIFY(service.setAutoReplyText("Away")); service.setAutoReply(true);
  service.receivePacket(received("REMOTE", "LOCAL", "hello{A1}"), 0);
  QCOMPARE(sent.size(), 2); QVERIFY(sent.last().contains("AA:Away"));
  service.receivePacket(received("REMOTE", "LOCAL", "hello{A1}"), 0); QCOMPARE(sent.size(), 2);
  service.receivePacket(received("REMOTE", "LOCAL", "new{A2}"), 0); QCOMPARE(sent.size(), 3); // throttle auto reply
  service.receivePacket(received("OTHER", "LOCAL", "AA:Away{A1}"), 0); QCOMPARE(sent.size(), 4); // no reply loop
  service.addGroup("NET"); service.receivePacket(received("REMOTE", "NET", "Group{G1"), 0); QCOMPARE(sent.size(), 4);
  QVERIFY(service.sendMessage("NET", "group hello", true, 0, {}));
  QCOMPARE(sent.last(), QByteArray(":NET      :group hello")); QVERIFY(!service.messages().last().pending);
  const int count = sent.size(); now += 7000; service.processRetries(); QCOMPARE(sent.size(), count); // auto replies never retry
}
void MessageTest::queries() {
  MessageService service("LOCAL"); QList<QByteArray> sent;
  service.setTransport([&](const QByteArray &info, int, const QString &, QString *) { sent.append(info); return true; });
  for (const auto &query : {QByteArray("?APRS?"), QByteArray("?IGATE?"), QByteArray("?WX?")}) {
    QVERIFY(service.sendQuery(query, 0)); QCOMPARE(sent.last(), query);
  }
  QVERIFY(!service.sendQuery("?UNKNOWN?", 0)); QCOMPARE(service.messages().size(), 3);
}
void MessageTest::queueOrderAndAutoReplyRules() {
  qint64 now = 1000; MessageService service("LOCAL", nullptr, [&] { return now; }); QList<QByteArray> sent;
  service.setTransport([&](const QByteArray &info, int, const QString &, QString *) { sent.append(info); return true; });
  QVERIFY(!service.sendMessage("REMOTE", "bad path", false, 0, "NOPE-99"));
  QVERIFY(service.sendMessage("REMOTE", "first", false, 0, {}));
  QVERIFY(service.sendMessage("REMOTE", "second", false, 0, {})); QCOMPARE(sent.size(), 1);
  service.receivePacket(received("REMOTE", "LOCAL", "ack00}"), 0);
  now += 1000; service.processRetries(); QCOMPARE(sent.size(), 2);
  QVERIFY(sent.last().contains("second"));
  service.receivePacket(received("REMOTE", "LOCAL", "AA:Away}01"), 0);
  QCOMPARE(service.messages()[1].status, QString("Acknowledged"));
  QVERIFY(service.setAutoReplyText("Away")); service.setAutoReply(true);
  service.receivePacket(received("OTHER", "LOCAL", "hi{A1}"), 0);
  QCOMPARE(sent.last(), QByteArray(":OTHER    :AA:Away}A1")); QVERIFY(!service.messages().last().pending);
  QVERIFY(!sent.last().contains('{'));
  QVERIFY(service.sendMessage("CQ", "group", false, 0, {}));
  QVERIFY(!service.autoReply()); QVERIFY(!service.messages().last().pending);
  QCOMPARE(sent.last(), QByteArray(":CQ       :group"));
}
void MessageTest::agwpeTransmitFrames() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  StationModel stations; AgwpeSettings settings; settings.callsign = "LOCAL-3";
  settings.host = "127.0.0.1"; settings.port = server.serverPort(); settings.transmitEnabled = true;
  settings.radioPort = 2; settings.path = "WIDE1-1,WIDE2-1";
  AgwpeClient client(settings, &stations); client.start(); QTRY_VERIFY(server.hasPendingConnections());
  auto *peer = server.nextPendingConnection(); QByteArray handshake;
  QTRY_VERIFY((handshake += peer->readAll()).size() == 108);
  QVERIFY(client.transmit(":REMOTE   :hello{00}"));
  QByteArray bytes; QTRY_VERIFY((bytes += peer->readAll()).size() == 77);
  QCOMPARE(qFromLittleEndian<quint32>(bytes.constData()), 2U); QCOMPARE(bytes[4], 'V'); QCOMPARE(quint8(bytes[6]), quint8(0xf0));
  QCOMPARE(bytes.mid(8, 8), QByteArray("LOCAL-3\0", 8)); QCOMPARE(bytes.mid(18, 7), QByteArray("APXQT1\0", 7));
  QCOMPARE(bytes[36], char(2)); QCOMPARE(bytes.mid(37, 10), QByteArray("WIDE1-1\0\0\0", 10));
  QCOMPARE(bytes.mid(57), QByteArray(":REMOTE   :hello{00}"));
  QVERIFY(client.transmit("?WX?", QStringLiteral(""))); bytes.clear();
  QTRY_VERIFY((bytes += peer->readAll()).size() == 40); QCOMPARE(bytes[4], 'M'); QCOMPARE(bytes.mid(36), QByteArray("?WX?"));
  QVERIFY(!client.transmit("?WX?", "INVALID-16"));
  AgwpeSettings disabled = settings; disabled.transmitEnabled = false;
  AgwpeClient receiver(disabled, &stations); QString error; QVERIFY(!receiver.transmit("test", {}, &error));
  QVERIFY(error.contains("disabled")); client.stop();
}
void MessageTest::enterSendsOnce() {
  ApplicationOptions options;options.config.callsign="LOCAL-3";
  options.tiles.urlTemplate="http://127.0.0.1:1/{z}/{x}/{y}.png";
  AgwpeSettings settings;settings.callsign="LOCAL-3";settings.host="127.0.0.1";
  settings.port=1;settings.connectOnStartup=false;settings.transmitEnabled=true;
  options.config.agwpe.append(settings);MainWindow window(options);
  int transmissions=0;
  window.messageService()->setTransport([&](const auto &,int,const auto &,auto *){++transmissions;return true;});
  for(const auto &menu:{"PULDNMG001","PULDNMG002"}) {
    window.findChild<QAction *>(menu)->trigger();
    QDialog *conversation=nullptr;
    for(auto *d:window.findChildren<QDialog *>("messageConversation"))if(d->isVisible())conversation=d;
    QVERIFY(conversation);
    if(QString(menu)=="PULDNMG001")conversation->findChild<QComboBox *>("messageRecipient")->setCurrentText("REMOTE");
    auto *text=conversation->findChild<QLineEdit *>("messageText");
    for(const auto key:{Qt::Key_Return,Qt::Key_Enter}) {
      text->setText("Keyboard send");text->setFocus();
      const auto before=window.messageService()->messages().size();
      const int sent=transmissions;
      QTest::keyClick(text,key);QCoreApplication::processEvents();
      QCOMPARE(transmissions,sent+1);QCOMPARE(window.messageService()->messages().size(),before+1);
      QVERIFY(text->text().isEmpty());QVERIFY(conversation->isVisible());
      QVERIFY(window.findChildren<QMessageBox *>().isEmpty());
      // Complete the addressed delivery so the following test send is not queued.
      const auto last=window.messageService()->messages().last();
      if(!last.group)window.messageService()->receivePacket("REMOTE>APRS::LOCAL-3  :ack"+last.id.toLatin1(),0);
    }
    conversation->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  }
}
void MessageTest::completeMessageMenu() {
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
  ApplicationOptions options; options.tiles.urlTemplate = "http://127.0.0.1:1/{z}/{x}/{y}.png";
  options.config.callsign = "LOCAL-3";
  AgwpeSettings settings; settings.callsign = "LOCAL-3"; settings.host = "127.0.0.1";
  settings.port = server.serverPort(); settings.transmitEnabled = true; options.config.agwpe.append(settings);
  MainWindow window(options);
  QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection(); QByteArray handshake;
  QTRY_VERIFY((handshake += peer->readAll()).size() == 108);
  const QStringList ids{"PULDNMG001", "PULDNMG002", "PULDNMG003", "PULDNMG004", "PULDNMG005", "PULDNMG006", "PULDNMG007", "PULDQUS001", "PULDQUS002", "PULDQUS003"};
  for (const auto &id : ids) { auto *action = window.findChild<QAction *>(id); QVERIFY(action); QVERIFY(action->property("implemented").toBool()); }
  window.findChild<QAction *>("PULDNMG001")->trigger();
  auto *conversation = window.findChild<QDialog *>("messageConversation"); QVERIFY(conversation);
  conversation->findChild<QComboBox *>("messageRecipient")->setCurrentText("REMOTE");
  conversation->findChild<QLineEdit *>("messageText")->setText("Hello radio");
  conversation->findChild<QPushButton *>("sendMessageButton")->click();
  QByteArray outgoing; QTRY_VERIFY((outgoing += peer->readAll()).size() == 62);
  QCOMPARE(outgoing.mid(36), QByteArray(":REMOTE   :Hello radio{00}"));
  window.findChild<QAction *>("PULDNMG007")->trigger();
  auto *pending = window.findChild<QDialog *>("pendingMessagesDialog"); QVERIFY(pending);
  auto *pendingTable = pending->findChild<QTableWidget *>();
  QCOMPARE(pendingTable->rowCount(), 1); pendingTable->selectRow(0);
  pending->findChild<QPushButton *>("cancelMessageButton")->click();
  QVERIFY(!window.messageService()->messages().first().pending);
  pending->findChild<QCheckBox *>()->setChecked(false); QCOMPARE(pendingTable->rowCount(), 1);
  pendingTable->selectRow(0); pending->findChild<QPushButton *>("retryMessageButton")->click();
  QVERIFY(window.messageService()->messages().first().pending);
  QByteArray retry; QTRY_VERIFY((retry += peer->readAll()).size() == 62);
  QCOMPARE(retry.mid(36), outgoing.mid(36)); pending->findChild<QCheckBox *>()->setChecked(true);
  peer->write(rawFrame(":LOCAL-3  :ack00")); QTRY_VERIFY(!window.messageService()->messages().first().pending);
  QCOMPARE(pending->findChild<QTableWidget *>()->rowCount(), 0);
  peer->write(rawFrame(":LOCAL-3  :Incoming hello{A1}")); QTRY_COMPARE(window.messageService()->messages().size(), 2);
  QCOMPARE(conversation->findChild<QTableWidget *>()->rowCount(), 2);
  window.findChild<QAction *>("PULDNMG004")->trigger();
  auto *autoDialog = window.findChild<QDialog *>("autoReplyDialog"); QVERIFY(autoDialog);
  autoDialog->findChild<QLineEdit *>("autoReplyText")->setText("Away");
  autoDialog->findChild<QCheckBox *>()->setChecked(true);
  autoDialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
  QVERIFY(window.messageService()->autoReply()); QCOMPARE(window.messageService()->autoReplyText(), QString("Away"));
  window.findChild<QAction *>("PULDNMG005")->trigger(); QVERIFY(!window.messageService()->autoReply());
  window.findChild<QAction *>("PULDNMG006")->trigger(); QVERIFY(window.messageService()->satelliteMode());
  window.findChild<QAction *>("PULDNMG002")->trigger();
  QDialog *groupDialog = nullptr;
  int groups = 0; for (auto *dialog : window.findChildren<QDialog *>("messageConversation"))
    if (dialog->windowTitle() == "Group Messages") { ++groups; groupDialog = dialog; }
  QCOMPARE(groups, 1);
  QTRY_VERIFY(peer->bytesAvailable() >= 53); peer->readAll(); // standalone ACK for incoming hello
  groupDialog->findChild<QComboBox *>("messageRecipient")->setCurrentText("CQ");
  groupDialog->findChild<QLineEdit *>("messageText")->setText("Group hello");
  groupDialog->findChild<QPushButton *>("sendMessageButton")->click();
  QByteArray groupBytes; QTRY_VERIFY((groupBytes += peer->readAll()).size() == 58);
  QCOMPARE(groupBytes.mid(36), QByteArray(":CQ       :Group hello"));
  QVERIFY(!window.messageService()->messages().last().pending);
  const QList<QPair<QString, QByteArray>> queries{{"PULDQUS001", "?APRS?"}, {"PULDQUS002", "?IGATE?"}, {"PULDQUS003", "?WX?"}};
  for (const auto &entry : queries) {
    window.findChild<QAction *>(entry.first)->trigger(); auto *query = window.findChild<QDialog *>("stationQueryDialog"); QVERIFY(query);
    for (auto *button : query->findChildren<QPushButton *>()) if (button->text() == "Send Query") button->click();
    QByteArray sent; QTRY_VERIFY((sent += peer->readAll()).size() == 36 + entry.second.size());
    QCOMPARE(sent.mid(36), entry.second);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  window.findChild<QAction *>("PULDNMG003")->trigger();
  for (auto *box : window.findChildren<QMessageBox *>()) if (box->windowTitle() == "Clear All Outgoing Messages") box->done(QMessageBox::Yes);
  QCOMPARE(window.messageService()->messages().size(), 1); // incoming history remains
}
QTEST_MAIN(MessageTest)
#include "test_messages.moc"
