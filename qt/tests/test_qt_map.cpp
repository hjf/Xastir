// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "map_projection.h"
#include "tile_source.h"
#include "aprs_symbols.h"
#include "station_model.h"
#include "station_db.h"
#include "agwpe_client.h"
#include <QtEndian>

#include <QAction>
#include <QMenuBar>
#include <QMessageBox>
#include <QBuffer>
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
const QColor TileColor(35, 145, 100);

// An actual HTTP server exercises asynchronous downloads without contacting OSM.
class TileServer : public QObject
{
public:
  enum Response { Image, NotFound, InvalidImage };
  QTcpServer server;
  QList<QByteArray> requests;
  QList<QPointer<QTcpSocket>> held;
  Response response = Image;
  bool hold = false;

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
      QImage image(map::TileSize, map::TileSize, QImage::Format_RGB32);
      image.fill(TileColor);
      QBuffer buffer(&body);
      buffer.open(QIODevice::WriteOnly);
      image.save(&buffer, "PNG");
    }
    else
      body = "Not a map image";
    QByteArray header = response == NotFound ? "HTTP/1.1 404 Not Found\r\n" : "HTTP/1.1 200 OK\r\n";
    header += "Content-Type: image/png\r\nCache-Control: public, max-age=604800\r\n";
    header += "Date: " + QDateTime::currentDateTimeUtc().toString("ddd, dd MMM yyyy hh:mm:ss 'GMT'").toLatin1() + "\r\n";
    header += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
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

private slots:
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
      if (!action->property("legacyMenuId").isValid() || action->property("implemented").toBool()) continue;
      ++leaves;
      if (action->isCheckable()) ++toggles;
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
    QVERIFY(leaves > 150); QVERIFY(toggles > 50);
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
    // Deterministic local HTTP tests must not use the machine's proxy settings.
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
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
    const QPoint midpoint(canvas->width() / 2, canvas->height() / 2);
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
    QCOMPARE(image.copy(QRect(54, 230, 20, 20)), image.copy(QRect(310, 230, 20, 20)));
    QCOMPARE(image.copy(QRect(566, 230, 20, 20)), image.copy(QRect(310, 230, 20, 20)));
    model.clear();
    QVERIFY(canvas.grab().toImage().copy(QRect(310, 230, 20, 20))
            != image.copy(QRect(310, 230, 20, 20)));
    canvas.close();
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
