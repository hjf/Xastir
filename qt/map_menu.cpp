// SPDX-License-Identifier: GPL-2.0-or-later
#include "map_menu.h"
#include "map_widget.h"
#include "tile_source.h"
#include "station_navigator.h"
#include "aprs_position.h"
#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

namespace {
QDialog *dialog(QMainWindow *window, const QString &title, const char *name)
{
  auto *result = new QDialog(window);
  result->setAttribute(Qt::WA_DeleteOnClose);
  result->setWindowTitle(title); result->setObjectName(name);
  return result;
}
void show(QDialog *dialog) { dialog->show(); dialog->raise(); }
}
MapMenu::MapMenu(QMainWindow *window, MapWidget *map, TileSource *tiles, StationNavigator *navigator)
  : QObject(window), window_(window), map_(map), tiles_(tiles), navigator_(navigator)
{
  // Explicit command-line tile sources take precedence over saved selection.
  if (tiles_->urlTemplate() == TileSourceOptions{}.urlTemplate) {
    const QString saved = QSettings().value("map/tileUrl").toString();
    if (!saved.isEmpty()) {
      tiles_->setUrlTemplate(saved);
      map_->setMapAttribution(QSettings().value("map/attribution").toString());
    }
  }
  auto bind = [this](const char *id, std::function<void()> callback) {
    auto *action = window_->findChild<QAction *>(id);
    if (!action) return;
    disconnect(action, nullptr, window_, nullptr);
    action->setProperty("implemented", true);
    connect(action, &QAction::triggered, this, std::move(callback));
  };
  bind("PULDNMP001", [this] { chooseMap(); });
  bind("PULDNMP012", [this] { bookmarks(); });
  bind("POPUPMA026", [this] { coordinates(false); });
  bind("COORD001", [this] { coordinates(true); });
  // Coordinate search is explicit; address geocoding needs a separate data source.
  auto *search = window_->findChild<QAction *>("PULDNMP029");
  search->setText(tr("Search Location (coordinates)…"));
  bind("PULDNMP029", [this] { coordinates(false); });
  auto toggle = [this, bind](const char *id, const char *key, bool initial, std::function<void(bool)> apply) {
    auto *action = window_->findChild<QAction *>(id);
    QSettings settings;
    const bool enabled = settings.value(QString("map/%1").arg(key), initial).toBool();
    action->setChecked(enabled); apply(enabled);
    bind(id, [action, key = QString(key), apply] {
      apply(action->isChecked()); QSettings().setValue("map/" + key, action->isChecked());
    });
  };
  toggle("PULDNMP013", "disabled", false, [map](bool value) { map->setMapsEnabled(!value); });
  toggle("PULDNMP003", "grid", false, [map](bool value) { map->setMapGrid(value); });
  toggle("PULDNMP031", "border", false, [map](bool value) { map->setMapBorder(value); });
  window_->findChild<QAction *>("PULDNMP031")->setToolTip(tr("Show raster tile boundaries."));
  auto choices = [this, bind](const QStringList &ids, const char *key, int initial, std::function<void(int)> apply) {
    auto *group = new QActionGroup(this);
    const int selected = qBound(0, QSettings().value(QString("map/%1").arg(key), initial).toInt(), int(ids.size()) - 1);
    for (int i = 0; i < ids.size(); ++i) {
      auto *action = window_->findChild<QAction *>(ids[i]);
      action->setCheckable(true); group->addAction(action); action->setChecked(i == selected);
      bind(ids[i].toLatin1().constData(), [i, apply, key = QString(key)] { apply(i); QSettings().setValue("map/" + key, i); });
    }
    apply(selected);
  };
  const QStringList colors = {"white", "black", "gray", "mistyrose", "navy", "steelblue", "mediumseagreen", "palegreen", "palegoldenrod", "goldenrod", "rosybrown", "firebrick"};
  choices({"PULDNMBC11","PULDNMBC12","PULDNMBC01","PULDNMBC02","PULDNMBC03","PULDNMBC04","PULDNMBC05","PULDNMBC06","PULDNMBC07","PULDNMBC08","PULDNMBC09","PULDNMBC10"}, "background", 0,
    [map, colors](int i) { map->setMapBackground(QColor(colors[i])); });
  QStringList intensity;
  for (int i = 0; i <= 100; i += 10) intensity << QString::number(i) + "%";
  choices(intensity, "intensity", 10, [map](int i) { map->setMapIntensity(i / 10.0); });
  choices({"PULDNMSL01","PULDNMSL02","PULDNMSL03","PULDNMSL04","PULDNMSL05"}, "textStyle", 4, [map](int i) { map->setTextStyle(i); });
  choices({"PULDNMIO01","PULDNMIO02","PULDNMIO03","PULDNMIO04"}, "iconOutline", 0, [map](int i) { map->setIconOutline(i); });
  window_->findChild<QAction *>("PULDNMP027")->setToolTip(tr("Discard cached tiles in the current view and fetch them again."));
  bind("PULDNMP027", [tiles] { tiles->reload(); });
  bind("PULDNMP028", [tiles] { tiles->reload(true); });
  // These controls depend on legacy map backends absent from the XYZ renderer.
  for (const char *id : {"PULDNMP014", "POPUPMA046", "POPUPMA047", "POPUPMA048", "POPUPMA050", "POPUPMA049", "POPUPMA051",
      "PULDNMP016", "PULDNMP002", "PULDNMP021", "PULDNMP004", "PULDNMP010", "PULDNMP009", "PULDNMP007",
      "PULDNMP030", "PULDNMP032", "PULDNMP022", "PULDNMP023", "PULDNMP024"}) {
    auto *action = window_->findChild<QAction *>(id);
    action->setEnabled(false); action->setProperty("unavailable", true);
    action->setText(action->text() + tr(" (unavailable)"));
    action->setToolTip(tr("Requires a legacy map backend that has not been migrated to Qt."));
  }
  auto *pointer = window_->findChild<QAction *>("PULDNMP011");
  pointer->setVisible(false); pointer->setProperty("unavailable", true);
  // Qt pans and zooms directly; the Motif fast-copy drawing switch has no role.
  window_->findChild<QAction *>("PULDNMP016")->setVisible(false);
  map->setMapGamma(QSettings().value("map/gamma", 1.0).toDouble());
  bind("GAMMA001", [this] {
    bool ok = false;
    const double value = QInputDialog::getDouble(window_, tr("Gamma correction"), tr("Gamma (1 = unchanged)"),
      QSettings().value("map/gamma", 1.0).toDouble(), 0.1, 5.0, 2, &ok);
    if (ok) { map_->setMapGamma(value); QSettings().setValue("map/gamma", value); }
  });
}

void MapMenu::coordinates(bool calculator)
{
  auto *d = dialog(window_, calculator ? tr("Coordinate Calculator") : tr("Center & Zoom"), "mapCoordinatesDialog");
  auto *form = new QFormLayout(d);
  auto spin = [d, form](const QString &label, const char *name, double max, double value) {
    auto *field = new QDoubleSpinBox(d); field->setObjectName(name); field->setDecimals(6);
    field->setRange(-max, max); field->setValue(value); form->addRow(label, field); return field;
  };
  const auto center = map_->centerCoordinate();
  auto *lat = spin(tr("Latitude (decimal degrees)"), "mapLatitude", 90, center.latitude);
  auto *lon = spin(tr("Longitude (decimal degrees)"), "mapLongitude", 180, center.longitude);
  auto *zoom = new QSpinBox(d); zoom->setObjectName("mapZoom"); zoom->setRange(map::MinZoom, map::MaxZoom);
  zoom->setValue(map_->zoomLevel()); form->addRow(tr("Zoom"), zoom);
  auto *result = new QLabel(d); result->setObjectName("coordinateResult"); result->setTextInteractionFlags(Qt::TextSelectableByMouse);
  if (calculator) {
    form->addRow(result);
    auto calculate = [lat, lon, result] {
      QStringList lines;
      for (int format : {CONVERT_DEC_DEG, CONVERT_HP_NORMAL, CONVERT_DMS_NORMAL}) {
        char a[64], b[64];
        xastir_convert_lat_l2s(std::lround((90 - lat->value()) * 360000), a, sizeof(a), format);
        xastir_convert_lon_l2s(std::lround((180 + lon->value()) * 360000), b, sizeof(b), format);
        lines << QString::fromLatin1(a) + "   " + QString::fromLatin1(b);
      }
      result->setText(lines.join('\n'));
    };
    connect(lat, &QDoubleSpinBox::valueChanged, d, calculate);
    connect(lon, &QDoubleSpinBox::valueChanged, d, calculate); calculate();
    form->addRow(new QLabel(tr("Conversions use the original Xastir C routines. UTM/MGRS input is not available yet."), d));
  }
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, d);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Center on map")); form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, d, [this, d, lat, lon, zoom] {
    navigator_->stopFollowing(); map_->setView({lat->value(), lon->value()}, zoom->value()); d->accept();
  });
  show(d);
}

void MapMenu::bookmarks()
{
  auto *d = dialog(window_, tr("Map Display Bookmarks"), "mapBookmarksDialog");
  auto *layout = new QVBoxLayout(d); layout->addWidget(new QLabel(tr("Save up to 200 named views (center and zoom)."), d)); auto *list = new QListWidget(d); list->setObjectName("mapBookmarks"); layout->addWidget(list);
  const auto values = QSettings().value("map/bookmarks").toList();
  for (const auto &value : values) {
    auto *item = new QListWidgetItem(value.toMap().value("name").toString(), list); item->setData(Qt::UserRole, value);
  }
  auto save = [list] {
    QVariantList values;
    for (int i = 0; i < list->count(); ++i) values << list->item(i)->data(Qt::UserRole);
    QSettings().setValue("map/bookmarks", values);
  };
  auto *add = new QPushButton(tr("Save current view…"), d); add->setObjectName("saveMapBookmark"); layout->addWidget(add);
  connect(add, &QPushButton::clicked, d, [this, d, list, save] {
    bool ok; QString name = QInputDialog::getText(d, tr("Save view"), tr("Name"), QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (list->count() >= 200) return;
    const auto center = map_->centerCoordinate();
    QVariantMap value{{"name", name}, {"latitude", center.latitude}, {"longitude", center.longitude}, {"zoom", map_->zoomLevel()}};
    auto *item = new QListWidgetItem(name, list); item->setData(Qt::UserRole, value); list->setCurrentItem(item); save();
  });
  auto *go = new QPushButton(tr("Go to selected view"), d); go->setObjectName("goMapBookmark"); layout->addWidget(go);
  auto restore = [this, list] {
    if (!list->currentItem()) return;
    const auto v = list->currentItem()->data(Qt::UserRole).toMap();
    navigator_->stopFollowing(); map_->setView({v.value("latitude").toDouble(), v.value("longitude").toDouble()}, v.value("zoom").toInt());
  };
  connect(go, &QPushButton::clicked, d, restore); connect(list, &QListWidget::itemDoubleClicked, d, restore);
  auto *remove = new QPushButton(tr("Delete selected view"), d); remove->setObjectName("deleteMapBookmark"); layout->addWidget(remove);
  connect(remove, &QPushButton::clicked, d, [list, save] { delete list->takeItem(list->currentRow()); save(); });
  if (list->count()) list->setCurrentRow(0);
  show(d);
}

void MapMenu::chooseMap()
{
  auto *d = dialog(window_, tr("Map Chooser"), "mapChooserDialog");
  auto *form = new QFormLayout(d);
  auto *url = new QLineEdit(tiles_->urlTemplate(), d); url->setObjectName("mapTileUrl"); form->addRow(tr("XYZ tile URL"), url);
  auto *credit = new QLineEdit(map_->mapAttribution(), d); credit->setObjectName("mapAttribution"); form->addRow(tr("Attribution"), credit);
  auto *reset = new QPushButton(tr("Use OpenStreetMap"), d); form->addRow(reset);
  connect(reset, &QPushButton::clicked, d, [url, credit] {
    url->setText("https://tile.openstreetmap.org/{z}/{x}/{y}.png"); credit->setText("© OpenStreetMap contributors");
  });
  form->addRow(new QLabel(tr("256 × 256 PNG tiles. URL must include {z}, {x}, {y}. Local file URLs are supported."), d));
  auto *error = new QLabel(d); error->setObjectName("mapChooserError"); form->addRow(error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, d); form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, d, [this, d, url, credit, error] {
    const QString text = url->text().trimmed(); const QUrl parsed(text);
    if (!parsed.isValid() || !(parsed.scheme() == "https" || parsed.scheme() == "http" || parsed.isLocalFile()) ||
        !text.contains("{z}") || !text.contains("{x}") || !text.contains("{y}") || credit->text().trimmed().isEmpty()) {
      error->setText(tr("Enter a valid XYZ URL and attribution.")); return;
    }
    tiles_->setUrlTemplate(text); map_->setMapAttribution(credit->text().trimmed());
    QSettings().setValue("map/tileUrl", text); QSettings().setValue("map/attribution", credit->text().trimmed());
    d->accept();
  });
  show(d);
}
