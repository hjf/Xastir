// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "message_service.h"
#include "station_navigator.h"
#include <QAction>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

class StationViewsTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settings_;
  static ApplicationOptions options()
  {
    ApplicationOptions options;
    options.config.callsign = "LOCAL";
    Station local; local.callsign = "LOCAL"; local.position = {49, -72};
    options.config.localStation = local;
    return options;
  }
  static void seed(StationModel *stations)
  {
    const auto now = QDateTime::currentSecsSinceEpoch();
    stations->applyPacket("FIXED>APRS:!4903.50N/07201.75W-Home", 'I', now - 300);
    stations->applyPacket("MOBILE>APRS:!4903.50N/07201.75W>090/010Mobile", 'T', now - 120);
    stations->applyPacket("MOBILE>APRS:!4903.51N/07201.75W>090/010Mobile", 'T', now - 60);
    stations->applyPacket("RFONLY>APRS:!4903.50N/07201.75W-Local", 'T', now - 240);
    stations->applyPacket("MIXED>APRS:!4903.50N/07201.75W-Mixed", 'T', now - 230);
    stations->applyPacket("MIXED>APRS:>Internet update", 'I', now - 30);
    stations->applyPacket("WXONLY>APRS:_10121200c180s010g020t050h50b10123p012", 'I', now - 50);
    stations->applyPacket("LOCAL>APRS:_10121200c090s005g010t060h60b10000", 'I', now - 40);
    stations->applyPacket("LOCAL>APRS:;Aid Point*101200z4903.50N/07201.75W-Own", 'I', now - 20);
    stations->applyPacket("OWNER>APRS:)ITEM!4903.50N/07201.75W-Other", 'I', now - 10);
    stations->applyPacket("NOFIX>APRS:>Waiting for a position", 'I', now + 1);
  }
  static QStringList names(QTableView *table)
  {
    QStringList names;
    for (int row = 0; row < table->model()->rowCount(); ++row) names << table->model()->index(row, 0).data(Qt::UserRole).toString();
    return names;
  }
  static QTableView *open(MainWindow &window, const char *id, int type)
  {
    window.findChild<QAction *>(id)->trigger();
    auto *dialog = window.findChild<QDialog *>(QString("stationList%1").arg(type));
    return dialog ? dialog->findChild<QTableView *>("stationListTable") : nullptr;
  }
private slots:
  void initTestCase()
  {
    QCoreApplication::setOrganizationName("XastirStationViewsTests");
    QCoreApplication::setApplicationName("StationViews");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
  }
  void init() { QSettings().clear(); }
  void categories_data()
  {
    QTest::addColumn<QByteArray>("action"); QTest::addColumn<int>("type"); QTest::addColumn<QStringList>("expected");
    const QStringList all{"Aid Point", "FIXED", "ITEM", "LOCAL", "MIXED", "MOBILE", "NOFIX", "RFONLY", "WXONLY"};
    QTest::newRow("all") << QByteArray("PULDNVI004") << 0 << all;
    QTest::newRow("mobile") << QByteArray("PULDNVI003") << 1 << QStringList{"MOBILE"};
    QTest::newRow("weather") << QByteArray("PULDNVI005") << 2 << QStringList({"LOCAL", "WXONLY"});
    QTest::newRow("local") << QByteArray("PULDNVI009") << 3 << QStringList({"MIXED", "MOBILE", "RFONLY"});
    QTest::newRow("last") << QByteArray("PULDNVI012") << 4 << QStringList({"NOFIX", "ITEM", "Aid Point", "MIXED", "LOCAL", "WXONLY", "MOBILE", "RFONLY", "FIXED"});
    QTest::newRow("objects") << QByteArray("LHPUPNI005") << 5 << QStringList({"Aid Point", "ITEM"});
    QTest::newRow("owned") << QByteArray("LHPUPNI006") << 6 << QStringList{"Aid Point"};
    QTest::newRow("own-weather") << QByteArray("PULDNVI008") << 7 << QStringList{"LOCAL"};
  }
  void categories()
  {
    QFETCH(QByteArray, action); QFETCH(int, type); QFETCH(QStringList, expected);
    MainWindow window(options()); seed(window.stationModel());
    auto *map = window.findChild<MapWidget *>();
    auto selection = map->selections(); selection.none = 1; map->setSelections(selection);
    auto *menu = window.findChild<QAction *>(QString::fromLatin1(action));
    QVERIFY(menu && menu->property("implemented").toBool());
    auto *table = open(window, action.constData(), type); QVERIFY(table);
    QTRY_COMPARE(names(table), expected);
    QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
    QVERIFY(!window.findChild<QDialog *>("notImplementedDialog"));
    // Reinvoking a menu action reuses its open window.
    QCOMPARE(open(window, action.constData(), type), table);
  }
  void filteringDetailsAndNavigation()
  {
    MainWindow window(options()); seed(window.stationModel());
    int transmissions = 0;
    window.messageService()->setTransport([&](const auto &, int, const auto &, auto *) { ++transmissions; return true; });
    auto *table = open(window, "PULDNVI004", 0); QVERIFY(table);
    QTRY_COMPARE(table->model()->rowCount(), 9);
    auto *dialog = window.findChild<QDialog *>("stationList0");
    auto *filter = dialog->findChild<QLineEdit *>("stationListFilter");
    auto *details = dialog->findChild<QPushButton *>("stationListDetails");
    auto *center = dialog->findChild<QPushButton *>("stationListCenter");
    auto *follow = dialog->findChild<QPushButton *>("stationListFollow");
    filter->setText("waiting for a position");
    QCOMPARE(names(table), QStringList{"NOFIX"}); table->setCurrentIndex(table->model()->index(0, 0));
    QVERIFY(details->isEnabled()); QVERIFY(!center->isEnabled()); QVERIFY(!follow->isEnabled());
    details->click();
    auto *text = window.findChild<QLabel *>("stationDetailsText"); QVERIFY(text);
    QVERIFY(text->text().contains("Position: not yet received"));
    QVERIFY(text->text().contains("Waiting for a position"));
    filter->setText("mobile"); QCOMPARE(names(table), QStringList{"MOBILE"});
    table->setCurrentIndex(table->model()->index(0, 0));
    QVERIFY(center->isEnabled()); QVERIFY(follow->isEnabled()); follow->click();
    auto *navigator = window.findChild<StationNavigator *>(); QCOMPARE(navigator->followedName(), QString("MOBILE"));
    auto *map = window.findChild<MapWidget *>();
    QVERIFY(qAbs(map->centerCoordinate().latitude - (49 + 3.51 / 60)) < .00001);
    filter->clear();
    window.stationModel()->applyPacket("AAA>APRS:>New arrival", 'I');
    QTRY_COMPARE(table->model()->rowCount(), 10);
    QCOMPARE(table->currentIndex().siblingAtColumn(0).data(Qt::UserRole).toString(), QString("MOBILE"));
    filter->setText("does not exist"); QVERIFY(!details->isEnabled()); QVERIFY(!center->isEnabled());
    QCOMPARE(transmissions, 0);
  }
  void updatesKillsAndClearing()
  {
    MainWindow window(options()); seed(window.stationModel());
    auto *objects = open(window, "LHPUPNI005", 5); auto *owned = open(window, "LHPUPNI006", 6);
    QVERIFY(objects && owned);
    QTRY_COMPARE(objects->model()->rowCount(), 2); QTRY_COMPARE(owned->model()->rowCount(), 1);
    window.stationModel()->applyPacket("LOCAL>APRS:;Aid Point_101200z4903.50N/07201.75W-Gone", 'I');
    QTRY_COMPARE(objects->model()->rowCount(), 1); QTRY_COMPARE(owned->model()->rowCount(), 0);
    QCOMPARE(names(objects), QStringList{"ITEM"});
    window.stationModel()->clear();
    QTRY_COMPARE(objects->model()->rowCount(), 0);
    // Dialog deletion removes all table callbacks; a new dialog gets fresh rows.
    window.findChild<QDialog *>("stationList5")->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    window.stationModel()->applyPacket("OWNER>APRS:)NEW!4903.50N/07201.75W-New", 'I');
    objects = open(window, "LHPUPNI005", 5); QVERIFY(objects);
    QTRY_COMPARE(names(objects), QStringList{"NEW"});
  }
  void batchedScanSurvivesReplacement()
  {
    MainWindow window(options());
    auto *stations = window.stationModel();
    for (int i = 0; i < 400; ++i)
      stations->applyPacket(QString("S%1>APRS:>Status").arg(i, 4, 10, QChar('0')).toLatin1(), 'I');
    auto *table = open(window, "PULDNVI004", 0); QVERIFY(table);
    QTimer::singleShot(0, stations, [stations] { stations->clear(); stations->applyPacket("KEEP>APRS:>Replacement", 'I'); });
    QTRY_COMPARE(names(table), QStringList{"KEEP"});
  }
  void weatherColumnsAndOwnWeather()
  {
    MainWindow window(options()); seed(window.stationModel());
    auto *table = open(window, "PULDNVI005", 2); QVERIFY(table); QTRY_COMPARE(table->model()->rowCount(), 2);
    const auto *model = table->model();
    QCOMPARE(model->index(1, 0).data().toString(), QString("WXONLY"));
    QCOMPARE(model->index(1, 11).data().toDouble(), 10.0);
    QCOMPARE(model->index(1, 12).data().toDouble(), 16.09344);
    QCOMPARE(model->index(1, 16).data().toDouble(), 1012.3);
    QCOMPARE(model->index(1, 17).data().toDouble(), 3.048);
    QVERIFY(!table->isColumnHidden(11)); QVERIFY(table->isColumnHidden(9));
    table = open(window, "PULDNVI008", 7); QVERIFY(table); QTRY_COMPARE(names(table), QStringList{"LOCAL"});
    QVERIFY(qAbs(table->model()->index(0, 11).data().toDouble() - 15.5555556) < .00001);
  }
  void sourceDestroyedWithSelectedRow()
  {
    MainWindow window(options()); seed(window.stationModel());
    auto *table = open(window, "PULDNVI004", 0); QVERIFY(table); QTRY_COMPARE(table->model()->rowCount(), 9);
    table->setCurrentIndex(table->model()->index(0, 0));
    auto *dialog = window.findChild<QDialog *>("stationList0");
    QVERIFY(dialog->findChild<QPushButton *>("stationListDetails")->isEnabled());
    delete window.stationModel();
    QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY(!dialog->findChild<QPushButton *>("stationListDetails")->isEnabled());
    QVERIFY(!dialog->findChild<QPushButton *>("stationListCenter")->isEnabled());
    dialog->close();
  }
  void uptimeAndAloha()
  {
    MainWindow window(options());
    auto *uptime = window.findChild<QAction *>("PULDNVI013"); QVERIFY(uptime->property("implemented").toBool()); uptime->trigger();
    auto *running = window.findChild<QLabel *>("applicationUptimeText"); QVERIFY(running);
    QVERIFY(running->text().contains("Station records: 1")); const auto first = running->text();
    QTRY_VERIFY(running->text() != first);
    auto *aloha = window.findChild<QAction *>("PULDNVI016"); QVERIFY(aloha->property("implemented").toBool());
    auto *stations = window.stationModel();
    for (int i = 0; i < 130; ++i) {
      Station mobile; mobile.callsign = QString("M%1").arg(i, 4, 10, QChar('0'));
      mobile.position = {49, -72 + (i + 1) * .001}; mobile.symbolCode = '>'; mobile.speed = "10"; mobile.dataVia = 'T';
      QVERIFY(stations->upsert(mobile));
    }
    // An Internet-only digipeater must not contribute to the radio calculation.
    stations->applyPacket("NET>APRS:!4900.00N/07200.00W#Internet only", 'I');
    aloha->trigger(); auto *stats = window.findChild<QLabel *>("alohaStatisticsText"); QVERIFY(stats);
    QVERIFY(stats->text().contains("Stations considered: 120")); QVERIFY(stats->text().contains("Mobiles in motion: 120"));
    QVERIFY(stats->text().contains("Digipeaters: 0")); QVERIFY(!stats->text().contains("indeterminate"));
    stations->clear(); window.findChild<QPushButton *>("alohaRefresh")->click();
    QVERIFY(stats->text().contains("Configure your station"));
    stations->upsert(*options().config.localStation); window.findChild<QPushButton *>("alohaRefresh")->click();
    QVERIFY(stats->text().contains("indeterminate")); QVERIFY(stats->text().contains("Stations considered: 0"));
  }
};
QTEST_MAIN(StationViewsTest)
#include "test_station_views.moc"
