// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_views.h"
#include "station_model.h"
#include "station_navigator.h"
#include "station_view.h"
#include <QAbstractTableModel>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>
#include <memory>

namespace {
enum Column { Name, Kind, Owner, Heard, Packets, Position, Via, Path, Course, Speed, Altitude,
              Temperature, Wind, WindDirection, Gust, Humidity, Pressure, Rain24, Text, ColumnCount };
enum Role { SortRole = Qt::UserRole, MembershipRole };
constexpr int OwnWeather = XASTIR_LIST_COUNT;
struct Row { QList<QVariant> cells; int membership = 0; };
QVariant number(const char *text)
{
  bool ok = false;
  const auto value = QByteArray(text).toDouble(&ok);
  return ok ? QVariant(value) : QVariant();
}
Row snapshot(const DataRow *record)
{
  Row row;
  row.cells.resize(ColumnCount);
  auto &v = row.cells;
  v[Name] = QString::fromUtf8(record->call_sign);
  v[Kind] = record->flag & ST_OBJECT ? QObject::tr("Object")
      : record->flag & ST_ITEM ? QObject::tr("Item") : QObject::tr("Station");
  v[Owner] = QString::fromUtf8(record->origin);
  v[Heard] = qint64(record->sec_heard);
  v[Packets] = record->num_packets;
  if (record->position_valid)
    v[Position] = QString("%1, %2").arg(record->latitude, 0, 'f', 5).arg(record->longitude, 0, 'f', 5);
  v[Via] = record->data_via == 'T' ? QObject::tr("AGWPE")
      : record->data_via == 'I' ? QObject::tr("APRS-IS") : QObject::tr("Replay/import");
  v[Path] = QString::fromLatin1(record->node_path_ptr ? record->node_path_ptr : "");
  v[Course] = number(record->course); v[Speed] = number(record->speed); v[Altitude] = number(record->altitude);
  if (const auto *wx = record->weather_data) {
    v[Temperature] = number(wx->wx_temp); v[Wind] = number(wx->wx_speed);
    v[WindDirection] = number(wx->wx_course); v[Gust] = number(wx->wx_gust);
    v[Humidity] = number(wx->wx_hum); v[Pressure] = number(wx->wx_baro);
    // Legacy weather storage uses hundredths of an inch for precipitation.
    if (const auto rain = number(wx->wx_prec_24); rain.isValid()) v[Rain24] = rain.toDouble() / 100.0;
  }
  const CommentRow *latest = record->comment_data;
  if (record->status_data && (!latest || record->status_data->sec_heard >= latest->sec_heard)) latest = record->status_data;
  if (latest && latest->text_ptr) v[Text] = QString::fromLatin1(latest->text_ptr);
  for (int type = 0; type < XASTIR_LIST_COUNT; ++type)
    if (xastir_station_list_contains(record, type)) row.membership |= 1 << type;
  return row;
}
}

// Values are copied in bounded batches. No database pointer survives an event
// loop turn, so packet updates, kills and expiry cannot invalidate table rows.
class StationTableModel : public QAbstractTableModel
{
public:
  StationTableModel(StationModel *stations, QObject *parent) : QAbstractTableModel(parent), stations_(stations), batch_(this)
  {
    batch_.setSingleShot(true);
    connect(&batch_, &QTimer::timeout, this, [this] { scan(); });
    connect(stations, &QObject::destroyed, this, [this] {
      batch_.stop(); busy_ = false;
      beginResetModel(); rows_.clear(); pending_.clear(); endResetModel();
    });
  }
  int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(rows_.size()); }
  int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override
  {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    const QStringList labels{tr("Name"), tr("Type"), tr("Owner"), tr("Last heard"), tr("Packets"), tr("Latitude, longitude"),
      tr("Latest source"), tr("Path"), tr("Course (°)"), tr("Speed (knots)"), tr("Altitude (m)"), tr("Temperature (°F)"),
      tr("Wind (mph)"), tr("Wind direction (°)"), tr("Gust (mph)"), tr("Humidity (%)"), tr("Pressure (hPa)"), tr("Rain 24h (in)"), tr("Latest comment/status")};
    return section >= 0 && section < labels.size() ? labels[section] : QVariant();
  }
  QVariant data(const QModelIndex &index, int role) const override
  {
    if (!index.isValid() || index.row() >= rows_.size() || index.column() >= ColumnCount) return {};
    const auto &row = rows_[index.row()];
    if (role == MembershipRole) return row.membership;
    const auto &value = row.cells[index.column()];
    if (role == SortRole) return value;
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) return {};
    if (!value.isValid() || value.toString().isEmpty()) return QStringLiteral("—");
    if (index.column() == Heard) return QDateTime::fromSecsSinceEpoch(value.toLongLong()).toString("yyyy-MM-dd HH:mm:ss");
    return value;
  }
  bool busy() const { return busy_; }
  void reload()
  {
    if (busy_) return;
    busy_ = true; pending_.clear(); after_.clear(); batch_.start(0);
  }
private:
  void scan()
  {
    if (!stations_) return;
    auto *db = const_cast<XastirStationDb *>(stations_->database());
    DataRow *row = db->first;
    if (!after_.isEmpty() && xastir_search_station_name(db, &row, after_.data(), 1)) row = row->n_next;
    int remaining = 128;
    while (row && remaining-- > 0) {
      pending_.append(snapshot(row)); after_ = row->call_sign; row = row->n_next;
    }
    if (row) { batch_.start(1); return; }
    beginResetModel(); rows_ = std::move(pending_); busy_ = false; endResetModel();
  }
  QPointer<StationModel> stations_;
  QTimer batch_;
  bool busy_ = false;
  QByteArray after_;
  QList<Row> rows_, pending_;
};

namespace {
class ListFilter : public QSortFilterProxyModel
{
public:
  ListFilter(int type, QString own, QObject *parent) : QSortFilterProxyModel(parent), type_(type), own_(std::move(own)) {}
protected:
  bool filterAcceptsRow(int row, const QModelIndex &parent) const override
  {
    const auto index = sourceModel()->index(row, Name, parent);
    const int type = type_ == OwnWeather ? XASTIR_LIST_WEATHER : type_;
    if (!(index.data(MembershipRole).toInt() & (1 << type))) return false;
    if (type_ == OwnWeather && index.data(SortRole).toString() != own_) return false;
    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
  }
private:
  int type_;
  QString own_;
};
}

StationViews::StationViews(QMainWindow *window, StationModel *stations, StationNavigator *navigator, QString own)
  : QObject(window), window_(window), stations_(stations), navigator_(navigator),
    model_(new StationTableModel(stations, this)), own_(std::move(own)), refreshTimer_(this), startedAt_(QDateTime::currentDateTime())
{
  elapsed_.start();
  const struct { const char *id; int type; } actions[] = {
    {"PULDNVI004", XASTIR_LIST_ALL}, {"PULDNVI003", XASTIR_LIST_MOBILE}, {"PULDNVI005", XASTIR_LIST_WEATHER},
    {"PULDNVI009", XASTIR_LIST_LOCAL}, {"PULDNVI012", XASTIR_LIST_LAST}, {"LHPUPNI005", XASTIR_LIST_OBJECTS},
    {"LHPUPNI006", XASTIR_LIST_OWN_OBJECTS}, {"PULDNVI008", OwnWeather}, {"PULDNVI013", -1}, {"PULDNVI016", -2}};
  for (const auto &entry : actions) {
    if (auto *action = window_->findChild<QAction *>(entry.id)) {
      disconnect(action, nullptr, window_, nullptr); action->setProperty("implemented", true);
      connect(action, &QAction::triggered, this, [this, type = entry.type, action] {
        if (type == -1) uptime();
        else if (type == -2) aloha();
        else openList(type, action->property("legacyLabel").toString());
      });
    }
  }
  connect(stations_, &StationModel::stationsChanged, this, [this] { dirty_ = true; });
  refreshTimer_.setInterval(500);
  connect(&refreshTimer_, &QTimer::timeout, this, &StationViews::refresh); refreshTimer_.start();
}

void StationViews::refresh()
{
  if (!stations_ || !dirty_ || model_->busy()) return;
  for (const auto &dialog : lists_) {
    if (dialog && dialog->isVisible()) { dirty_ = false; model_->reload(); return; }
  }
}

void StationViews::openList(int type, const QString &title)
{
  if (lists_[type]) { lists_[type]->show(); lists_[type]->raise(); dirty_ = true; refresh(); return; }
  auto *dialog = new QDialog(window_); lists_[type] = dialog;
  dialog->setObjectName(QString("stationList%1").arg(type)); dialog->setWindowTitle(title);
  dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->resize(1050, 500);
  auto *layout = new QVBoxLayout(dialog);
  auto *filter = new QLineEdit(dialog); filter->setObjectName("stationListFilter");
  filter->setPlaceholderText(tr("Filter names, owners, paths, or text…")); layout->addWidget(filter);
  auto *proxy = new ListFilter(type, own_, dialog); proxy->setSourceModel(model_);
  proxy->setSortRole(SortRole); proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
  proxy->setFilterKeyColumn(-1); proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
  connect(filter, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
  auto *table = new QTableView(dialog); table->setObjectName("stationListTable"); table->setModel(proxy);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection); table->setSortingEnabled(true);
  table->sortByColumn(type == XASTIR_LIST_LAST ? Heard : Name, type == XASTIR_LIST_LAST ? Qt::DescendingOrder : Qt::AscendingOrder);
  table->verticalHeader()->hide(); table->horizontalHeader()->setStretchLastSection(true);
  table->setColumnWidth(Name, 120); table->setColumnWidth(Heard, 170); table->setColumnWidth(Position, 180);
  table->setColumnWidth(Path, 160); table->setColumnWidth(Text, 240);
  for (int column = Course; column <= Rain24; ++column) {
    const bool mobile = type == XASTIR_LIST_MOBILE && column <= Altitude;
    const bool weather = (type == XASTIR_LIST_WEATHER || type == OwnWeather) && column >= Temperature;
    table->setColumnHidden(column, !mobile && !weather);
  }
  layout->addWidget(table);
  auto *count = new QLabel(dialog); count->setObjectName("stationListCount"); layout->addWidget(count);
  if (type == XASTIR_LIST_LOCAL || type == OwnWeather) {
    auto *note = new QLabel(type == OwnWeather
      ? tr("Weather received for configured callsign %1.").arg(own_.isEmpty() ? tr("(unset)") : own_)
      : tr("Includes records heard through a TNC, even if their latest update arrived from APRS-IS."), dialog);
    note->setTextFormat(Qt::PlainText); note->setWordWrap(true); layout->addWidget(note);
  }
  auto *buttons = new QHBoxLayout; layout->addLayout(buttons);
  auto *details = new QPushButton(tr("Details"), dialog); details->setObjectName("stationListDetails"); buttons->addWidget(details);
  auto *center = new QPushButton(tr("Center on map"), dialog); center->setObjectName("stationListCenter"); buttons->addWidget(center);
  auto *follow = new QPushButton(tr("Follow"), dialog); follow->setObjectName("stationListFollow"); buttons->addWidget(follow);
  buttons->addStretch();
  auto *close = new QPushButton(tr("Close"), dialog); buttons->addWidget(close);
  connect(close, &QPushButton::clicked, dialog, &QDialog::close);
  auto selected = [table] { return table->currentIndex().siblingAtColumn(Name).data(SortRole).toString(); };
  auto update = [this, selected, details, center, follow, count, proxy] {
    const auto *row = stations_ ? stations_->record(selected()) : nullptr;
    details->setEnabled(row); center->setEnabled(row && row->position_valid && (row->flag & ST_ACTIVE));
    follow->setEnabled(center->isEnabled()); count->setText(tr("%1 records").arg(proxy->rowCount()));
  };
  auto saved = std::make_shared<QString>();
  connect(table->selectionModel(), &QItemSelectionModel::currentChanged, dialog,
    [saved, selected, update](const QModelIndex &current) {
      if (current.isValid()) *saved = selected();
      update();
    });
  connect(proxy, &QAbstractItemModel::modelReset, dialog, update);
  connect(filter, &QLineEdit::textChanged, dialog, update);
  connect(model_, &QAbstractItemModel::modelReset, dialog, [proxy, table, saved, update] {
    for (int row = 0; row < proxy->rowCount(); ++row) {
      if (proxy->index(row, Name).data(SortRole).toString() == *saved) { table->setCurrentIndex(proxy->index(row, Name)); break; }
    }
    update();
  });
  connect(details, &QPushButton::clicked, this, [this, selected] { emit detailsRequested(selected()); });
  connect(table, &QTableView::doubleClicked, this, [this, selected] { emit detailsRequested(selected()); });
  connect(center, &QPushButton::clicked, this, [this, selected] { navigator_->locate(selected(), false); });
  connect(follow, &QPushButton::clicked, this, [this, selected] { navigator_->locate(selected(), true); });
  update(); dialog->show(); dirty_ = true; refresh();
}

void StationViews::aloha()
{
  if (aloha_) { aloha_->show(); aloha_->raise(); return; }
  auto *dialog = new QDialog(window_); aloha_ = dialog;
  dialog->setObjectName("alohaStatistics"); dialog->setWindowTitle(tr("ALOHA Statistics"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  auto *layout = new QVBoxLayout(dialog);
  auto *text = new QLabel(dialog); text->setObjectName("alohaStatisticsText"); text->setTextFormat(Qt::PlainText);
  text->setWordWrap(true); text->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(text);
  auto calculate = [this, text] {
    const auto *local = stations_ ? stations_->record(own_) : nullptr;
    if (!local || !local->position_valid) { text->setText(tr("Configure your station callsign and position to calculate ALOHA statistics.")); return; }
    aloha_stats stats{};
    const double radius = xastir_aloha_radius(stations_->database(), local->latitude, local->longitude, &stats);
    text->setText(tr("Center: %1\nRadius: %2\n\nStations considered: %3\nDigipeaters: %4\nMobiles in motion: %5\nOther mobiles: %6\nWeather stations: %7\nHome stations: %8\n\nCalculated: %9\nUses retained TNC reception records. Select Refresh to recalculate.")
      .arg(own_, radius < 0 ? tr("indeterminate") : tr("%1 km").arg(radius / 1000, 0, 'f', 2))
      .arg(stats.total).arg(stats.digis).arg(stats.mobiles_in_motion).arg(stats.other_mobiles).arg(stats.wxs).arg(stats.homes)
      .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
  };
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  auto *refresh = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole); refresh->setObjectName("alohaRefresh");
  connect(refresh, &QPushButton::clicked, dialog, calculate);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close); layout->addWidget(buttons);
  calculate(); dialog->resize(430, 380); dialog->show();
}

void StationViews::uptime()
{
  if (uptime_) { uptime_->show(); uptime_->raise(); return; }
  auto *dialog = new QDialog(window_); uptime_ = dialog;
  dialog->setObjectName("applicationUptime"); dialog->setWindowTitle(tr("Uptime")); dialog->setAttribute(Qt::WA_DeleteOnClose);
  auto *layout = new QVBoxLayout(dialog);
  auto *text = new QLabel(dialog); text->setObjectName("applicationUptimeText"); text->setTextFormat(Qt::PlainText); layout->addWidget(text);
  auto update = [this, text] {
    const qint64 seconds = elapsed_.elapsed() / 1000;
    text->setText(tr("Started: %1\nRunning: %2 days, %3:%4:%5\nStation records: %6")
      .arg(startedAt_.toString(Qt::ISODate)).arg(seconds / 86400)
      .arg(seconds / 3600 % 24, 2, 10, QChar('0')).arg(seconds / 60 % 60, 2, 10, QChar('0'))
      .arg(seconds % 60, 2, 10, QChar('0')).arg(stations_ ? stations_->database()->count : 0));
  };
  auto *timer = new QTimer(dialog); timer->setInterval(1000);
  connect(timer, &QTimer::timeout, dialog, update); timer->start();
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close); layout->addWidget(buttons);
  update(); dialog->show();
}
