// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "message_service.h"
#include <QAction>
#include <QDialog>
#include <QJsonObject>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

class BulletinTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settings_;
private slots:
  void initTestCase()
  {
    QCoreApplication::setOrganizationName("XastirBulletinTests"); QCoreApplication::setApplicationName("Bulletins");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
  }
  void init() { QSettings().clear(); }
  void receiveUpdateAndBrowse()
  {
    ApplicationOptions options; options.config.callsign = "LOCAL";
    MainWindow window(options); auto *service = window.messageService();
    int sent = 0; service->setTransport([&](const auto &, int, const auto &, auto *) { ++sent; return true; });
    service->setAutoReplyText("Away"); service->setAutoReply(true);
    QSignalSpy incoming(service, &MessageService::incomingMessage);
    auto *action = window.findChild<QAction *>("PULDNVI001"); QVERIFY(action && action->property("implemented").toBool()); action->trigger();
    auto *dialog = window.findChild<QDialog *>("bulletinBrowser"); QVERIFY(dialog);
    auto *table = dialog->findChild<QTableView *>("bulletinTable"); QVERIFY(table); QCOMPARE(table->model()->rowCount(), 0);
    service->receivePacket("REMOTE>APRS::BLN1     :Road closed{A1}", 0);
    service->receiveInternetPacket("GATE>APRS:}REMOTE>APRS::BLN2GROUP:Club meeting", 0, false);
    service->receiveInternetPacket("REMOTE>APRS::BLNA     :Announcement", 0, true);
    QTRY_COMPARE(table->model()->rowCount(), 3);
    QCOMPARE(sent, 0); QCOMPARE(incoming.count(), 0); QVERIFY(service->autoReply());
    auto *filter = dialog->findChild<QLineEdit *>("bulletinFilter"); filter->setText("Road");
    QCOMPARE(table->model()->rowCount(), 1); QCOMPARE(table->model()->index(0, 5).data().toString(), QString("Road closed"));
    table->setCurrentIndex(table->model()->index(0, 0)); const auto key = table->currentIndex().data(Qt::UserRole + 1);
    service->receivePacket("REMOTE>APRS::BLN1     :Road open", 0);
    QTRY_COMPARE(table->model()->index(0, 5).data().toString(), QString("Road open"));
    QCOMPARE(table->currentIndex().data(Qt::UserRole + 1), key);
    QCOMPARE(service->messages().size(), 3);
    filter->setText("GROUP"); QCOMPARE(table->model()->rowCount(), 1);
    QCOMPARE(table->model()->index(0, 4).data().toString(), QString("APRS-IS"));
    filter->setText("Announcement"); QCOMPARE(table->model()->rowCount(), 1);
    QCOMPARE(table->model()->index(0, 3).data().toString(), QString("Announcement"));
    action->trigger(); QCOMPARE(window.findChildren<QDialog *>("bulletinBrowser").size(), 1);
    dialog->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    action->trigger(); dialog = window.findChild<QDialog *>("bulletinBrowser"); QVERIFY(dialog);
    QTRY_COMPARE(dialog->findChild<QTableView *>("bulletinTable")->model()->rowCount(), 3);
  }
  void archiveRestartAndRetention()
  {
    QTemporaryDir directory; HistoryOptions options; options.path = directory.filePath("history.sqlite");
    const auto recorded = QDateTime::currentMSecsSinceEpoch() - 9LL * 86400000;
    {
      HistoryStore store(options); QVERIFY(store.open());
      qint64 now = recorded; MessageService service("LOCAL", nullptr, [&] { return now; });
      service.receivePacket("REMOTE>APRS::BLN1     :Original", 0); store.saveMessages(&service);
      ++now; service.receiveInternetPacket("REMOTE>APRS::BLN1     :Updated", now); store.saveMessages(&service);
      store.recordPacket("REMOTE>APRS::BLN1     :Updated", 256, now);
      store.prune(); store.flush(); QVERIFY(store.packets().isEmpty());
      QCOMPARE(store.messages("LOCAL").size(), 1);
    }
    HistoryStore store(options); QVERIFY(store.open());
    StationModel stations; MessageService service("LOCAL");
    int sent = 0; service.setTransport([&](const auto &, int, const auto &, auto *) { ++sent; return true; });
    service.setAutoReplyText("Away"); service.setAutoReply(true); QSignalSpy incoming(&service, &MessageService::incomingMessage);
    QVERIFY(store.restore(&stations, &service)); QCOMPARE(service.messages().size(), 1);
    const auto &bulletin = service.messages().first();
    QCOMPARE(bulletin.bulletin, 1); QCOMPARE(bulletin.text, QString("Updated")); QCOMPARE(bulletin.timestamp, recorded + 1);
    QVERIFY(!bulletin.pending && !bulletin.retryable); QCOMPARE(bulletin.device, 256);
    service.processRetries(); QCOMPARE(sent, 0); QCOMPARE(incoming.count(), 0);
  }
  void malformedAndOldArchive()
  {
    qint64 now = 10000; MessageService service("LOCAL", nullptr, [&] { return now; });
    service.receivePacket("REMOTE>APRS::BLN1    :Bad padding", 0);
    service.receiveInternetPacket("REMOTE>APRS::BLN1     :Bad|text"); QVERIFY(service.messages().isEmpty());
    service.receivePacket("REMOTE>APRS::BLN1     :ackA1", 0); QCOMPARE(service.messages().size(), 1);
    QCOMPARE(service.messages().first().text, QString("ackA1"));
    auto records = service.archiveRecords(); auto old = records.first().toObject(); old.remove("bulletin");
    records = {old}; MessageService restored("LOCAL"); restored.restoreArchive(records, "00", 2);
    QCOMPARE(restored.messages().size(), 1); QCOMPARE(restored.messages().first().bulletin, 1);
  }
};
QTEST_MAIN(BulletinTest)
#include "test_bulletins.moc"
