// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_navigator.h"
#include "map_widget.h"
#include <QAction>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

StationNavigator::StationNavigator(QMainWindow *window, StationModel *stations, MapWidget *map)
  : QObject(window), window_(window), stations_(stations), map_(map)
{
  for (const auto &id : {"PULDNDP014", "PULDNDP001"}) {
    if (auto *action = window->findChild<QAction *>(id)) {
      QObject::disconnect(action, nullptr, window, nullptr);
      action->setProperty("implemented", true);
      connect(action, &QAction::triggered, this, [this, id] { open(QString::fromLatin1(id) == "PULDNDP001"); });
    }
  }
  connect(stations_, &StationModel::stationsChanged, this, [this] { updateFollowing(); refresh(); });
  connect(map_->preferences(), &DisplayPreferences::changed, this, &StationNavigator::refresh);
  connect(map_, &MapWidget::manuallyPanned, this, &StationNavigator::stopFollowing);
  // Keep a followed position centered even when wheel zoom uses the mouse location.
  connect(map_, &MapWidget::viewChanged, this, &StationNavigator::updateFollowing);
}
bool StationNavigator::locate(const QString &name, bool follow)
{
  const auto it = stations_->stations().constFind(name);
  if (it == stations_->stations().cend()) return false;
  stopFollowing();
  if (follow) {
    followed_ = name; followedKind_ = it->kind;
    emit followingChanged(followed_);
  }
  map_->setView(it->position, map_->zoomLevel());
  refresh(); return true;
}
void StationNavigator::stopFollowing()
{
  if (followed_.isEmpty()) return;
  followed_.clear(); emit followingChanged({});
  if (status_) status_->setText(tr("Not following a station."));
}
void StationNavigator::updateFollowing()
{
  if (followed_.isEmpty()) return;
  const auto it = stations_->stations().constFind(followed_);
  if (it == stations_->stations().cend() || it->kind != followedKind_) { stopFollowing(); return; }
  const auto center = map_->centerCoordinate();
  // Projection clamps polar positions and wraps longitude. Compare projected points.
  if ((map::toWorld(center, map_->zoomLevel()) - map::toWorld(it->position, map_->zoomLevel())).manhattanLength() > 0.001)
    map_->setView(it->position, map_->zoomLevel());
}
void StationNavigator::refresh()
{
  if (!list_) return;
  const auto selected = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
  list_->clear();
  for (const auto &station : stations_->stations()) {
    const auto kind = station.kind == StationKind::Station ? tr("Station") : station.kind == StationKind::Object ? tr("Object") : tr("Item");
    const QString label = tr("%1 — %2%3 — %4").arg(station.callsign, kind,
      station.sourceStation.isEmpty() ? QString() : tr(" (owner %1)").arg(station.sourceStation))
      .arg(map_->preferences()->position(station.position));
    if (!label.contains(filter_->text(), Qt::CaseInsensitive)) continue;
    auto *item = new QListWidgetItem(label, list_); item->setData(Qt::UserRole, station.callsign);
    if (station.callsign == selected) list_->setCurrentItem(item);
  }
  if (!list_->currentItem() && list_->count()) list_->setCurrentRow(0);
  center_->setEnabled(list_->currentItem()); follow_->setEnabled(list_->currentItem());
  status_->setText(followed_.isEmpty() ? tr("Not following a station.") : tr("Following %1").arg(followed_));
}
void StationNavigator::open(bool track)
{
  if (!dialog_) {
    auto *dialog = new QDialog(window_); dialog_ = dialog;
    dialog->setObjectName("stationSearchDialog"); dialog->resize(700, 450);
    auto *layout = new QVBoxLayout(dialog);
    filter_ = new QLineEdit(dialog); filter_->setObjectName("stationSearchFilter");
    filter_->setPlaceholderText(tr("Search callsign, object/item name, or owner…")); layout->addWidget(filter_);
    list_ = new QListWidget(dialog); list_->setObjectName("stationSearchResults"); layout->addWidget(list_);
    status_ = new QLabel(dialog); layout->addWidget(status_);
    layout->addWidget(new QLabel(tr("Following keeps the target centered. Drag the map or use arrow keys to stop."), dialog));
    auto *buttons = new QHBoxLayout; layout->addLayout(buttons);
    center_ = new QPushButton(tr("Center on map"), dialog); center_->setObjectName("centerStation"); buttons->addWidget(center_);
    follow_ = new QPushButton(tr("Follow"), dialog); follow_->setObjectName("followStation"); buttons->addWidget(follow_);
    auto *stop = new QPushButton(tr("Stop following"), dialog); buttons->addWidget(stop);
    auto *close = new QPushButton(tr("Close"), dialog); buttons->addWidget(close);
    connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    connect(stop, &QPushButton::clicked, this, &StationNavigator::stopFollowing);
    auto locateSelected = [this](bool follow) {
      if (list_->currentItem()) locate(list_->currentItem()->data(Qt::UserRole).toString(), follow);
    };
    connect(center_, &QPushButton::clicked, this, [locateSelected] { locateSelected(false); });
    connect(follow_, &QPushButton::clicked, this, [locateSelected] { locateSelected(true); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [locateSelected] { locateSelected(false); });
    connect(filter_, &QLineEdit::textChanged, this, &StationNavigator::refresh);
    connect(list_, &QListWidget::currentRowChanged, this, [this] {
      center_->setEnabled(list_->currentItem()); follow_->setEnabled(list_->currentItem());
    });
  }
  dialog_->setWindowTitle(track ? tr("Track Station") : tr("Find Station"));
  (track ? follow_ : center_)->setDefault(true);
  refresh(); dialog_->show(); dialog_->raise(); dialog_->activateWindow(); filter_->setFocus();
}
