// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "map_projection.h"
#include "tile_source.h"
#include "vector_maps.h"
#include "vector_map.h"
#include <QPainter>
#include <QComboBox>
#include <QCheckBox>
#include "aprs_symbols.h"
#include "station_model.h"
#include "station_db.h"
#include "agwpe_client.h"
#include <QtEndian>

#include <QAction>
#include <QSettings>
#include <QInputDialog>
#include <QTimer>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QBuffer>
#include <QDirIterator>
#include <QUrlQuery>
#include <QDateTime>
#include <QImage>
#include <QFile>
#include <QNetworkProxy>
#include <QPointer>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <cmath>
#include <limits>

namespace
{
QByteArray requestHeader(const QByteArray &request, const QByteArray &name)
{
  for (const auto &line : request.split('\n')) {
    const int separator = line.indexOf(':');
    if (separator > 0 && line.left(separator).toLower() == name.toLower()) return line.mid(separator + 1).trimmed();
  }
  return {};
}

const QColor TileColor(35, 145, 100);

// An actual HTTP server exercises asynchronous downloads without contacting OSM.
class TileServer : public QObject
{
public:
  enum Response { Image, NotFound, InvalidImage, Unauthorized, Redirect };
  QTcpServer server;
  QList<QByteArray> requests;
  QList<QPointer<QTcpSocket>> held;
  Response response = Image;
  bool hold = false;
  QByteArray redirectUrl, extraHeaders;
  bool redirectOnce = false;
  QSize imageSize{map::TileSize, map::TileSize};
  bool patterned = false;

  TileServer()
  {
    connect(&server, &QTcpServer::newConnection, this, [this] {
      while (server.hasPendingConnections())
      {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
          QByteArray request = socket->property("request").toByteArray() + socket->readAll();
          socket->setProperty("request", request);
          if (!request.contains("\r\n\r\n") || socket->property("handled").toBool())
            return;
          socket->setProperty("handled", true);
          requests.append(request);
          if (hold)
            held.append(socket);
          else
            respond(socket);
        });
      }
    });
    server.listen(QHostAddress::LocalHost);
  }

  QString urlTemplate() const
  {
    return QStringLiteral("http://127.0.0.1:%1/{z}/{x}/{y}.png").arg(server.serverPort());
  }

  void respond(QTcpSocket *socket)
  {
    if (socket->state() != QAbstractSocket::ConnectedState)
      return;
    QByteArray body;
    if (response == Image)
    {
      QImage image(imageSize, QImage::Format_RGB32);
      image.fill(TileColor);
      if (patterned) {
        QPainter painter(&image);
        const int w = image.width()/2, h = image.height()/2;
        painter.fillRect(QRect(0,0,w,h), Qt::red); painter.fillRect(QRect(w,0,w,h), Qt::green);
        painter.fillRect(QRect(0,h,w,h), Qt::blue); painter.fillRect(QRect(w,h,w,h), Qt::cyan);
      }
      QBuffer buffer(&body);
      buffer.open(QIODevice::WriteOnly);
      image.save(&buffer, "PNG");
    }
    else
      body = "Not a map image";
    QByteArray header = response == NotFound ? "HTTP/1.1 404 Not Found\r\n" : "HTTP/1.1 200 OK\r\n";
    if (response == Unauthorized) header = "HTTP/1.1 401 Unauthorized\r\n";
    if (response == Redirect) header = "HTTP/1.1 302 Found\r\nLocation: " + redirectUrl + "\r\n";
    header += extraHeaders;
    header += "Content-Type: image/png\r\nCache-Control: public, max-age=604800\r\n";
    header += "Date: " + QDateTime::currentDateTimeUtc().toString("ddd, dd MMM yyyy hh:mm:ss 'GMT'").toLatin1() + "\r\n";
    header += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    if (response == Redirect && redirectOnce) response = Image;
    socket->write(header + body);
    socket->disconnectFromHost();
  }

  void release()
  {
    hold = false;
    for (const auto &socket : held)
      if (socket)
        respond(socket);
    held.clear();
  }
};
}

class MapTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settingsDirectory_;

private slots:
  void init() { QSettings().clear(); }
  void legacyMenuStructureAndPlaceholders()
  {
    TileServer server;
    QTemporaryDir cache;
    ApplicationOptions options;
    options.tiles = {server.urlTemplate(), cache.path()};
    MainWindow window(options);
    window.show();
    QStringList roots;
    for (auto *action : window.menuBar()->actions()) {
      QVERIFY(action->menu()); roots.append(action->text().remove('&'));
    }
    QCOMPARE(roots, QStringList({"File", "View", "Map", "Station", "Message", "Interface", "Help"}));
    auto *mapConfigure = window.findChild<QMenu *>("MENUTB0004");
    QVERIFY(mapConfigure);
    auto *background = mapConfigure->findChild<QMenu *>("PULDNMP005");
    QVERIFY(background); QCOMPARE(background->actions().size(), 12);
    // All leaf actions share the placeholder behavior, including toggles and Exit.
    int leaves = 0, toggles = 0;
    for (auto *action : window.findChildren<QAction *>()) {
      if (action->property("legacyMenuId").isValid()) {
        ++leaves;
        if (action->isCheckable()) ++toggles;
      }
      if (!action->property("legacyMenuId").isValid() || action->property("implemented").toBool()
          || action->property("unavailable").toBool()) continue;
      action->trigger();
      auto *dialog = window.findChild<QMessageBox *>("notImplementedDialog");
      QVERIFY(dialog); QVERIFY(dialog->isVisible());
      QCOMPARE(dialog->text(), QStringLiteral("Not implemented yet"));
      QCOMPARE(dialog->informativeText(), action->property("legacyLabel").toString());
      QVERIFY(!action->isChecked());
      dialog->accept();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QVERIFY(window.isVisible());
    }
    QCOMPARE(leaves, 183); QCOMPARE(toggles, 111);
    window.close();
  }

  void receivedObjectsRenderUpdateAndDisappear()
  {
    TileServer server;
    QTcpServer agwpe;
    QVERIFY(agwpe.listen(QHostAddress::LocalHost));
    QTemporaryDir cache;
    ApplicationOptions options;
    options.center = {49 + 3.5 / 60, -(72 + 1.75 / 60)};
    options.zoom = 12;
    options.tiles = {server.urlTemplate(), cache.path()};
    MainWindow window(options);
    window.resize(640, 480);
    auto *canvas = window.findChild<MapWidget *>();
    // This test isolates icon movement; retained trails have their own coverage.
    auto display = canvas->displayOptions(); display.trail = false; canvas->setDisplayOptions(display);
    window.show();
    QTRY_COMPARE(window.statusBar()->currentMessage(), QStringLiteral("Map ready"));
    const QImage background = canvas->grab().toImage();
    AgwpeSettings settings;
    settings.host = "127.0.0.1"; settings.port = agwpe.serverPort();
    AgwpeClient client(settings, window.stationModel());
    client.start(); QTRY_VERIFY(agwpe.hasPendingConnections());
    auto *peer = agwpe.nextPendingConnection();
    auto sendObject = [peer](const QByteArray &info) {
      QByteArray payload(1, '\0');
      for (const auto &name : {QByteArray("APRS  "), QByteArray("ORIGIN")}) {
        for (char c : name) payload += char(c << 1);
        payload += char(name == "ORIGIN" ? 0x61 : 0x60);
      }
      payload += char(3); payload += char(0xf0); payload += info;
      QByteArray header(36, '\0'); header[4] = 'K';
      qToLittleEndian<quint32>(quint32(payload.size()), header.data() + 28);
      peer->write(header + payload);
    };
    sendObject(";Aid Point*092345z4903.50N/07201.75Wh");
    QTRY_VERIFY(window.stationModel()->stations().contains("Aid Point"));
    const Station object = window.stationModel()->stations().value("Aid Point");
    QCOMPARE(object.kind, StationKind::Object); QCOMPARE(object.sourceStation, "ORIGIN");
    AprsSymbols symbols;
    const QImage icon = symbols.image('/', 'h');
    QPoint opaque(-1, -1);
    for (int y = 0; y < 20 && opaque.x() < 0; ++y)
      for (int x = 0; x < 20; ++x)
        if (icon.pixelColor(x, y).alpha() == 255) { opaque = {x, y}; break; }
    QVERIFY(opaque.x() >= 0);
    const QPoint center = canvas->coordinateToScreen(object.position).toPoint();
    const QPoint pixel = center - QPoint(10, 10) + opaque;
    const QImage rendered = canvas->grab().toImage();
    QCOMPARE(rendered.pixelColor(pixel), icon.pixelColor(opaque));
    const QRect label(center + QPoint(12, -10), QSize(100, 22));
    QVERIFY(rendered.copy(label) != background.copy(label));
    sendObject(";Aid Point*092346z4904.50N/07201.75Wh");
    QTRY_VERIFY(window.stationModel()->stations().value("Aid Point").position.latitude > object.position.latitude);
    QCOMPARE(canvas->grab().toImage().pixelColor(pixel), background.pixelColor(pixel));
    sendObject(";Aid Point_092347z4904.50N/07201.75Wh");
    QTRY_VERIFY(!window.stationModel()->stations().contains("Aid Point"));
    QCOMPARE(canvas->grab().toImage(), background);
    client.stop(); window.close();
  }

  void agwpeUpdatesWindow()
  {
    TileServer tiles;
    QTcpServer agwpe;
    QVERIFY(agwpe.listen(QHostAddress::LocalHost));
    QTemporaryDir cache;
    ApplicationOptions options;
    options.tiles = {tiles.urlTemplate(), cache.path()};
    options.config.callsign = "LOCAL-3";
    Station local;
    local.callsign = options.config.callsign;
    local.position = {49, -72};
    options.config.localStation = local;
    AgwpeSettings settings;
    settings.host = "127.0.0.1";
    settings.port = agwpe.serverPort();
    options.config.agwpe.append(settings);
    auto *window = new MainWindow(options);
    window->show();
    QVERIFY(window->windowTitle().contains("LOCAL-3"));
    QCOMPARE(window->stationModel()->stations().size(), 1);
    QTRY_VERIFY(agwpe.hasPendingConnections());
    auto *peer = agwpe.nextPendingConnection();
    QByteArray payload(1, '\0');
    for (const auto &name : {QByteArray("APRS  "), QByteArray("REMOTE")}) {
      for (char c : name) payload += char(c << 1);
      payload += char(name == "REMOTE" ? 0x61 : 0x60);
    }
    payload += char(3); payload += char(0xf0);
    payload += "!4903.50N/07201.75W>";
    QByteArray header(36, '\0'); header[4] = 'K';
    qToLittleEndian<quint32>(quint32(payload.size()), header.data() + 28);
    peer->write(header + payload);
    QTRY_VERIFY(window->stationModel()->stations().contains("REMOTE"));
    QCOMPARE(window->stationModel()->stations()["REMOTE"].symbolCode, QChar('>'));
    QTRY_VERIFY(!tiles.requests.isEmpty());
    delete window;
    QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
  }

  void initTestCase()
  {
    QCoreApplication::setOrganizationName("XastirMapTests");
    QCoreApplication::setApplicationName("MapMenu");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
    // Deterministic local HTTP tests must not use the machine's proxy settings.
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
  }

  void mapMenuRenderingAndReload()
  {
    TileServer server;
    QTemporaryDir cache;
    ApplicationOptions options; options.center = {0,0}; options.zoom = 0;
    options.tiles = {server.urlTemplate(), cache.path()};
    MainWindow window(options); window.show();
    auto *canvas = window.findChild<MapWidget *>();
    auto *source = window.findChild<TileSource *>();
    QTRY_VERIFY(source->tile({0,0,0}));
    auto centerColor = [canvas] { const auto image = canvas->grab().toImage(); return image.pixelColor(image.width()/2, image.height()/2); };
    QCOMPARE(centerColor(), TileColor);
    window.findChild<QAction *>("PULDNMP013")->trigger();
    QVERIFY(!canvas->mapsEnabled());
    window.findChild<QAction *>("PULDNMBC12")->trigger(); QCOMPARE(centerColor(), QColor(Qt::black));
    window.findChild<QAction *>("PULDNMP013")->trigger();
    QCOMPARE(centerColor(), TileColor);
    window.findChild<QAction *>("0%")->trigger(); QCOMPARE(centerColor(), QColor(Qt::black));
    window.findChild<QAction *>("100%")->trigger(); QCOMPARE(centerColor(), TileColor);
    canvas->setMapGamma(2); QVERIFY(centerColor().green() > TileColor.green()); canvas->setMapGamma(1);
    const int before = server.requests.size();
    window.findChild<QAction *>("PULDNMP027")->trigger();
    QTRY_VERIFY(server.requests.size() > before); QTRY_VERIFY(source->tile({0,0,0}));
    QCOMPARE(centerColor(), TileColor);
    const int beforeFlush = server.requests.size();
    window.findChild<QAction *>("PULDNMP028")->trigger();
    QTRY_VERIFY(server.requests.size() > beforeFlush); QTRY_VERIFY(source->tile({0,0,0}));
    const QImage plain = canvas->grab().toImage();
    window.findChild<QAction *>("PULDNMP003")->trigger(); QVERIFY(canvas->grab().toImage() != plain);
    MainWindow restored(options);
    QVERIFY(restored.findChild<QAction *>("PULDNMP003")->isChecked());
    QVERIFY(restored.findChild<QAction *>("PULDNMBC12")->isChecked());
    QVERIFY(!window.findChild<QAction *>("PULDNMP011")->isVisible());
    QVERIFY(!window.findChild<QAction *>("POPUPMA046")->isEnabled());
  }

  void offlineChooserDoesNotRequestTiles()
  {
    TileServer server; QTemporaryDir cache;
    ApplicationOptions options; options.center = {0,0}; options.zoom = 2;
    options.tiles = {server.urlTemplate(), cache.path()};
    MainWindow window(options);
    auto *canvas = window.findChild<MapWidget *>();
    window.findChild<QAction *>("PULDNMP001")->trigger();
    auto *d = window.findChild<QDialog *>("mapChooserDialog"); QVERIFY(d);
    d->findChild<QComboBox *>("mapSource")->setCurrentIndex(1);
    QTest::mouseClick(d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
    QCOMPARE(canvas->baseMap(), QString("world"));
    window.show(); QTRY_VERIFY(canvas->vectorMapReady());
    const auto image = canvas->grab().toImage();
    canvas->setMapsEnabled(false); QVERIFY(canvas->grab().toImage() != image); canvas->setMapsEnabled(true);
    QVERIFY(server.requests.isEmpty());
    QVERIFY(!window.findChild<QAction *>("PULDNMP010")->isEnabled());
    QVERIFY(!window.findChild<QAction *>("PULDNMP027")->isEnabled());
    canvas->setView({-34.6,-58.4}, 8); QVERIFY(canvas->grab().toImage() != image);
    ApplicationOptions restoredOptions;
    MainWindow restored(restoredOptions);
    QCOMPARE(restored.findChild<MapWidget *>()->baseMap(), QString("world"));
    // Switching back to tiles restores their provider credit and controls.
    window.findChild<QAction *>("PULDNMP001")->trigger();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    d = window.findChild<QDialog *>("mapChooserDialog");
    d->findChild<QComboBox *>("mapSource")->setCurrentIndex(0);
    QCOMPARE(d->findChild<QLineEdit *>("mapAttribution")->text(), QString("© OpenStreetMap contributors"));
    QTest::mouseClick(d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
    QCOMPARE(canvas->baseMap(), QString("raster"));
    QTRY_VERIFY(!server.requests.isEmpty());
    QVERIFY(window.findChild<QAction *>("PULDNMP027")->isEnabled());
    // An explicit tile URL always overrides saved offline selection.
    QSettings().setValue("map/baseMap", "world");
    restoredOptions.tileUrlExplicit = true;
    MainWindow explicitRaster(restoredOptions);
    QCOMPARE(explicitRaster.findChild<MapWidget *>()->baseMap(), QString("raster"));
  }

  void naturalEarthLayersAndRendering()
  {
    if (!xastir_has_shapefile_support()) QSKIP("Shapelib unavailable");
    TileServer server; QTemporaryDir cache;
    ApplicationOptions options; options.center = {-34.6,-58.4}; options.zoom = 4;
    options.tiles = {server.urlTemplate(), cache.path()};
    MainWindow window(options);
    auto *canvas = window.findChild<MapWidget *>();
    window.findChild<QAction *>("PULDNMP001")->trigger();
    auto *d = window.findChild<QDialog *>("mapChooserDialog");
    d->findChild<QComboBox *>("mapSource")->setCurrentIndex(2);
    QTest::mouseClick(d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
    window.show(); QTRY_VERIFY_WITH_TIMEOUT(canvas->vectorMapReady(), 10000);
    auto *vectors = canvas->findChild<VectorMaps *>();
    QCOMPARE(vectors->scenes_.value("naturalearth")->shapes.size(), 2559); // One bundled feature is SHPT_NULL.
    const auto filled = canvas->grab().toImage();
    const auto renderDirectory = qEnvironmentVariable("XASTIR_VECTOR_TEST_RENDER_DIR");
    if (!renderDirectory.isEmpty()) filled.save(renderDirectory + "/naturalearth.png");
    window.findChild<QAction *>("PULDNMP009")->trigger(); QVERIFY(canvas->grab().toImage() != filled);
    canvas->setVectorLayers(0); const auto blank = canvas->grab().toImage();
    canvas->setVectorLayers(1); QVERIFY(canvas->grab().toImage() != blank);
    QVERIFY(server.requests.isEmpty());
    for (const auto &view : {map::Coordinate{0,179.9}, map::Coordinate{0,-179.9}, map::Coordinate{-70,0}}) {
      canvas->setVectorLayers(15); canvas->setView(view, 2); canvas->grab();
      if (!renderDirectory.isEmpty() && view.latitude == -70) {
        canvas->setVectorFills(true); canvas->grab().save(renderDirectory + "/antarctica.png");
      }
    }
  }

  void vectorMultipartHolesAndDateline()
  {
    if (!xastir_has_shapefile_support()) QSKIP("Shapelib unavailable");
    VectorMaps maps;
    QString error;
    auto polygon = VectorMaps::readShapefile(QString(VECTOR_DATA_DIR "/tests/core/fixtures/polygon_hole.shp"), 0, &error);
    QVERIFY2(polygon, qPrintable(error)); maps.scenes_.insert("fixture", polygon);
    QImage image(256,256,QImage::Format_RGB32); image.fill(Qt::white);
    const auto origin = map::toWorld({0,0},3) - QPointF(128,128);
    { QPainter painter(&image); maps.draw(painter,"fixture",origin,3,image.size(),15,true,false); }
    // Hole is water/background, surrounding polygon is land.
    QCOMPARE(image.pixelColor(128,128), QColor(Qt::white));
    QVERIFY(image.pixelColor(200,128) != QColor(Qt::white));
    auto line = VectorMaps::readShapefile(QString(VECTOR_DATA_DIR "/tests/core/fixtures/multipart_line.shp"), 1, &error);
    QVERIFY2(line, qPrintable(error)); maps.scenes_["fixture"] = line;
    image.fill(Qt::white);
    { QPainter painter(&image); maps.draw(painter,"fixture",origin,3,image.size(),15,false,false); }
    QCOMPARE(image.pixelColor(128,128), QColor(Qt::white)); // No line between separate parts.
    QVERIFY(image.pixelColor(43,128) != QColor(Qt::white));
    auto seam = VectorMaps::readShapefile(QString(VECTOR_DATA_DIR "/tests/core/fixtures/dateline_line.shp"), 1, &error);
    QVERIFY2(seam, qPrintable(error));
    QVERIFY(seam->shapes[0].path.boundingRect().width() < 2);
    maps.scenes_["fixture"] = seam; image.fill(Qt::white);
    { QPainter painter(&image); maps.draw(painter,"fixture",map::toWorld({0,180},3)-QPointF(128,128),3,image.size(),15,false,false); }
    QVERIFY(image.pixelColor(128,128) != QColor(Qt::white));
  }

  void darkMapBorderConfigurationAndPersistence()
  {
    TileServer server; QTemporaryDir cache;
    ApplicationOptions options; options.center = {0,0}; options.zoom = 0;
    options.tiles = {server.urlTemplate(),cache.path()};
    MainWindow window(options); auto *canvas = window.findChild<MapWidget *>();
    window.show(); auto *tiles = window.findChild<TileSource *>();
    QTRY_VERIFY(tiles->tile({0,0,0}));
    const qint64 tileIdentity = tiles->tile({0,0,0})->cacheKey();
    window.findChild<QAction *>("PULDNMP003")->trigger();
    window.findChild<QAction *>("PULDNMP031")->trigger();
    const auto light = canvas->grab().toImage(); QCOMPARE(light.pixelColor(1,1), QColor(Qt::white));
    auto openChooser = [&window] {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      window.findChild<QAction *>("PULDNMP001")->trigger();
      return window.findChild<QDialog *>("mapChooserDialog");
    };
    auto *dialog = openChooser(); QVERIFY(dialog);
    dialog->findChild<QComboBox *>("mapOverlayTheme")->setCurrentIndex(1);
    QVERIFY(!canvas->darkMapMode());
    dialog->reject(); QVERIFY(!canvas->darkMapMode());
    dialog = openChooser();
    dialog->findChild<QComboBox *>("mapOverlayTheme")->setCurrentIndex(1);
    QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
    QVERIFY(canvas->darkMapMode()); QVERIFY(QSettings().value("map/darkMode").toBool());
    QVERIFY(tiles->tile({0,0,0})); QCOMPARE(tiles->tile({0,0,0})->cacheKey(), tileIdentity);
    const auto dark = canvas->grab().toImage(); const int band = canvas->mapFrameWidth();
    QCOMPARE(dark.pixelColor(1,1), QColor("#20262b"));
    QCOMPARE(dark.pixelColor(dark.width()-2,dark.height()-2), QColor("#20262b"));
    auto brightText = [&dark](QRect area) {
      int count = 0;
      for (int y=area.top(); y<=area.bottom(); ++y) for (int x=area.left(); x<=area.right(); ++x) {
        const QColor color = dark.pixelColor(x,y);
        if (color.red()>150 && color.green()>150 && color.blue()>150) ++count;
      }
      return count;
    };
    QVERIFY(brightText(QRect(band,0,dark.width()-2*band,band-5))>30);
    QVERIFY(brightText(QRect(band,dark.height()-band+6,dark.width()-2*band,band-6))>30);
    QVERIFY(brightText(QRect(0,band,band-5,dark.height()-2*band))>30);
    QVERIFY(brightText(QRect(dark.width()-band+5,band,band-5,dark.height()-2*band))>30);
    auto *credit = canvas->findChild<QLabel *>(); QVERIFY(credit);
    QVERIFY(credit->styleSheet().contains("#20262b")); QVERIFY(credit->styleSheet().contains("#e6edf3"));
    MainWindow restored(options); QVERIFY(restored.findChild<MapWidget *>()->darkMapMode());
    dialog = openChooser(); dialog->findChild<QComboBox *>("mapOverlayTheme")->setCurrentIndex(0);
    dialog->findChild<QLineEdit *>("mapTileUrl")->setText("invalid URL");
    QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
    QVERIFY(canvas->darkMapMode()); QVERIFY(dialog->isVisible());
    // Correcting the configuration applies the light choice again.
    dialog->findChild<QLineEdit *>("mapTileUrl")->setText(server.urlTemplate());
    QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
    QVERIFY(!canvas->darkMapMode()); QCOMPARE(canvas->grab().toImage().pixelColor(1,1), QColor(Qt::white));
  }

  void mapBorderIsLabeledViewportFrame_data()
  {
    QTest::addColumn<double>("latitude");
    QTest::addColumn<double>("longitude");
    QTest::addColumn<int>("zoom");
    QTest::newRow("city") << -34.6037 << -58.3816 << 10;
    QTest::newRow("east-dateline") << 20.0 << 179.9 << 5;
    QTest::newRow("west-dateline") << -20.0 << -179.9 << 5;
    QTest::newRow("repeated-worlds") << 0.0 << 0.0 << 0;
    QTest::newRow("street") << -34.6037 << -58.3816 << 19;
  }

  void mapBorderIsLabeledViewportFrame()
  {
    QFETCH(double, latitude); QFETCH(double, longitude); QFETCH(int, zoom);
    ApplicationOptions options; options.center = {latitude, longitude}; options.zoom = zoom;
    MainWindow window(options);
    auto *canvas = window.findChild<MapWidget *>();
    canvas->setMapsEnabled(false); canvas->setMapBackground(TileColor);
    window.resize(900, 650); window.show();
    auto *grid = window.findChild<QAction *>("PULDNMP003");
    auto *border = window.findChild<QAction *>("PULDNMP031");
    QVERIFY(!border->isEnabled());
    grid->trigger(); QVERIFY(border->isEnabled());
    border->trigger(); QVERIFY(border->isChecked());
    const auto image = canvas->grab().toImage();
    // Each edge has a solid white band; coordinates are ink inside that band.
    const int band = canvas->mapFrameWidth();
    QVERIFY(band >= 14);
    QCOMPARE(image.pixelColor(1, 1), QColor(Qt::white));
    QCOMPARE(image.pixelColor(image.width()-2, 1), QColor(Qt::white));
    QCOMPARE(image.pixelColor(1, image.height()-2), QColor(Qt::white));
    QCOMPARE(image.pixelColor(image.width()-2, image.height()-2), QColor(Qt::white));
    auto inkCount = [&image](QRect area) {
      int count = 0;
      for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
          const auto color = image.pixelColor(x, y);
          if (color.red() < 128 && color.green() < 128 && color.blue() < 128) ++count;
        }
      return count;
    };
    // Leave tick marks out of the sampled areas: these must contain text.
    QVERIFY(inkCount(QRect(band, 0, image.width()-2*band, band-5)) > 30);
    QVERIFY(inkCount(QRect(band, image.height()-band+6, image.width()-2*band, band-6)) > 30);
    QVERIFY(inkCount(QRect(0, band, band-5, image.height()-2*band)) > 30);
    QVERIFY(inkCount(QRect(image.width()-band+5, band, band-5, image.height()-2*band)) > 30);
    canvas->setView({latitude, longitude + .01}, zoom);
    QVERIFY(canvas->grab().toImage() != image);
    grid->trigger(); QVERIFY(!border->isEnabled()); QVERIFY(border->isChecked());
    const auto noGrid = canvas->grab().toImage();
    QCOMPARE(noGrid.pixelColor(1, 1), TileColor);
    // Re-enabling the grid restores the saved border preference.
    grid->trigger(); QVERIFY(border->isEnabled());
    QCOMPARE(canvas->grab().toImage().pixelColor(1, 1), QColor(Qt::white));
  }

  void mapCoordinateDialogsAndBookmarks()
  {
    TileServer server; QTemporaryDir cache;
    ApplicationOptions options; options.tiles = {server.urlTemplate(), cache.path()};
    MainWindow window(options);
    auto *canvas = window.findChild<MapWidget *>();
    window.findChild<QAction *>("COORD001")->trigger();
    auto *d = window.findChild<QDialog *>("mapCoordinatesDialog"); QVERIFY(d);
    d->findChild<QDoubleSpinBox *>("mapLatitude")->setValue(-34.6037);
    d->findChild<QDoubleSpinBox *>("mapLongitude")->setValue(-58.3816);
    const auto result = d->findChild<QLabel *>("coordinateResult")->text();
    QVERIFY(result.contains("S")); QVERIFY(result.contains("W")); QVERIFY(result.contains("34"));
    d->findChild<QSpinBox *>("mapZoom")->setValue(10);
    QTest::mouseClick(d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
    QCOMPARE(canvas->zoomLevel(), 10);
    QVERIFY(std::abs(canvas->centerCoordinate().latitude + 34.6037) < 1e-6);
    QSettings().setValue("map/bookmarks", QVariantList{QVariantMap{{"name","Home"},{"latitude",12.5},{"longitude",179.8},{"zoom",6}}});
    window.findChild<QAction *>("PULDNMP012")->trigger();
    auto *bookmarks = window.findChild<QDialog *>("mapBookmarksDialog"); QVERIFY(bookmarks);
    QCOMPARE(bookmarks->findChild<QListWidget *>("mapBookmarks")->count(), 1);
    QTest::mouseClick(bookmarks->findChild<QPushButton *>("goMapBookmark"), Qt::LeftButton);
    QCOMPARE(canvas->zoomLevel(), 6); QVERIFY(std::abs(canvas->centerCoordinate().longitude - 179.8) < 1e-6);
    QTest::mouseClick(bookmarks->findChild<QPushButton *>("deleteMapBookmark"), Qt::LeftButton);
    QVERIFY(QSettings().value("map/bookmarks").toList().isEmpty());
    QTimer::singleShot(0, [] {
      auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
      QVERIFY(input); input->setTextValue("Saved view"); input->accept();
    });
    QTest::mouseClick(bookmarks->findChild<QPushButton *>("saveMapBookmark"), Qt::LeftButton);
    const auto saved = QSettings().value("map/bookmarks").toList();
    QCOMPARE(saved.size(), 1); QCOMPARE(saved[0].toMap().value("name").toString(), QString("Saved view"));
    QCOMPARE(saved[0].toMap().value("zoom").toInt(), 6);
  }

  void mapChooserValidationAndSourceSwitch()
  {
    TileServer first, second; QTemporaryDir cache;
    ApplicationOptions options; options.zoom = 0; options.tiles = {first.urlTemplate(), cache.path()};
    MainWindow window(options); window.show();
    auto *source = window.findChild<TileSource *>(); QTRY_VERIFY(source->tile({0,0,0}));
    window.findChild<QAction *>("PULDNMP001")->trigger();
    auto *d = window.findChild<QDialog *>("mapChooserDialog"); QVERIFY(d);
    auto *url = d->findChild<QLineEdit *>("mapTileUrl");
    url->setText("bad URL");
    auto *ok = d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    QTest::mouseClick(ok, Qt::LeftButton);
    QVERIFY(!d->findChild<QLabel *>("mapChooserError")->text().isEmpty()); QCOMPARE(source->urlTemplate(), first.urlTemplate());
    url->setText(second.urlTemplate()); d->findChild<QLineEdit *>("mapAttribution")->setText("Test map credit");
    QTest::mouseClick(ok, Qt::LeftButton);
    QTRY_VERIFY(!second.requests.isEmpty()); QTRY_VERIFY(source->tile({0,0,0}));
    QCOMPARE(source->urlTemplate(), second.urlTemplate());
    QCOMPARE(window.findChild<MapWidget *>()->mapAttribution(), QString("Test map credit"));
    QCOMPARE(QSettings().value("map/tileUrl").toString(), second.urlTemplate());
  }

  void projectionRoundTrip_data()
  {
    QTest::addColumn<double>("latitude");
    QTest::addColumn<double>("longitude");
    QTest::newRow("equator") << 0.0 << 0.0;
    QTest::newRow("Buenos Aires") << -34.6037 << -58.3816;
    QTest::newRow("London") << 51.5074 << -0.1278;
    QTest::newRow("near antimeridian") << 70.0 << 179.999;
    QTest::newRow("south limit") << -map::MaxLatitude << -179.999;
  }

  void projectionRoundTrip()
  {
    QFETCH(double, latitude);
    QFETCH(double, longitude);
    for (const int zoom : {0, 3, 10, 19})
    {
      const auto coordinate = map::fromWorld(map::toWorld({latitude, longitude}, zoom), zoom);
      QVERIFY(std::abs(coordinate.latitude - latitude) < 1e-8);
      QVERIFY(std::abs(coordinate.longitude - longitude) < 1e-8);
    }
  }

  void projectionLimits()
  {
    QCOMPARE(map::toWorld({0, 0}, 0), QPointF(128, 128));
    QCOMPARE(map::wrapTileX(-1, 3), 7);
    QCOMPARE(map::wrapTileX(8, 3), 0);
    QCOMPARE(map::wrapTileX(-123, 0), 0);
    const auto north = map::fromWorld(map::toWorld({90, 540}, 3), 3);
    QVERIFY(std::abs(north.latitude - map::MaxLatitude) < 1e-8);
    QCOMPARE(north.longitude, -180.0);
    QCOMPARE(map::constrainCenter({-1, -10}, 0), QPointF(255, 0));
    QCOMPARE(map::constrainCenter({257, 300}, 0), QPointF(1, 256));
  }

  void originalSymbolsAndOverlays()
  {
    AprsSymbols symbols;
    QVERIFY(symbols.contains('/', '>'));
    QVERIFY(symbols.contains('/', '-'));
    QVERIFY(symbols.contains('\\', '&'));
    QVERIFY(symbols.contains('A', '#'));
    QVERIFY(!symbols.contains('/', '"'));
    const QImage car = symbols.image('/', '>');
    QCOMPARE(car.size(), QSize(20, 20));
    QCOMPARE(car.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(symbols.image('/', '>', 270), car);
    QVERIFY(symbols.image('/', '>', 90) != car);
    QCOMPARE(symbols.image('/', '-', 90), symbols.image('/', '-'));
    QVERIFY(symbols.image('A', '#') != symbols.image('\\', '#'));
    QVERIFY(symbols.image('/', '#') != symbols.image('\\', '#'));
    QVERIFY(!symbols.image('/', '"').isNull());
    QCOMPARE(symbols.image('/', '"'), symbols.image('/', QChar()));
  }

  void stationModelUpdatesAndValidation()
  {
    StationModel model;
    QSignalSpy changes(&model, &StationModel::stationsChanged);
    QString error;
    QVERIFY2(model.loadFile(QStringLiteral(STATION_EXAMPLE_FILE), &error), qPrintable(error));
    QCOMPARE(model.stations().size(), 8);
    QCOMPARE(model.database()->count, 8);
    QCOMPARE(changes.size(), 1);
    Station station = model.stations().value(QStringLiteral("DEMO-HOME"));
    station.callsign = " demo-home ";
    station.position.latitude = -34.6;
    QVERIFY(model.upsert(station));
    QCOMPARE(model.stations().size(), 8);
    QCOMPARE(model.stations().value("DEMO-HOME").position.latitude, -34.6);
    QCOMPARE(changes.size(), 2);
    station.position.latitude = std::numeric_limits<double>::quiet_NaN();
    QVERIFY(!model.upsert(station, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(changes.size(), 2);
    station.position.latitude = 0;
    station.symbolTable = '?';
    QVERIFY(!model.upsert(station));
    station.symbolTable = '/';
    station.course = 360;
    QVERIFY(!model.upsert(station));
    QVERIFY(model.remove("demo-car"));
    QVERIFY(!model.remove("demo-car"));
    QCOMPARE(model.stations().size(), 7);
    model.clear();
    QCOMPARE(model.stations().size(), 0);
    QCOMPARE(model.database()->count, 0);
    QCOMPARE(changes.size(), 4);
    model.clear();
    QCOMPARE(changes.size(), 4);
  }

  void stationSnapshotsAreAtomic()
  {
    StationModel model;
    QString error;
    QVERIFY(model.loadFile(QStringLiteral(STATION_EXAMPLE_FILE), &error));
    QTemporaryDir directory;
    QFile file(directory.filePath("stations.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[{\"callsign\":\"NEW\",\"latitude\":0,\"longitude\":0,\"symbol\":\"/-\"},"
               "{\"callsign\":\"INVALID\",\"latitude\":0,\"longitude\":181,\"symbol\":\"/>\"}]");
    file.close();
    QVERIFY(!model.loadFile(file.fileName(), &error));
    QVERIFY(error.contains("Station 2"));
    QCOMPARE(model.stations().size(), 8);
    QVERIFY(!model.stations().contains("NEW"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("[{\"callsign\":\"n0call-1\",\"latitude\":0,\"longitude\":0,\"symbol\":\"/-\"},"
               "{\"callsign\":\"N0CALL-1\",\"latitude\":1,\"longitude\":2,\"symbol\":\"A#\",\"course\":90}]");
    file.close();
    QVERIFY2(model.loadFile(file.fileName(), &error), qPrintable(error));
    QCOMPARE(model.stations().size(), 1);
    const auto station = model.stations().first();
    QCOMPARE(station.position.latitude, 1.0);
    QCOMPARE(station.position.longitude, 2.0);
    QCOMPARE(station.symbolTable, QChar('A'));
    QCOMPARE(station.course.value(), 90.0);
  }

  void clickStationDetails()
  {
    ApplicationOptions options; options.center={0,0}; options.zoom=3;
    MainWindow window(options); window.show();
    auto *canvas=window.findChild<MapWidget *>();
    Station station; station.callsign="DETAIL"; station.position={0,0}; station.comment="Initial report";
    QVERIFY(window.stationModel()->upsert(station));
    QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(qRound(canvas->width()/2.0),qRound(canvas->height()/2.0)));
    auto *dialog=window.findChild<QDialog *>("stationDetails"); QVERIFY(dialog); QVERIFY(dialog->isVisible());
    auto *text=dialog->findChild<QLabel *>("stationDetailsText"); QVERIFY(text->text().contains("DETAIL")); QVERIFY(text->text().contains("Initial report"));
    station.comment="Updated report"; QVERIFY(window.stationModel()->upsert(station)); QVERIFY(text->text().contains("Updated report"));
    window.stationModel()->remove("DETAIL"); QVERIFY(text->text().contains("no longer"));
  }

  void stationIconsRenderAndMove()
  {
    TileServer server;
    QVERIFY(server.server.isListening());
    QTemporaryDir cache;
    ApplicationOptions options;
    options.center = {0, 0};
    options.zoom = 3;
    options.tiles = {server.urlTemplate(), cache.path()};
    MainWindow window(options);
    window.resize(640, 480);
    auto *canvas = window.findChild<MapWidget *>();
    QVERIFY(canvas);
    window.show();
    QTRY_COMPARE(window.statusBar()->currentMessage(), QStringLiteral("Map ready"));
    const QImage background = canvas->grab().toImage();
    const int requests = server.requests.size();
    Station station{"N0CALL-1", {0, 0}, '/', '-', std::nullopt};
    AprsSymbols symbols;
    const QImage icon = symbols.image('/', '-');
    QPoint opaque;
    bool found = false;
    for (int y = 0; y < icon.height() && !found; ++y)
      for (int x = 0; x < icon.width(); ++x)
        if (icon.pixelColor(x, y).alpha() == 255)
        {
          opaque = {x, y};
          found = true;
          break;
        }
    QVERIFY(found);
    const QPoint midpoint(qRound(canvas->width() / 2.0), qRound(canvas->height() / 2.0));
    const QPoint pixel = midpoint - QPoint(10, 10) + opaque;
    QVERIFY(window.stationModel()->upsert(station));
    const QImage rendered = canvas->grab().toImage();
    QCOMPARE(rendered.pixelColor(pixel), icon.pixelColor(opaque));
    QVERIFY(rendered.copy(QRect(midpoint + QPoint(12, -10), QSize(100, 22)))
            != background.copy(QRect(midpoint + QPoint(12, -10), QSize(100, 22))));

    station.position.longitude = 20;
    QVERIFY(window.stationModel()->upsert(station));
    const QImage moved = canvas->grab().toImage();
    QCOMPARE(moved.pixelColor(pixel), background.pixelColor(pixel));
    const QPointF screen = canvas->coordinateToScreen(station.position);
    const QPoint newPixel(qRound(screen.x()) - 10 + opaque.x(), qRound(screen.y()) - 10 + opaque.y());
    QCOMPARE(moved.pixelColor(newPixel), icon.pixelColor(opaque));
    QCOMPARE(server.requests.size(), requests); // Station changes do not reload tiles.
    canvas->setView(station.position, 4);
    const QPointF centered = canvas->coordinateToScreen(station.position);
    QCOMPARE(centered, QPointF(canvas->width() / 2.0, canvas->height() / 2.0));
    window.close();
  }

  void crowdedStationsHaveSeparateClickableCards_data()
  {
    QTest::addColumn<double>("longitude");
    QTest::newRow("center") << 0.0;
    QTest::newRow("left-edge") << -25.0;
    QTest::newRow("right-edge") << 25.0;
  }

  void crowdedStationsHaveSeparateClickableCards()
  {
    QFETCH(double, longitude);
    TileServer server;
    QTemporaryDir cache;
    TileSource tiles({server.urlTemplate(), cache.path()});
    MapWidget canvas(&tiles, {0, 0}, 4);
    canvas.resize(640, 480);
    StationModel model;
    for (int i = 0; i < 12; ++i)
      QVERIFY(model.upsert({QString("CLOSE-%1").arg(i, 2, 10, QChar('0')), {0, longitude}, '/', '-', std::nullopt}));
    canvas.setStationModel(&model);
    canvas.show();
    const QImage rendered = canvas.grab().toImage();
    QCOMPARE(canvas.stationHits_.size(), 12);
    const QPointF location = canvas.coordinateToScreen({0, longitude});
    int displaced = 0;
    QSignalSpy clicks(&canvas, &MapWidget::stationClicked);
    const auto placements = canvas.stationHits_;
    for (int i = 0; i < placements.size(); ++i) {
      const auto &hit = placements[i];
      QVERIFY(canvas.rect().contains(hit.bounds.toAlignedRect()));
      QCOMPARE(hit.location, location);
      for (int j = 0; j < i; ++j)
        QVERIFY(!hit.bounds.intersects(placements[j].bounds));
      if (!hit.bounds.contains(location)) ++displaced;
      QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, hit.bounds.center().toPoint());
      QCOMPARE(clicks.last().first().toString(), hit.name);
    }
    QVERIFY(displaced >= 11);
    QCOMPARE(rendered.pixelColor(location.toPoint()), QColor("#192d3b"));
    canvas.grab();
    for (int i = 0; i < placements.size(); ++i)
      QCOMPARE(canvas.stationHits_[i].bounds, placements[i].bounds);
    model.clear();
    canvas.grab();
    QVERIFY(canvas.stationHits_.isEmpty());
  }

  void crowdedViewportKeepsDotsWithoutOverlappingText()
  {
    TileServer server;
    QTemporaryDir cache;
    TileSource tiles({server.urlTemplate(), cache.path()});
    MapWidget canvas(&tiles, {0, 0}, 4);
    canvas.resize(360, 256);
    StationModel model;
    for (int i = 0; i < 100; ++i)
      QVERIFY(model.upsert({QString("DENSE-%1").arg(i), {0, 0}, '/', '-', std::nullopt}));
    canvas.setStationModel(&model);
    canvas.show();
    const auto rendered = canvas.grab().toImage();
    QVERIFY(!canvas.stationHits_.isEmpty());
    QVERIFY(canvas.stationHits_.size() < 100);
    for (int i = 0; i < canvas.stationHits_.size(); ++i)
      for (int j = 0; j < i; ++j)
        QVERIFY(!canvas.stationHits_[i].bounds.intersects(canvas.stationHits_[j].bounds));
    QCOMPARE(rendered.pixelColor(canvas.coordinateToScreen({0, 0}).toPoint()), QColor("#192d3b"));
    auto display = canvas.displayOptions();
    display.symbol = false; display.callsign = false;
    canvas.setDisplayOptions(display);
    canvas.grab();
    QVERIFY(canvas.stationHits_.isEmpty());
  }

  void stationWrappingMatchesTheMap()
  {
    TileServer server;
    QVERIFY(server.server.isListening());
    QTemporaryDir cache;
    TileSource tiles({server.urlTemplate(), cache.path()});
    MapWidget canvas(&tiles, {0, 179.9}, 3);
    canvas.resize(640, 480);
    const QPointF seam = canvas.coordinateToScreen({0, -179.9});
    QVERIFY(std::abs(seam.x() - (320 + 2048 * 0.2 / 360)) < 1e-8);
    QCOMPARE(seam.y(), 240.0);
    StationModel model;
    QVERIFY(model.upsert({"REPEAT", {0, 0}, '/', '-', std::nullopt}));
    canvas.setStationModel(&model);
    canvas.setView({0, 0}, 0);
    canvas.show();
    const QImage image = canvas.grab().toImage();
    // At zoom zero a wide viewport shows multiple copies of the same world.
    QCOMPARE(canvas.stationHits_.size(), 3);
    const auto hits = canvas.stationHits_;
    auto iconRect = [](const auto &hit) {
      return QRect(QPoint(qRound(hit.bounds.left() + 13) - 10,
                          qRound(hit.bounds.center().y()) - 10), QSize(20, 20));
    };
    // Edge copies can move inward to keep their labels fully visible.
    AprsSymbols symbols;
    const auto icon = symbols.image('/', '-');
    for (const auto &hit : hits)
      for (int y = 0; y < icon.height(); ++y)
        for (int x = 0; x < icon.width(); ++x)
          if (icon.pixelColor(x, y).alpha() == 255)
            QCOMPARE(image.pixelColor(iconRect(hit).topLeft() + QPoint(x, y)), icon.pixelColor(x, y));
    QCOMPARE(hits[1].location - hits[0].location, QPointF(256, 0));
    QCOMPARE(hits[2].location - hits[1].location, QPointF(256, 0));
    model.clear();
    QVERIFY(canvas.grab().toImage().copy(QRect(310, 230, 20, 20))
            != image.copy(QRect(310, 230, 20, 20)));
    canvas.close();
  }

  void rasterImageDensityPreservesMapCoordinates_data()
  {
    QTest::addColumn<int>("side");
    QTest::newRow("standard") << 256;
    QTest::newRow("MapTiler-default") << 512;
    QTest::newRow("retina") << 1024;
  }

  void rasterImageDensityPreservesMapCoordinates()
  {
    QFETCH(int, side);
    TileServer server; server.imageSize = {side,side}; server.patterned = true;
    QTemporaryDir cache;
    ApplicationOptions options; options.center = {0,0}; options.zoom = 0;
    const QString templateUrl = side == 1024 ? server.urlTemplate().replace(".png", "@2x.png") : server.urlTemplate();
    options.tiles = {templateUrl, cache.path()};
    MainWindow window(options); window.show();
    auto *source = window.findChild<TileSource *>();
    auto *canvas = window.findChild<MapWidget *>();
    QTRY_VERIFY(source->tile({0,0,0})); QCOMPARE(source->tile({0,0,0})->size(), QSize(side,side));
    const auto image = canvas->grab().toImage();
    if (side == 1024) QVERIFY(server.requests.first().contains("@2x.png"));
    const QPoint midpoint(image.width()/2,image.height()/2);
    const int offset = qRound(64 * image.devicePixelRatio());
    QCOMPARE(image.pixelColor(midpoint+QPoint(-offset,-offset)), QColor(Qt::red));
    QCOMPARE(image.pixelColor(midpoint+QPoint(offset,-offset)), QColor(Qt::green));
    QCOMPARE(image.pixelColor(midpoint+QPoint(-offset,offset)), QColor(Qt::blue));
    QCOMPARE(image.pixelColor(midpoint+QPoint(offset,offset)), QColor(Qt::cyan));
    // Geographic center and keyboard zoom retain their standard XYZ semantics.
    QCOMPARE(canvas->coordinateToScreen({0,0}), QPointF(canvas->width()/2.0,canvas->height()/2.0));
    QTest::keyClick(canvas, Qt::Key_Plus); QCOMPARE(canvas->zoomLevel(), 1);
    QVERIFY(std::abs(canvas->centerCoordinate().latitude) < 1e-8);
    QVERIFY(std::abs(canvas->centerCoordinate().longitude) < 1e-8);
  }

  void unsupportedRasterImageSize_data()
  {
    QTest::addColumn<QSize>("size");
    QTest::newRow("non-square") << QSize(256,512);
    QTest::newRow("unsupported") << QSize(300,300);
    QTest::newRow("oversized") << QSize(2048,2048);
  }
  void unsupportedRasterImageSize()
  {
    QFETCH(QSize,size);
    TileServer server; server.imageSize = size; QTemporaryDir cache;
    TileSource source({server.urlTemplate(),cache.path()});
    QSignalSpy status(&source, &TileSource::statusChanged);
    source.setVisibleTiles({{0,0,0}});
    QTRY_VERIFY(!status.isEmpty() && status.last().first().toString().contains("Unsupported tile image size"));
    QVERIFY(!source.tile({0,0,0}));
  }

  void keyedTilesWireFormatAndPrivateCache_data()
  {
    QTest::addColumn<bool>("header"); QTest::addColumn<bool>("path");
    QTest::newRow("URL-query") << false << false;
    QTest::newRow("URL-path") << false << true;
    QTest::newRow("HTTP-header") << true << false;
  }

  void keyedTilesWireFormatAndPrivateCache()
  {
    QFETCH(bool, header); QFETCH(bool, path);
    TileServer server; QTemporaryDir cache;
    const QString key = "fixture-key&plus+/=%# {z}";
    TileSourceOptions options; options.cacheDirectory = cache.path();
    options.urlTemplate = path ? server.urlTemplate().replace(".png", "/{api_key}.png") : server.urlTemplate() + (header ? "" : "?key={api_key}");
    options.authentication.key = key;
    if (header) options.authentication.header = "X-API-Key";
    server.extraHeaders = "X-Echo-Key: " + key.toUtf8() + "\r\nETag: " + key.toUtf8() + "\r\n";
    {
      TileSource source(options); source.setVisibleTiles({{3,2,1}});
      QTRY_VERIFY(source.tile({3,2,1})); QCOMPARE(server.requests.size(), 1);
      const QByteArray request = server.requests.first();
      if (header) QCOMPARE(requestHeader(request, "X-API-Key"), key.toUtf8());
      else {
        const QUrl url = QUrl::fromEncoded("http://127.0.0.1" + request.split(' ')[1]);
        if (path) QVERIFY(url.path(QUrl::FullyDecoded).endsWith(key + ".png"));
        else QCOMPARE(QUrlQuery(url).queryItemValue("key", QUrl::FullyDecoded), key);
        QVERIFY(requestHeader(request, "X-API-Key").isEmpty());
      }
    }
    // Reopen the disk cache: same credentials retain the cached tile.
    TileSource cached(options); cached.setVisibleTiles({{3,2,1}});
    QTRY_VERIFY(cached.tile({3,2,1})); QCOMPARE(server.requests.size(), 1);
    int files = 0;
    QDirIterator iterator(cache.path(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
      QFile file(iterator.next()); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray bytes = file.readAll(); ++files;
      QVERIFY(!bytes.contains(key.toUtf8())); QVERIFY(!bytes.contains(QUrl::toPercentEncoding(key)));
    }
    QVERIFY(files > 0);
    TileAuthentication changed = options.authentication; changed.key = "fixture-new-account";
    QVERIFY(cached.configure(options.urlTemplate, changed));
    QVERIFY(!cached.tile({3,2,1})); QTRY_VERIFY(cached.tile({3,2,1})); QCOMPARE(server.requests.size(), 2);
    QVERIFY(server.requests.last().contains(changed.key.toUtf8()));
  }

  void keyedTilesEnvironmentErrorsAndRedirects()
  {
    TileServer server, other; QTemporaryDir cache;
    TileSourceOptions options; options.urlTemplate = server.urlTemplate(); options.cacheDirectory = cache.path();
    options.authentication.environment = "XASTIR_TEST_TILE_KEY";
    options.authentication.header = "Authorization"; options.authentication.prefix = "Bearer ";
    qunsetenv("XASTIR_TEST_TILE_KEY");
    QString error;
    QVERIFY(!TileSource::validate(options.urlTemplate, options.authentication, &error));
    {
      TileSource missing(options); QSignalSpy status(&missing, &TileSource::statusChanged);
      missing.setVisibleTiles({{0,0,0}}); QVERIFY(server.requests.isEmpty());
      QVERIFY(status.last().first().toString().contains("missing"));
    }
    qputenv("XASTIR_TEST_TILE_KEY", "fixture-bearer-key");
    server.response = TileServer::Unauthorized;
    {
      TileSource denied(options); QSignalSpy status(&denied, &TileSource::statusChanged);
      denied.setVisibleTiles({{0,0,0}});
      QTRY_VERIFY(!status.isEmpty() && status.last().first().toString().contains("401"));
      QCOMPARE(requestHeader(server.requests.first(), "Authorization"), QByteArray("Bearer fixture-bearer-key"));
      for (const auto &entry : status) QVERIFY(!entry.first().toString().contains("fixture-bearer-key"));
    }
    {
      TileSourceOptions urlOptions = options; urlOptions.urlTemplate += "?key={api_key}";
      urlOptions.authentication.header.clear(); urlOptions.authentication.prefix.clear();
      TileSource denied(urlOptions); QSignalSpy status(&denied, &TileSource::statusChanged);
      denied.setVisibleTiles({{0,0,0}});
      QTRY_VERIFY(!status.isEmpty() && status.last().first().toString().contains("401"));
      for (const auto &entry : status) QVERIFY(!entry.first().toString().contains("fixture-bearer-key"));
    }
    server.response = TileServer::Redirect; server.redirectUrl = other.urlTemplate().replace("{z}","0").replace("{x}","0").replace("{y}","0").toUtf8();
    {
      TileSource redirect(options); QSignalSpy status(&redirect, &TileSource::statusChanged);
      redirect.setVisibleTiles({{0,0,0}});
      QTRY_VERIFY(!status.isEmpty() && status.last().first().toString().contains("unavailable"));
      QVERIFY(other.requests.isEmpty()); QVERIFY(!redirect.tile({0,0,0}));
    }
    server.response = TileServer::Redirect; server.redirectOnce = true;
    server.redirectUrl = server.urlTemplate().replace("/{z}/{x}/{y}.png", "/redirected.png").toUtf8();
    {
      TileSource redirect(options); redirect.setVisibleTiles({{0,0,0}});
      QTRY_VERIFY(redirect.tile({0,0,0}));
      QCOMPARE(requestHeader(server.requests.last(), "Authorization"), QByteArray("Bearer fixture-bearer-key"));
    }
    qunsetenv("XASTIR_TEST_TILE_KEY");
    TileAuthentication invalid; invalid.key = "fixture-key"; invalid.header = "Host";
    QVERIFY(!TileSource::validate(server.urlTemplate(), invalid, &error));
    invalid.header = "X-API-Key"; invalid.key += "\r\nX-Injection: bad";
    QVERIFY(!TileSource::validate(server.urlTemplate(), invalid, &error));
    invalid.key = "fixture-key";
    QVERIFY(!TileSource::validate("http://tile.example/{z}/{x}/{y}.png", invalid, &error));
    QVERIFY(TileSource::validate("https://tile.example/{z}/{x}/{y}.png", invalid, &error));
  }

  void keyedTilesChooserSessionAndRestoration()
  {
    TileServer server; QTemporaryDir cache;
    ApplicationOptions options; options.tiles = {server.urlTemplate(),cache.path()};
    MainWindow window(options);
    window.findChild<QAction *>("PULDNMP001")->trigger();
    auto *dialog = window.findChild<QDialog *>("mapChooserDialog"); QVERIFY(dialog);
    dialog->findChild<QComboBox *>("tileAuthentication")->setCurrentIndex(1);
    dialog->findChild<QLineEdit *>("mapTileUrl")->setText(server.urlTemplate()+"?key={api_key}");
    auto *key = dialog->findChild<QLineEdit *>("tileApiKey"); QCOMPARE(key->echoMode(), QLineEdit::Password);
    auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    dialog->findChild<QLineEdit *>("tileKeyEnvironment")->setText("XASTIR_TEST_CHOOSER_KEY");
    qunsetenv("XASTIR_TEST_CHOOSER_KEY"); QTest::mouseClick(ok,Qt::LeftButton);
    QVERIFY(dialog->isVisible()); QVERIFY(dialog->findChild<QLabel *>("mapChooserError")->text().contains("missing"));
    key->setText("fixture-session-key"); QTest::mouseClick(ok,Qt::LeftButton);
    window.show(); auto *tiles = window.findChild<TileSource *>();
    QTRY_VERIFY(!server.requests.isEmpty()); QVERIFY(server.requests.first().contains("key=fixture-session-key"));
    QCOMPARE(tiles->authentication().key, QString("fixture-session-key"));
    QSettings settings;
    for (const auto &setting : settings.allKeys()) QVERIFY(!settings.value(setting).toString().contains("fixture-session-key"));
    qputenv("XASTIR_TEST_CHOOSER_KEY", "fixture-restored-key");
    ApplicationOptions defaultOptions; defaultOptions.tiles.cacheDirectory = cache.path();
    MainWindow restored(defaultOptions);
    QCOMPARE(restored.findChild<TileSource *>()->urlTemplate(), server.urlTemplate()+"?key={api_key}");
    QVERIFY(restored.findChild<TileSource *>()->authentication().key.isEmpty());
    restored.show(); QTRY_VERIFY(server.requests.last().contains("key=fixture-restored-key"));
    qunsetenv("XASTIR_TEST_CHOOSER_KEY");
  }

  void downloadsAndPersistentCache()
  {
    TileServer server;
    QVERIFY(server.server.isListening());
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    const TileSourceOptions options{server.urlTemplate(), cache.path()};
    const TileId id{3, 3, 3};
    {
      TileSource source(options);
      source.setVisibleTiles({id, id});
      QTRY_VERIFY(source.tile(id));
      QCOMPARE(server.requests.size(), 1);
      QCOMPARE(source.tile(id)->toImage().pixelColor(128, 128), TileColor);
      QVERIFY(server.requests.first().startsWith("GET /3/3/3.png HTTP/1.1\r\n"));
      QVERIFY(server.requests.first().toLower().contains("user-agent: xastirqt/"));
      QVERIFY(!server.requests.first().toLower().contains("cache-control: no-cache"));
      source.setVisibleTiles({id});
      QCOMPARE(server.requests.size(), 1);
    }
    // A new source has no decoded images: it must retrieve the persisted HTTP entry.
    TileSource source(options);
    source.setVisibleTiles({id});
    QTRY_VERIFY(source.tile(id));
    QCOMPARE(server.requests.size(), 1);
  }

  void limitsRequestsAndCancelsObsoleteTiles()
  {
    TileServer server;
    QVERIFY(server.server.isListening());
    server.hold = true;
    QTemporaryDir cache;
    TileSource source({server.urlTemplate(), cache.path()});
    QList<TileId> ids;
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 6; ++x)
        ids.append({3, x, y});
    source.setVisibleTiles(ids);
    QTRY_COMPARE(server.requests.size(), 6);
    const TileId replacement{3, 7, 7};
    source.setVisibleTiles({replacement});
    QTRY_COMPARE(server.requests.size(), 7);
    server.release();
    QTRY_VERIFY(source.tile(replacement));
    for (const TileId &id : ids)
      QVERIFY(!source.tile(id));
    QCOMPARE(server.requests.size(), 7);
  }

  void failuresAreReported_data()
  {
    QTest::addColumn<int>("response");
    QTest::newRow("HTTP error") << int(TileServer::NotFound);
    QTest::newRow("invalid image") << int(TileServer::InvalidImage);
  }

  void failuresAreReported()
  {
    QFETCH(int, response);
    TileServer server;
    QVERIFY(server.server.isListening());
    server.response = TileServer::Response(response);
    QTemporaryDir cache;
    TileSource source({server.urlTemplate(), cache.path()});
    QSignalSpy messages(&source, &TileSource::statusChanged);
    const TileId id{3, 3, 3};
    source.setVisibleTiles({id});
    QTRY_VERIFY(!messages.isEmpty() && messages.last().first().toString().contains("unavailable"));
    QVERIFY(!source.tile(id));
    source.setVisibleTiles({id});
    QTest::qWait(100);
    QCOMPARE(server.requests.size(), 1);
  }

  void windowRendersNavigatesAndShowsExitPlaceholder()
  {
    TileServer server;
    QVERIFY(server.server.isListening());
    QTemporaryDir cache;
    ApplicationOptions options;
    options.center = {0, 0};
    options.zoom = 0;
    options.tiles = {server.urlTemplate(), cache.path()};
    MainWindow window(options);
    window.resize(640, 480);
    auto *mapWidget = window.findChild<MapWidget *>();
    auto *source = window.findChild<TileSource *>();
    QVERIFY(mapWidget);
    QVERIFY(source);
    window.show();
    QTRY_VERIFY(source->tile({0, 0, 0}));
    QTRY_COMPARE(window.statusBar()->currentMessage(), QStringLiteral("Map ready"));
    QCOMPARE(server.requests.size(), 1); // Repeated worlds use the same tile.
    const QImage rendered = mapWidget->grab().toImage();
    QCOMPARE(rendered.pixelColor(rendered.width() / 2, rendered.height() / 2), TileColor);

    QTest::keyClick(mapWidget, Qt::Key_Plus);
    QCOMPARE(mapWidget->zoomLevel(), 1);
    QVERIFY(std::abs(mapWidget->centerCoordinate().latitude) < 1e-8);
    QVERIFY(std::abs(mapWidget->centerCoordinate().longitude) < 1e-8);

    const QPointF anchor(100, 120);
    const QPointF offset = anchor - QPointF(mapWidget->width() / 2.0, mapWidget->height() / 2.0);
    const auto before = map::fromWorld(map::toWorld(mapWidget->centerCoordinate(), 1) + offset, 1);
    QWheelEvent wheel(anchor, mapWidget->mapToGlobal(anchor.toPoint()), {}, {0, 120},
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(mapWidget, &wheel);
    QCOMPARE(mapWidget->zoomLevel(), 2);
    const auto after = map::fromWorld(map::toWorld(mapWidget->centerCoordinate(), 2) + offset, 2);
    QVERIFY(std::abs(before.latitude - after.latitude) < 1e-8);
    QVERIFY(std::abs(before.longitude - after.longitude) < 1e-8);

    const double longitude = mapWidget->centerCoordinate().longitude;
    QTest::keyClick(mapWidget, Qt::Key_Right);
    QVERIFY(mapWidget->centerCoordinate().longitude > longitude);
    const auto dragStart = mapWidget->centerCoordinate();
    QTest::mousePress(mapWidget, Qt::LeftButton, Qt::NoModifier, QPoint(200, 200));
    QTest::mouseMove(mapWidget, QPoint(250, 220));
    QTest::mouseRelease(mapWidget, Qt::LeftButton, Qt::NoModifier, QPoint(250, 220));
    QVERIFY(mapWidget->centerCoordinate().longitude < dragStart.longitude);
    auto *exit = window.findChild<QAction *>(QStringLiteral("actionExit"));
    QVERIFY(exit);
    exit->trigger();
    auto *dialog = window.findChild<QMessageBox *>("notImplementedDialog");
    QVERIFY(dialog); QCOMPARE(dialog->text(), QStringLiteral("Not implemented yet"));
    QVERIFY(window.isVisible()); dialog->accept(); window.close();
  }

  void destroysVisibleWindowWithPendingDownloads()
  {
    TileServer server;
    QVERIFY(server.server.isListening());
    server.hold = true;
    QTemporaryDir cache;
    ApplicationOptions options;
    options.tiles = {server.urlTemplate(), cache.path()};
    auto *window = new MainWindow(options);
    window->show();
    QTRY_VERIFY(!server.requests.isEmpty());
    // Application shutdown can destroy a visible window without first hiding it.
    // Hide notifications must not access a QMainWindow whose teardown has begun.
    delete window;
    server.release();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
};

QTEST_MAIN(MapTest)
#include "test_qt_map.moc"
