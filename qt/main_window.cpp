// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "map_menu.h"
#include "file_menu.h"
#include "configuration_ui.h"
#include "snapshot_control.h"
#include "kml_snapshot.h"
#include "station_model.h"
#include "agwpe_client.h"
#include "kiss_client.h"
#include <QSet>
#include "legacy_menus.h"
#include "message_service.h"
#include "message_ui.h"
#include "packet_monitor.h"
#include "traffic_log.h"
#include "station_navigator.h"
#include "station_menu.h"
#include "station_details.h"
#include "interface_control.h"
#include "aprsis_client.h"
#include "beacon_control.h"
#include "object_control.h"
#include "history_ui.h"
#include <QDebug>
#include "aprs_decoder.h"
#include <QTimer>

#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QSettings>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QWindowStateChangeEvent>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

MainWindow::MainWindow(const ApplicationOptions &options, QWidget *parent)
  : QMainWindow(parent)
{
  setWindowTitle(options.config.callsign.isEmpty() ? tr("Xastir Qt")
    : tr("Xastir Qt — %1").arg(options.config.callsign));
  resize(1100, 760);
  auto *tiles = new TileSource(options.tiles, this);
  auto *mapWidget = new MapWidget(tiles, options.center, options.zoom, this);
  mapWidget_ = mapWidget;
  if (options.config.selection) mapWidget_->setSelections(*options.config.selection);
  if (options.config.display) mapWidget_->setDisplayOptions(*options.config.display);
  stations_ = new StationModel(mapWidget);
  stations_->setOwnCallsign(options.config.callsign);
  mapWidget->setStationModel(stations_);
  localStation_ = options.config.localStation;
  if (localStation_) localStation_->local = true;
  if (localStation_) stations_->upsert(*localStation_);
  tiles->setParent(mapWidget);
  setCentralWidget(mapWidget);

  createLegacyMenus(this);
  new FileMenu(this, mapWidget_);
  auto *snapshots = new SnapshotControl(this, mapWidget_, options.config);
  new KmlSnapshotControl(this, stations_, snapshots, options.config);
  auto *fullscreen = new QAction(tr("Toggle fullscreen"), this);
  fullscreen->setObjectName("toggleFullscreen");
  fullscreen->setShortcut(QKeySequence(Qt::Key_F11));
  fullscreen->setAutoRepeat(false);
  addAction(fullscreen);
  connect(fullscreen, &QAction::triggered, this, [this] {
    if (isFullScreen()) setWindowState(previousWindowState_);
    else showFullScreen();
  });
  auto *exitFullscreen = new QAction(this);
  exitFullscreen->setShortcut(QKeySequence(Qt::Key_Escape));
  exitFullscreen->setAutoRepeat(false);
  addAction(exitFullscreen);
  connect(exitFullscreen, &QAction::triggered, this, [this] {
    if (isFullScreen()) setWindowState(previousWindowState_);
  });
  setMouseTracking(true);
  mapWidget_->setMouseTracking(true);
  menuBar()->setMouseTracking(true);
  statusBar()->setMouseTracking(true);
  qApp->installEventFilter(this);
  auto *navigator = new StationNavigator(this, stations_, mapWidget_);
  new MapMenu(this, mapWidget_, tiles, navigator, !options.tileUrlExplicit);
  stationMenu_ = new StationMenu(this, stations_, mapWidget_, options.config.callsign, localStation_);
  auto *following = new QLabel(this);
  statusBar()->addPermanentWidget(following);
  connect(navigator, &StationNavigator::followingChanged, following, [following](const QString &name) {
    following->setText(name.isEmpty() ? QString() : tr("Following %1").arg(name));
  });
  auto *monitor = new PacketMonitor(this); monitor_=monitor;
  auto *logs = new TrafficLogControl(this, options.config);
  connect(monitor, &PacketMonitor::packetObserved, logs, &TrafficLogControl::observe);
  connect(monitor, &PacketMonitor::replayPacket, stations_, [this](const QByteArray &packet, qint64 timestamp) {
    stations_->applyPacket(packet,'F', timestamp / 1000);
  });

  auto *position = new QLabel(this);
  position->setObjectName("mapPositionStatus");
  auto *stationCount = new QLabel(this);
  statusBar()->addPermanentWidget(stationCount);
  auto updateCount = [this, stationCount] {
    int stationTotal = 0, objectTotal = 0;
    for (const auto &entry : stations_->stations()) {
      if (entry.kind == StationKind::Station) ++stationTotal;
      else ++objectTotal;
    }
    stationCount->setText(tr("Stations: %1  Objects/items: %2").arg(stationTotal).arg(objectTotal));
  };
  connect(stations_, &StationModel::stationsChanged, stationCount, updateCount);
  updateCount();
  statusBar()->addPermanentWidget(position);
  auto *status = statusBar();
  connect(mapWidget, &MapWidget::mapStatusChanged, status, [status](const QString &message) { status->showMessage(message); });
  connect(tiles, &TileSource::statusChanged, status, [this, status](const QString &message) {
    if (mapWidget_->baseMap() == "raster") status->showMessage(message);
  });
  auto updatePosition = [mapWidget, position] {
    position->setText(mapWidget->statusText());
  };
  connect(mapWidget, &MapWidget::viewChanged, this, updatePosition);
  connect(mapWidget, &MapWidget::statusChanged, this, updatePosition);
  updatePosition();
  statusBar()->showMessage(tr("Loading map…"));
  messages_ = new MessageService(options.config.callsign, this);
  if(options.historyEnabled) {
    history_=new HistoryStore(options.history,this);
    connect(history_,&HistoryStore::errorOccurred,this,[this](const QString &error){qWarning().noquote()<<error;statusBar()->showMessage(error,30000);});
    QString error;
    if(!history_->open(&error) || !history_->restore(stations_,messages_,&error)) {
      qWarning().noquote()<<"History:"<<error;
      QTimer::singleShot(0,this,[this,error]{statusBar()->showMessage(tr("History unavailable: %1").arg(error),30000);});
    }
    history_->saveMessages(messages_);
    connect(messages_,&MessageService::changed,history_,[this]{history_->saveMessages(messages_);});
    new HistoryUi(this,history_,options.config.callsign);
  }
  beacons_=new BeaconControl(this,options.config,[this]{return clients_;});
  new ConfigurationUi(this, mapWidget_, monitor_, beacons_, snapshots, logs, options.config);
  transmitDisabled_=beacons_->transmitDisabled();
  connect(beacons_,&BeaconControl::transmitDisabledChanged,this,[this](bool disabled){transmitDisabled_=disabled; for(auto *client:clients_) client->setTransmitBlocked(disabled);});
  connect(beacons_,&BeaconControl::statusChanged,statusBar(),[this](const QString &message){statusBar()->showMessage(message,10000);});
  configureRadios(options.config);
  auto *objects = new ObjectControl(this, stations_, mapWidget_, beacons_, options.config, [this] { return clients_; });
  messageUi_ = new MessageUi(this, messages_, clients_, options.config.autoReplyText);
  messageUi_->setClients(clients_,aprsis_);
  new StationDetails(this, stations_, mapWidget_, navigator, messageUi_, options.config.callsign);
  auto *interfaces=new InterfaceControl(this,options.config);
  connect(interfaces, &InterfaceControl::kissStartRequested, this, [this, interfaces](int device) {
    for (auto *radio : clients_) if (radio->deviceIndex() == device) if (auto *kiss = qobject_cast<KissClient *>(radio)) {
      kiss->start();
      interfaces->reportKissResult(kiss->initializing() ? tr("Serial port open; entering KISS mode. See the interface status.") :
        kiss->connected() ? tr("Serial port open; awaiting KISS frames.") : tr("Could not open the saved port; see the interface status.")); return;
    }
    interfaces->reportKissResult(tr("Save this KISS interface before opening its port."));
  });
  connect(interfaces, &InterfaceControl::kissParametersRequested, this, [this, interfaces](const KissSettings &requested) {
    for (auto *radio : clients_) if (radio->deviceIndex() == requested.deviceIndex) if (auto *kiss = qobject_cast<KissClient *>(radio)) {
      QString error;
      interfaces->reportKissResult(kiss->applyParameters(requested, &error) ? tr("TNC parameters queued to the serial port; KISS does not acknowledge commands.") : error); return;
    }
    interfaces->reportKissResult(tr("Save and open this KISS interface before applying parameters."));
  });
  connect(interfaces, &InterfaceControl::kissStopRequested, this, [this, interfaces](int device) {
    for (auto *radio : clients_) if (radio->deviceIndex() == device) if (auto *kiss = qobject_cast<KissClient *>(radio)) {
      kiss->stop(); interfaces->reportKissResult(tr("Serial port closed; automatic reconnect stopped.")); return;
    }
    interfaces->reportKissResult(tr("No saved KISS interface with that device number."));
  });
  if (auto *configureStation = findChild<QAction *>("PULDNCF004")) {
    disconnect(configureStation, nullptr, this, nullptr);
    configureStation->setProperty("implemented", true);
    connect(configureStation, &QAction::triggered, interfaces, &InterfaceControl::open);
  }
  if (auto *saveConfig = findChild<QAction *>("PULDNCF008")) {
    disconnect(saveConfig, nullptr, this, nullptr);
    saveConfig->setProperty("implemented", true);
    connect(saveConfig, &QAction::triggered, this, [this] {
      QSettings settings;
      settings.sync();
      if (settings.status() != QSettings::NoError) {
        QMessageBox::warning(this, tr("Save configuration"),
                             tr("Could not save configuration."));
        return;
      }
      statusBar()->showMessage(tr("Configuration saved."), 5000);
    });
  }
  if (auto *exit = findChild<QAction *>("actionExit")) {
    disconnect(exit, nullptr, this, nullptr);
    exit->setProperty("implemented", true);
    connect(exit, &QAction::triggered, this, [this] { close(); });
  }
  connect(interfaces,&InterfaceControl::configurationChanged,this,[this, objects](const XastirConfig &config) {
    if(config.callsign!=messages_->callsign()) return; // Identity changes take effect on restart.
    localStation_=config.localStation; if(localStation_) {localStation_->local=true;stations_->upsert(*localStation_);}
    stationMenu_->setLocalStation(localStation_);
    configureRadios(config);
    beacons_->configure(config);
    objects->configure(config);
  });

}

void MainWindow::changeEvent(QEvent *event)
{
  QMainWindow::changeEvent(event);
  if (event->type() != QEvent::WindowStateChange) return;
  const auto oldState = static_cast<QWindowStateChangeEvent *>(event)->oldState();
  if (isFullScreen() && !(oldState & Qt::WindowFullScreen)) {
    fullscreenAltHeld_ = false;
    previousWindowState_ = oldState & ~(Qt::WindowMinimized | Qt::WindowActive);
    menuWasVisible_ = !menuBar()->isHidden();
    statusWasVisible_ = !statusBar()->isHidden();
    menuBar()->hide();
    statusBar()->hide();
  } else if (!isFullScreen() && (oldState & Qt::WindowFullScreen)) {
    fullscreenAltHeld_ = false;
    menuBar()->setVisible(menuWasVisible_);
    statusBar()->setVisible(statusWasVisible_);
  }
}

void MainWindow::revealFullscreenBars(bool reveal)
{
  menuBar()->setVisible(reveal);
  statusBar()->setVisible(reveal);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
  const auto *widget = qobject_cast<QWidget *>(watched);
  if (!isFullScreen() || !widget)
    return QMainWindow::eventFilter(watched, event);
  // Popup menus are separate windows but still owned by this main window.
  bool belongsToWindow = widget->window() == this;
  for (const QObject *owner = widget->parent(); !belongsToWindow && owner; owner = owner->parent())
    belongsToWindow = owner == this;
  if (!belongsToWindow) return QMainWindow::eventFilter(watched, event);
  if (widget->window() != this && !qobject_cast<QMenu *>(widget->window()))
    return QMainWindow::eventFilter(watched, event);
  if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) &&
      static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape &&
      menuBar()->hasFocus() && !QApplication::activePopupWidget()) {
    event->accept();
    if (event->type() == QEvent::KeyPress) {
      menuBar()->setActiveAction(nullptr);
      mapWidget_->setFocus();
      revealFullscreenBars(false);
    }
    return true;
  }
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Alt) {
      fullscreenAltHeld_ = event->type() == QEvent::KeyPress;
      if (fullscreenAltHeld_) {
        revealFullscreenBars(true);
        if (!QApplication::activePopupWidget()) {
          menuBar()->setFocus(Qt::MenuBarFocusReason);
          if (!menuBar()->activeAction() && !menuBar()->actions().isEmpty())
            menuBar()->setActiveAction(menuBar()->actions().first());
        }
      }
      // Keep keyboard menu navigation active after releasing Alt.
      return true;
    }
  } else if (event->type() == QEvent::MouseMove && widget->window() == this) {
    const auto *mouse = static_cast<QMouseEvent *>(event);
    const int y = mapFromGlobal(mouse->globalPosition().toPoint()).y();
    if (y <= 1 && y >= 0) revealFullscreenBars(true);
    else if (!menuBar()->isHidden() && !fullscreenAltHeld_ && !QApplication::activePopupWidget()) {
      const bool overMenu = y < menuBar()->height();
      const bool overStatus = y >= height() - statusBar()->height();
      if (!overMenu && !overStatus) {
        if (menuBar()->hasFocus()) mapWidget_->setFocus();
        revealFullscreenBars(false);
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::loadStations(const QString &path, QString *error, bool centerOnStations)
{
  if (!stations_->loadFile(path, error))
    return false;
  if (localStation_) stations_->upsert(*localStation_);
  if (centerOnStations && !stations_->stations().isEmpty())
    mapWidget_->setView(stations_->stations().first().position, mapWidget_->zoomLevel());
  return true;
}

void MainWindow::configureRadios(const XastirConfig &config) {
  if(aprsis_) {aprsis_->stop();delete aprsis_;aprsis_=nullptr;}
  if(aprsisLabel_){statusBar()->removeWidget(aprsisLabel_);delete aprsisLabel_;aprsisLabel_=nullptr;}
  if(config.aprsis.enabled){
    aprsis_=new AprsisClient(config.aprsis,config.callsign,stations_,this);
    aprsisLabel_=new QLabel(tr("APRS-IS stopped"),this);statusBar()->addWidget(aprsisLabel_);
    connect(aprsis_,&AprsisClient::statusChanged,aprsisLabel_,&QLabel::setText);
    connect(aprsis_,&AprsisClient::packetReceived,monitor_,[this](const QByteArray &packet){monitor_->receive(packet,256);if(history_)history_->recordPacket(packet,256);messages_->receiveInternetPacket(packet,0,aprsis_ && aprsis_->canTransmit());});
    if(history_)connect(aprsis_,&AprsisClient::packetTransmitted,history_,[this](const QByteArray &packet){history_->recordPacket(packet,256,0,true);});
    connect(aprsis_, &AprsisClient::packetTransmitted, monitor_, [this](const QByteArray &packet) { monitor_->recordTransmitted(packet, 256); });
    QTimer::singleShot(0,aprsis_,&AprsisClient::start);
  }
  for(auto *client:clients_) { client->stop(); delete client; } clients_.clear();
  for(auto *label:interfaceLabels_) {statusBar()->removeWidget(label);delete label;} interfaceLabels_.clear();
  auto attach = [this](RadioInterface *client, bool startup) {
    clients_.append(client); client->setTransmitBlocked(transmitDisabled_);
    const int device = client->deviceIndex();
    connect(client, &RadioInterface::packetTransmitted, monitor_, [this, device](const QByteArray &packet) {
      monitor_->recordTransmitted(packet, device); if (history_) history_->recordPacket(packet, device, 0, true);
    });
    connect(client, &RadioInterface::packetReceived, monitor_, [this, device](const QByteArray &packet) {
      monitor_->receive(packet, device); if (history_) history_->recordPacket(packet, device);
      messages_->receivePacket(packet, device);
    });
    auto *label = new QLabel(client->transportName() + tr(" stopped"), this);
    statusBar()->addWidget(label); interfaceLabels_.append(label);
    connect(client, &RadioInterface::statusChanged, label, &QLabel::setText);
    if (startup) QTimer::singleShot(0, client, [client] { client->start(); });
  };
  QSet<int> ids;
  for (const auto &settings : config.agwpe) {
    if (settings.deviceIndex < 0 || settings.deviceIndex > 255 || ids.contains(settings.deviceIndex)) continue;
    ids.insert(settings.deviceIndex);
    attach(new AgwpeClient(settings, stations_, this), settings.connectOnStartup);
  }
  for (const auto &settings : config.kiss) {
    QString error;
    if (ids.contains(settings.deviceIndex) || !KissClient::validate(settings, &error)) {
      statusBar()->showMessage(tr("Serial KISS configuration rejected: %1").arg(error.isEmpty() ? tr("duplicate device number") : error), 30000);
      continue;
    }
    ids.insert(settings.deviceIndex);
    attach(new KissClient(settings, stations_, this), settings.connectOnStartup);
  }
  messages_->setTransport([this](const QByteArray &info,int device,const QString &path,QString *error){if(transmitDisabled_){if(error)*error=tr("All transmission is disabled.");return false;}if(device==256){if(aprsis_)return aprsis_->transmit(info,error);if(error)*error=tr("APRS-IS interface is unavailable.");return false;}for(auto *client:clients_)if(client->deviceIndex()==device)return client->transmit(info,path,error);if(error)*error=tr("Radio interface is no longer available.");return false;});
  if(messageUi_)messageUi_->setClients(clients_,aprsis_);
}
