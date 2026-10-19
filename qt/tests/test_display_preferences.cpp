// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "station_details.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

class DisplayPreferencesTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settings_;
  static ApplicationOptions options()
  {
    ApplicationOptions value;
    value.config.callsign = "LOCAL";
    Station local; local.callsign = "LOCAL"; local.position = {0, 0}; value.config.localStation = local;
    value.center = {0, .01}; value.zoom = 14; return value;
  }
  static void apply(QDialog *dialog) { dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click(); }
private slots:
  void initTestCase()
  {
    QCoreApplication::setOrganizationName("XastirDisplayTests"); QCoreApplication::setApplicationName("Display");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
  }
  void init() { QSettings().clear(); }
  void formatBoundaries()
  {
    DisplayPreferences preferences;
    QCOMPARE(preferences.position({0, 0}), QString("00.00000N, 000.00000W"));
    QVERIFY(preferences.position({-34.5, 58.25}).contains("34.50000S"));
    QVERIFY(preferences.position({-34.5, 58.25}).contains("058.25000E"));
    auto value = preferences.value(); value.coordinateSystem = 1; preferences.setValue(value);
    QCOMPARE(preferences.position({-34.5, 58.25}), QString("34°30.000S, 058°15.000E"));
    value.coordinateSystem = 2; preferences.setValue(value);
    QCOMPARE(preferences.position({90, -180}), QString("90°00'00.0N, 180°00'00.0W"));
    QVERIFY(preferences.position({-90, 180}).contains("90°00'00.0S"));
    QCOMPARE(preferences.latitude(NAN), QString("—"));
    QCOMPARE(preferences.longitude(181), QString("—"));
  }
  void coordinateApplyCancelAndRestore()
  {
    {
      MainWindow window(options()); auto *map = window.findChild<MapWidget *>();
      auto *action = window.findChild<QAction *>("PULDNCF002"); QVERIFY(action->property("implemented").toBool());
      action->trigger(); auto *dialog = window.findChild<QDialog *>("configureCoordinates"); QVERIFY(dialog);
      auto *choice = dialog->findChild<QComboBox *>("coordinateSystem"); choice->setCurrentIndex(1);
      QVERIFY(dialog->findChild<QLabel *>("coordinatePreview")->text().contains("°"));
      QCOMPARE(map->preferences()->value().coordinateSystem, 0); dialog->reject();
      QVERIFY(!QSettings().contains("display/coordinateSystem"));
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      action->trigger(); dialog = window.findChild<QDialog *>("configureCoordinates");
      dialog->findChild<QComboBox *>("coordinateSystem")->setCurrentIndex(2); apply(dialog);
      QCOMPARE(map->preferences()->value().coordinateSystem, 2);
      QVERIFY(window.findChild<QLabel *>("mapPositionStatus")->text().contains("°"));
      window.findChild<QAction *>("COORD001")->trigger();
      auto *calculator = window.findChild<QDialog *>("mapCoordinatesDialog");
      QVERIFY(calculator->findChild<QLabel *>("coordinatePreferred")->text().contains("°"));
      QVERIFY(calculator->findChild<QDoubleSpinBox *>("mapLatitude"));
    }
    MainWindow restored(options()); QCOMPARE(restored.findChild<MapWidget *>()->preferences()->value().coordinateSystem, 2);
  }
  void unitsUpdateExistingDetailsTablesAndMap()
  {
    MainWindow window(options()); auto *map = window.findChild<MapWidget *>(); map->setMapsEnabled(false);
    auto *stations = window.stationModel();
    stations->applyPacket("MOBILE>APRS:!0000.00N/00000.60E>090/010/A=000328", 'I');
    stations->applyPacket("WX>APRS:_10121200c180s010g020t050h50b10000p012", 'I');
    window.findChild<StationDetails *>()->open("MOBILE");
    auto *text = window.findChild<QLabel *>("stationDetailsText");
    QVERIFY(text->text().contains("18.5 km/h")); QVERIFY(text->text().contains("100.0 m"));
    window.findChild<QAction *>("PULDNVI005")->trigger();
    auto *weather = window.findChild<QDialog *>("stationList2")->findChild<QTableView *>("stationListTable");
    QTRY_COMPARE(weather->model()->rowCount(), 1);
    QCOMPARE(weather->model()->index(0, 11).data().toDouble(), 10.0);
    auto *english = window.findChild<QAction *>("PULDNUT001"); QVERIFY(english->property("implemented").toBool());
    english->trigger(); QVERIFY(english->isChecked());
    QVERIFY(text->text().contains("11.5 mph")); QVERIFY(text->text().contains("328.0 ft"));
    QCOMPARE(weather->model()->headerData(11, Qt::Horizontal).toString(), QString("Temperature (°F)"));
    QCOMPARE(weather->model()->index(0, 11).data().toDouble(), 50.0);
    QCOMPARE(weather->model()->index(0, 12).data().toDouble(), 10.0);
    QCOMPARE(weather->model()->index(0, 16).data().toDouble(), 29.53);
    QCOMPARE(weather->model()->index(0, 17).data().toDouble(), .12);
    QCOMPARE(weather->model()->index(0, 11).data(Qt::UserRole).toDouble(), 50.0);
    QCOMPARE(QString::fromLatin1(stations->record("MOBILE")->speed), QString("010"));
    QCOMPARE(QString::fromLatin1(stations->record("WX")->weather_data->wx_temp), QString("050"));
    DisplayPreferences restored; QVERIFY(restored.value().englishUnits);
    auto value = restored.value(); value.coordinateSystem = 1; map->preferences()->setValue(value);
    QVERIFY(text->text().contains("00°00.000N"));
  }
  void cursorDistanceBearingAndLocalPosition()
  {
    MainWindow window(options()); auto *map = window.findChild<MapWidget *>(); map->setMapsEnabled(false);
    window.show();
    auto *action = window.findChild<QAction *>("PULDNDB001"); QVERIFY(action->property("implemented").toBool()); action->trigger();
    const QPoint point(map->width() / 2, map->height() / 2);
    QTest::mouseMove(map, point);
    auto *status = window.findChild<QLabel *>("mapPositionStatus");
    QTRY_VERIFY(status->text().contains("1.11 km / 90°"));
    window.findChild<QAction *>("PULDNUT001")->trigger(); QVERIFY(status->text().contains(" yd / 90°"));
    action->trigger(); QVERIFY(!status->text().contains(" / "));
    QVERIFY(!QSettings().value("display/distanceBearing").toBool());
    QEvent leave(QEvent::Leave); QCoreApplication::sendEvent(map, &leave);
    QVERIFY(!map->statusText().contains(" / "));
    // Without a configured local position the cursor must not invent an origin.
    MainWindow unconfigured(ApplicationOptions{}); auto *other = unconfigured.findChild<MapWidget *>();
    auto value = other->preferences()->value(); value.distanceBearing = true; other->preferences()->setValue(value);
    QMouseEvent move(QEvent::MouseMove, QPointF(100, 100), QPointF(100, 100), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(other, &move); QVERIFY(!other->statusText().contains(" / "));
  }
  void legacyImportAndInvalidQtSettings()
  {
    QFile legacy(settings_.filePath("legacy.cnf")); QVERIFY(legacy.open(QIODevice::WriteOnly));
    legacy.write("COORDINATE_SYSTEM:1\nDISPLAY_UNITS_ENGLISH:1\nDISPLAY_DIST_BEAR_STATUS:1\n"); legacy.close();
    auto value = options(); QString error; QVERIFY(readXastirConfig(legacy.fileName(), value.config, &error));
    QSettings().setValue("display/coordinateSystem", 5); QSettings().setValue("display/englishUnits", "bad");
    MainWindow window(value); const auto preferences = window.findChild<MapWidget *>()->preferences()->value();
    QCOMPARE(preferences.coordinateSystem, 1); QVERIFY(preferences.englishUnits); QVERIFY(preferences.distanceBearing);
  }
};
QTEST_MAIN(DisplayPreferencesTest)
#include "test_display_preferences.moc"
