// SPDX-License-Identifier: GPL-2.0-or-later
#include "object_control.h"
#include "agwpe_client.h"
#include "main_window.h"
#include "packet_monitor.h"
#include "aprs_decoder.h"
#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
class ObjectTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settings_;
  static OwnedEntity definition() {
    OwnedEntity d; d.name = "Aid Point"; d.position = {49+3.5/60, -(72+1.75/60)};
    d.symbol = 'h'; d.comment = "Open"; d.device = 3; d.maximumSeconds = 60; return d;
  }
  static ApplicationOptions options(quint16 port = 0, bool tx = true) {
    ApplicationOptions o; o.config.callsign = "LOCAL";
    if (port) { AgwpeSettings d; d.callsign = "LOCAL"; d.host = "127.0.0.1"; d.port = port; d.deviceIndex = 3; d.transmitEnabled = tx; d.path = "WIDE1-1"; d.radioPort = 1; o.config.agwpe.append(d); }
    return o;
  }
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("XastirObjectTests"); QCoreApplication::setApplicationName("OwnedObjects");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
  }
  void init() { QSettings().clear(); }
  void radioWireControlsRepeatAndKill() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(options(server.serverPort()));
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    QTRY_VERIFY(peer->bytesAvailable() >= 108); peer->readAll();
    auto *objects = window.findChild<ObjectControl *>(); QVERIFY(objects);
    qint64 now = QDateTime::fromString("2026-10-19T12:34:00Z", Qt::ISODate).toMSecsSinceEpoch();
    objects->setClock([&] { return now; });
    QSignalSpy observed(window.findChild<PacketMonitor *>(), &PacketMonitor::packetObserved);
    QString error; auto d = definition(); QVERIFY2(objects->saveDefinition(d, &error), qPrintable(error));
    QVERIFY(window.stationModel()->record(d.name)->flag & ST_MYOBJITEM);
    QCOMPARE(peer->bytesAvailable(), qint64(0)); QVERIFY(!objects->sendNow(d.name, &error)); QVERIFY(error.contains("Object/item transmission is disabled"));
    window.findChild<QAction *>("PULDNTNT06")->trigger();
    window.findChild<QAction *>("PULDNTNT03")->trigger();
    QVERIFY(!objects->sendNow(d.name, &error)); QVERIFY(error.contains("All transmission"));
    window.findChild<QAction *>("PULDNTNT03")->trigger();
    QVERIFY(window.findChild<QAction *>("PULDNTNT05")->isChecked()); // Position has an independent guard.
    QVERIFY2(objects->sendNow(d.name, &error), qPrintable(error));
    QTRY_VERIFY(peer->bytesAvailable() >= 36); const auto frame = peer->readAll();
    QCOMPARE(frame[4], 'V'); QCOMPARE(qFromLittleEndian<quint32>(frame.constData()), 1u);
    QCOMPARE(frame.mid(8, 5), QByteArray("LOCAL")); QCOMPARE(frame.mid(18, 6), QByteArray("APXQT1"));
    QCOMPARE(frame.mid(37, 7), QByteArray("WIDE1-1"));
    const auto packet = observed.last()[0].toByteArray(); QVERIFY(observed.last()[3].toBool());
    QCOMPARE(packet, QByteArray("LOCAL>APXQT1,WIDE1-1:;Aid Point*191234z4903.50N/07201.75WhOpen"));
    QCOMPARE(frame.mid(47), packet.mid(packet.indexOf(':') + 1));
    QVERIFY(objects->setRepeat(d.name, true)); now += 19999; objects->process(); QCOMPARE(observed.count(), 1);
    ++now; objects->process(); QCOMPARE(observed.count(), 2); // First repeat at 20 seconds.
    QVERIFY(objects->kill(d.name, &error)); QCOMPARE(observed.count(), 3);
    QVERIFY(observed.last()[0].toByteArray().contains(";Aid Point_191234z")); QVERIFY(!window.stationModel()->record(d.name));
    QVERIFY(objects->repeating(d.name));
    for (int i = 0; i < 19; ++i) { now += 60000; objects->process(); }
    QCOMPARE(observed.count(), 22); QVERIFY(!objects->repeating(d.name));
    now += 60000; objects->process(); QCOMPARE(observed.count(), 22);
    {
      MainWindow restored(options()); auto *saved = restored.findChild<ObjectControl *>();
      QCOMPARE(saved->definitions().size(), 1); QVERIFY(!saved->definitions().first().alive);
      QVERIFY(!saved->repeating(d.name)); QVERIFY(!restored.stationModel()->record(d.name));
    }
    QVERIFY(objects->forget(d.name, &error)); QVERIFY(objects->definitions().isEmpty());
  }
  void receiveOnlyAndFailedKillPauseLiveSchedule() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); MainWindow window(options(server.serverPort(), false));
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection(); QTRY_VERIFY(peer->bytesAvailable() >= 108); peer->readAll();
    auto *objects = window.findChild<ObjectControl *>(); qint64 now = QDateTime::currentMSecsSinceEpoch(); objects->setClock([&] { return now; });
    QString error; QVERIFY(objects->saveDefinition(definition(), &error)); window.findChild<QAction *>("PULDNTNT06")->trigger();
    QVERIFY(!objects->sendNow("Aid Point", &error)); QVERIFY(error.contains("disabled"));
    QVERIFY(objects->setRepeat("Aid Point", true)); now += 20000; objects->process(); QVERIFY(objects->repeating("Aid Point"));
    QVERIFY(!objects->kill("Aid Point", &error)); QVERIFY(!objects->repeating("Aid Point"));
    QVERIFY(objects->definitions().first().alive); QVERIFY(window.stationModel()->record("Aid Point")); QCOMPARE(peer->bytesAvailable(), qint64(0));
  }
  void editorDraftsCollisionAndRestartWithoutTransmission() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
      MainWindow window(options()); auto *objects = window.findChild<ObjectControl *>(); objects->setClock([now] { return now; });
      QSignalSpy observed(window.findChild<PacketMonitor *>(), &PacketMonitor::packetObserved);
      window.findChild<QAction *>("manageOwnObjects")->trigger(); auto *manager = window.findChild<QDialog *>("ownObjectManager"); QVERIFY(manager);
      manager->findChild<QPushButton *>("newOwnObject")->click(); auto *editor = window.findChild<QDialog *>("ownObjectEditor"); QVERIFY(editor);
      editor->findChild<QLineEdit *>("objectName")->setText("Tent"); editor->findChild<QComboBox *>("objectKind")->setCurrentIndex(1);
      editor->findChild<QDoubleSpinBox *>("objectLatitude")->setValue(-27.5); editor->findChild<QDoubleSpinBox *>("objectLongitude")->setValue(-59.25);
      editor->findChild<QLineEdit *>("objectSymbol")->setText("-");
      editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
      QCOMPARE(objects->definitions().size(), 1); QCOMPARE(observed.count(), 0);
      QCOMPARE(objects->information(objects->definitions().first()), QByteArray(")Tent!2730.00S/05915.00W-"));
      QVERIFY(window.stationModel()->record("Tent")->flag & ST_ITEM);
      QVERIFY(objects->setRepeat("Tent", true)); QVERIFY(objects->repeating("Tent"));
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      manager->findChild<QTableWidget *>("ownObjectTable")->selectRow(0);
      manager->findChild<QPushButton *>("editOwnObject")->click(); editor = window.findChild<QDialog *>("ownObjectEditor"); QVERIFY(editor);
      QVERIFY(editor->findChild<QLineEdit *>("objectName")->isReadOnly());
      editor->findChild<QLineEdit *>("objectComment")->setText("Updated draft");
      editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
      QCOMPARE(objects->definitions().first().comment, QString("Updated draft"));
      QVERIFY(!objects->repeating("Tent")); QCOMPARE(observed.count(), 0);
      QVERIFY(objects->setRepeat("Tent", true));
      const auto count = window.stationModel()->database()->count;
      QString error; auto invalid = definition(); invalid.item = true; invalid.name = "A!B"; QVERIFY(!objects->saveDefinition(invalid, &error));
      invalid = definition(); invalid.comment = QString("bad") + QChar(0) + "tail"; QVERIFY(!objects->saveDefinition(invalid, &error));
      window.stationModel()->applyPacket("REMOTE>APRS:!4903.50N/07201.75W>", 'T');
      auto collision = definition(); collision.name = "REMOTE"; QVERIFY(!objects->saveDefinition(collision, &error));
      QCOMPARE(window.stationModel()->database()->count, count + 1);
    }
    MainWindow restored(options()); auto *objects = restored.findChild<ObjectControl *>();
    QCOMPARE(objects->definitions().size(), 1); QVERIFY(!objects->repeating("Tent"));
    QCOMPARE(qint64(restored.stationModel()->record("Tent")->sec_heard), now / 1000);
    QSignalSpy observed(restored.findChild<PacketMonitor *>(), &PacketMonitor::packetObserved);
    objects->process(); QTest::qWait(30); QCOMPARE(observed.count(), 0);
  }
  void otherOwnerStopsRepeatAndLegacyImport() {
    MainWindow window(options()); auto *objects = window.findChild<ObjectControl *>(); QString error;
    QVERIFY(objects->saveDefinition(definition(), &error)); QVERIFY(objects->setRepeat("Aid Point", true));
    window.stationModel()->applyPacket("OTHER>APRS:;Aid Point*191200z4903.50N/07201.75WhOther owner", 'T');
    window.findChild<QAction *>("PULDNTNT06")->trigger();
    QVERIFY(!objects->sendNow("Aid Point", &error)); QVERIFY(error.contains("another")); QVERIFY(!objects->repeating("Aid Point"));
    QTemporaryDir output; QFile file(output.filePath("legacy.cnf")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("DISABLE_OBJECT_TX:0\nOBJECT_RATE:600\n"); file.close();
    XastirConfig config; QVERIFY(readXastirConfig(file.fileName(), config, &error)); QVERIFY(!config.objectTransmitDisabled); QCOMPARE(config.objectIntervalSeconds, 600);
  }
  void savedDraftDoesNotReplaceNewerArchivedReception() {
    QTemporaryDir output; auto o = options(); o.historyEnabled = true; o.history.path = output.filePath("history.sqlite");
    const auto now = QDateTime::currentMSecsSinceEpoch();
    {
      MainWindow window(o); auto *objects = window.findChild<ObjectControl *>(); objects->setClock([now] { return now; });
      QString error; QVERIFY(objects->saveDefinition(definition(), &error));
      window.historyStore()->recordPacket("LOCAL>APRS:;Aid Point*191234z4904.50N/07201.75WhNewer reception", 3, now + 60000);
      window.historyStore()->flush();
    }
    MainWindow restored(o);
    const auto *row = restored.stationModel()->record("Aid Point"); QVERIFY(row);
    QCOMPARE(qint64(row->sec_heard), (now + 60000) / 1000);
    QVERIFY(qAbs(row->latitude - (49 + 4.5/60)) < .00001);
    QVERIFY(!restored.findChild<ObjectControl *>()->repeating("Aid Point"));
  }
};
QTEST_MAIN(ObjectTest)
#include "test_objects.moc"
