// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_menu.h"
#include "station_navigator.h"
#include "station_model.h"
#include "map_widget.h"
#include "tile_source.h"
#include "legacy_menus.h"
#include "aprs_decoder.h"
#include <QAction>
#include <QMenu>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>
class StationMenuTest : public QObject {
  Q_OBJECT
  QTemporaryDir settingsDirectory_;
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("XastirStationTests");QCoreApplication::setApplicationName("StationMenu");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDirectory_.path());
  }
  void init() { QSettings().clear(); }
  void projectionRetainsStoredMetadata() {
    StationModel stations;
    stations.applyPacket("WX>APRS:_10091300c180s005g010t032h50b10000", 'T', 100);
    stations.applyPacket("WX>APRS:!4903.50N/07201.75W_PHG5130first", 'T', 101);
    const auto first = stations.stations().value("WX");
    QVERIFY(first.weather);
    QCOMPARE(first.heardAt, qint64(101));
    QCOMPARE(first.powerGain, QString("PHG5130"));
    stations.applyPacket("WX>APRS,qAR,GATE:!4903.51N/07201.74W_", 'I', 102);
    const auto updated = stations.stations().value("WX");
    QVERIFY(updated.weather);
    QCOMPARE(updated.weather->wx_sec_time, time_t(100));
    QCOMPARE(updated.powerGain, first.powerGain);
    QCOMPARE(updated.comment, QString::fromLatin1(stations.record("WX")->comment_data->text_ptr));
    QCOMPARE(QString::fromLatin1(stations.record("WX")->comment_data->next->text_ptr), first.comment);
    QCOMPARE(updated.direct, bool(stations.record("WX")->flag & ST_DIRECT));
    QCOMPARE(updated.dataVia, 'I');
    QCOMPARE(updated.heardAt, qint64(102));
    // Ordinary live imports use the core's actual receive time, never zero.
    Station live; live.callsign = "LIVE";
    QVERIFY(stations.upsert(live));
    QCOMPARE(stations.stations().value("LIVE").heardAt, qint64(stations.record("LIVE")->sec_heard));
    QVERIFY(stations.stations().value("LIVE").heardAt > 0);
  }
  void killRequiresMatchingEntityKind() {
    StationModel stations;
    Station local; local.callsign = "LOCAL"; local.local = true;
    QVERIFY(stations.upsert(local));
    stations.applyPacket("OWNER>APRS:;LOCAL    _", 'T');
    stations.applyPacket("OWNER>APRS:)LOCAL_", 'T');
    QVERIFY(stations.stations().value("LOCAL").local);
    stations.applyPacket("OWNER>APRS:;Aid Point*092345z4903.50N/07201.75Wh", 'T');
    stations.applyPacket("OWNER>APRS:)Aid Point_", 'T');
    QVERIFY(stations.record("Aid Point"));
    stations.applyPacket("OWNER>APRS:;Aid Point_", 'T');
    QVERIFY(!stations.record("Aid Point"));
    stations.applyPacket("OWNER>APRS:)Item!4903.50N/07201.75Wh", 'T');
    stations.applyPacket("OWNER>APRS:)Item_", 'T');
    QVERIFY(!stations.record("Item"));
  }
  void actionsAndFiltering() {
    QMainWindow window;createLegacyMenus(&window);
    TileSource tiles({},&window);auto *map=new MapWidget(&tiles,{49.058333,-72.029167},14,&window);window.setCentralWidget(map);
    StationModel stations;map->setStationModel(&stations);stations.setOwnCallsign("LOCAL");
    StationNavigator navigator(&window,&stations,map);StationMenu menu(&window,&stations,map,"LOCAL",{},settingsDirectory_.path());
    auto *root=window.findChild<QMenu *>("MENUTB0005");QVERIFY(root);
    std::function<void(QMenu *)> inspect=[&](QMenu *parent) {
      for(auto *action:parent->actions()) {
        if(action->menu())inspect(action->menu());
        else if(!action->isSeparator()) QVERIFY(action->property("implemented").toBool() || (action->property("unavailable").toBool() && !action->isEnabled()));
      }
    };inspect(root);
    auto update=decodeAprs("REMOTE>APRS:!4903.50N/07201.75W>090/010/A=000100PHG5130");QVERIFY(update);
    update->station.dataVia='T';QVERIFY(stations.upsert(update->station));QVERIFY(map->stationVisible("REMOTE"));
    auto *tnc=window.findChild<QAction *>("PULDNDP042");tnc->trigger();QVERIFY(!map->stationVisible("REMOTE"));tnc->trigger();QVERIFY(map->stationVisible("REMOTE"));
    auto *none=window.findChild<QAction *>("PULDNDP040");none->trigger();QVERIFY(!map->stationVisible("REMOTE"));none->trigger();
    auto *moving=window.findChild<QAction *>("PULDNDP029");moving->trigger();QVERIFY(!map->stationVisible("REMOTE"));moving->trigger();
    auto *tactical=window.findChild<QAction *>("PULDNDP051");tactical->trigger();QVERIFY(!map->stationVisible("REMOTE"));
    stations.setTactical({{"REMOTE","RESCUE"}});QVERIFY(map->stationVisible("REMOTE"));tactical->trigger();
    window.resize(900,600);map->resize(900,600);
    auto image=[&]{QImage result(map->size(),QImage::Format_ARGB32);result.fill(Qt::transparent);map->render(&result);return result;};
    const auto baseline=image();window.findChild<QAction *>("PULDNDP010")->trigger();QVERIFY(image()!=baseline);
    window.findChild<QAction *>("PULDNDP010")->trigger();
    window.findChild<QAction *>("PULDNDP003")->trigger();window.findChild<QAction *>("PULDNDP004")->trigger();
    QVERIFY(image()!=baseline);
    const auto record=stations.record("REMOTE");QVERIFY(record);QCOMPARE(QString::fromLatin1(record->power_gain),QString("PHG5130"));
    // Enable all display overlays with real metadata; render must remain valid.
    auto wx=decodeAprs("WX>APRS:!4903.51N/07201.74W_090/010g020t032h50b10123");QVERIFY(wx && wx->station.weather);QVERIFY(stations.upsert(wx->station));
    auto positionless=decodeAprsWeather("WX>APRS:_10091300c180s005g010t-05h60b10000");QVERIFY(positionless);
    stations.updateWeather(positionless->callsign,positionless->weather,'T',positionless->direct);
    QCOMPARE(QString::fromLatin1(stations.record("WX")->weather_data->wx_temp),QString("-05"));
    QVERIFY(stations.record("WX")->flag&ST_VIATNC);
    positionless=decodeAprsWeather("NEW>APRS:_10091300c180s005g010t032h50b10000");QVERIFY(positionless);
    stations.updateWeather(positionless->callsign,positionless->weather,'T',positionless->direct);
    QVERIFY(!stations.stations().contains("NEW"));QVERIFY(stations.record("NEW")->weather_data);
    auto newPosition=decodeAprs("NEW>APRS:!4903.51N/07201.74W_");QVERIFY(newPosition);QVERIFY(stations.upsert(newPosition->station));
    QVERIFY(stations.stations().contains("NEW") && stations.record("NEW")->weather_data);
    update->station.position.latitude+=.0001;update->station.heardAt=QDateTime::currentSecsSinceEpoch()-30;QVERIFY(stations.upsert(update->station));
    for(auto *action:root->findChildren<QAction *>())if(action->isCheckable() && action->objectName()!="PULDNDP040" && !action->isChecked())action->trigger();
    QVERIFY(!image().isNull());
  }
  void historiesExportAndClear() {
    QMainWindow window;createLegacyMenus(&window);TileSource tiles({},&window);
    auto *map=new MapWidget(&tiles,{0,0},10,&window);window.setCentralWidget(map);
    StationModel stations;map->setStationModel(&stations);stations.setOwnCallsign("LOCAL");
    QTemporaryDir directory;QVERIFY(directory.isValid());
    Station local;local.callsign="LOCAL";local.local=true;QVERIFY(stations.upsert(local));
    StationMenu menu(&window,&stations,map,"LOCAL",local,directory.path());
    QFile objects(directory.filePath("object.log"));QVERIFY(objects.open(QIODevice::WriteOnly));
    objects.write("# comment\n;Aid Point*092345z4903.50N/07201.75Wh\n)Item!4903.51N/07201.74W-\n");objects.close();
    QString error;QVERIFY2(menu.reloadObjects(objects.fileName(),&error),qPrintable(error));
    QVERIFY(stations.stations().contains("Aid Point") && stations.stations().contains("Item"));
    QVERIFY(stations.record("Aid Point")->flag&ST_MYOBJITEM);
    QFile tactical(directory.filePath("tactical_calls.log"));QVERIFY(tactical.open(QIODevice::WriteOnly));tactical.write("LOCAL,HOME\nItem,RESCUE\nItem,TEAM\n");tactical.close();
    QVERIFY(menu.reloadTactical(tactical.fileName(),&error));QCOMPARE(QString::fromUtf8(stations.record("Item")->tactical_call_sign),QString("TEAM"));
    const auto kml=directory.filePath("stations.kml");QVERIFY(menu.exportKml(kml,&error));QFile exported(kml);QVERIFY(exported.open(QIODevice::ReadOnly));
    QXmlStreamReader xml(exported.readAll());int names=0;while(!xml.atEnd()){xml.readNext();if(xml.isStartElement() && xml.name()=="Placemark")++names;}QVERIFY(!xml.hasError());QCOMPARE(names,3);
    auto run=[&](const char *id){window.findChild<QAction *>(id)->trigger();auto *box=window.findChild<QMessageBox *>();QVERIFY(box);box->done(QMessageBox::Yes);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);};
    run("PULDNDP049");QVERIFY(!stations.record("Item")->tactical_call_sign);
    QVERIFY(tactical.open(QIODevice::ReadOnly));QVERIFY(tactical.readAll().startsWith('#'));tactical.close();
    run("PULDNDP050");QCOMPARE(QFileInfo(tactical).size(),qint64(0));
    run("PULDNDP025");QCOMPARE(QFileInfo(objects).size(),qint64(0));QVERIFY(stations.stations().contains("Item"));
    run("PULDNDP015");QCOMPARE(stations.stations().size(),1);QVERIFY(stations.record("LOCAL")->flag&ST_MYSTATION);
  }
  void finduImportPreservesNewerPosition() {
    QMainWindow window;TileSource tiles({},&window);auto *map=new MapWidget(&tiles,{0,0},10,&window);window.setCentralWidget(map);
    StationModel stations;map->setStationModel(&stations);StationMenu menu(&window,&stations,map,"LOCAL",{},settingsDirectory_.path());
    Station current;current.callsign="REMOTE";current.position={40,20};QVERIFY(stations.upsert(current));
    QString error;
    const auto data=QByteArray("<html>\n20200109120000,REMOTE&gt;APRS:!4903.50N/07201.75W&gt;090/010<br>20200109120100,REMOTE&gt;APRS:!4903.51N/07201.74W&gt;090/010<br>\n</html>");
    QCOMPARE(menu.importFindu(data,"REMOTE",&error),2);
    QCOMPARE(stations.stations().value("REMOTE").position.latitude,40.0);
    const auto *row=stations.record("REMOTE");QVERIFY(row->oldest_trackpoint && row->newest_trackpoint);
    QVERIFY(row->oldest_trackpoint->sec < row->newest_trackpoint->sec);
    stations.clearTrails();QVERIFY(!stations.record("REMOTE")->oldest_trackpoint);
    QCOMPARE(menu.importFindu("server error","REMOTE",&error),-1);
    QCOMPARE(menu.importFindu(data,"OTHER",&error),-1);
  }
};
QTEST_MAIN(StationMenuTest)
#include "test_station_menu.moc"
