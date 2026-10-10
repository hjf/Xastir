// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "station_model.h"
#include "station_navigator.h"
#include <QAction>
#include <QLineEdit>
#include <QListWidget>
#include <QtTest>
class StationNavigatorTest : public QObject {
  Q_OBJECT
private slots:
  void searchAndFollow() {
    ApplicationOptions options;
    MainWindow window(options);
    auto *navigator = window.findChild<StationNavigator *>();
    auto *map = window.findChild<MapWidget *>();
    auto *stations = window.stationModel();
    QVERIFY(navigator && map);
    Station station; station.callsign = "REMOTE-1"; station.position = {30, 40};
    QVERIFY(stations->upsert(station));
    Station object; object.callsign = "Aid Point"; object.kind = StationKind::Object;
    object.sourceStation = "OWNER"; object.position = {10, 20};
    QVERIFY(stations->upsert(object));
    auto *find = window.findChild<QAction *>("PULDNDP014");
    auto *track = window.findChild<QAction *>("PULDNDP001");
    QVERIFY(find->property("implemented").toBool());
    QVERIFY(track->property("implemented").toBool()); find->trigger();
    auto *filter = window.findChild<QLineEdit *>("stationSearchFilter");
    auto *list = window.findChild<QListWidget *>("stationSearchResults");
    QVERIFY(filter && list); QCOMPARE(list->count(), 2);
    filter->setText("owner"); QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), QString("Aid Point"));
    filter->setText("unknown"); QCOMPARE(list->count(), 0);
    QVERIFY(!navigator->locate("unknown", true));
    QVERIFY(navigator->locate("REMOTE-1", true));
    QCOMPARE(navigator->followedName(), QString("REMOTE-1"));
    station.position = {31, 41}; QVERIFY(stations->upsert(station));
    QVERIFY(qAbs(map->centerCoordinate().latitude - 31) < 0.00001);
    const int zoom = map->zoomLevel(); map->setView({0, 0}, zoom + 1);
    QCOMPARE(map->zoomLevel(), zoom + 1);
    QVERIFY(qAbs(map->centerCoordinate().longitude - 41) < 0.00001);
    QTest::keyClick(map, Qt::Key_Right); QVERIFY(navigator->followedName().isEmpty());
    QVERIFY(navigator->locate("Aid Point", true));
    QVERIFY(stations->remove("Aid Point", StationKind::Object));
    QVERIFY(navigator->followedName().isEmpty());
    QVERIFY(navigator->locate("REMOTE-1", true));
    QVERIFY(navigator->locate("REMOTE-1", false));
    QVERIFY(navigator->followedName().isEmpty());
    QVERIFY(navigator->locate("REMOTE-1", true));
    stations->clear(); QVERIFY(navigator->followedName().isEmpty());
  }
};
QTEST_MAIN(StationNavigatorTest)
#include "test_station_navigator.moc"
