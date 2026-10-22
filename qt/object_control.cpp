// SPDX-License-Identifier: GPL-2.0-or-later
#include "object_control.h"
#include "radio_interface.h"
#include "aprs_decoder.h"
#include "beacon_control.h"
#include "map_widget.h"
#include "message_core.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QChar first(const QString &s) { return s.isEmpty() ? QChar() : s.at(0); }
bool reject(QString *error, const QString &reason) { if (error) *error = reason; return false; }
}
ObjectControl::ObjectControl(QMainWindow *window, StationModel *stations, MapWidget *map, BeaconControl *beacons,
                             XastirConfig config, Interfaces interfaces)
  : QObject(window), window_(window), stations_(stations), map_(map), beacons_(beacons), config_(std::move(config)),
    interfaces_(std::move(interfaces)), clock_([] { return QDateTime::currentMSecsSinceEpoch(); })
{
  QSettings settings;
  disabled_ = settings.value("transmit/objectsDisabled", config_.objectTransmitDisabled).toBool();
  if (auto *action = window->findChild<QAction *>("PULDNTNT06")) {
    disconnect(action, nullptr, window, nullptr); action->setProperty("implemented", true); action->setChecked(disabled_);
    connect(action, &QAction::triggered, this, [this](bool value) {
      disabled_ = value; QSettings().setValue("transmit/objectsDisabled", value); emit changed();
    });
  }
  if (auto *menu = window->findChild<QMenu *>("MENUTB0005")) {
    auto *action = menu->addAction(tr("Manage Own Objects/Items…")); action->setObjectName("manageOwnObjects");
    action->setProperty("implemented", true); connect(action, &QAction::triggered, this, &ObjectControl::open);
  }
  const auto data = settings.value("objects/definitions").toByteArray();
  if (data.size() <= 128 * 1024) for (const auto &value : QJsonDocument::fromJson(data).array()) {
    if (entries_.size() >= 200) break;
    const auto o = value.toObject(); OwnedEntity d;
    d.name = o["name"].toString(); d.owner = o["owner"].toString(); d.item = o["item"].toBool(); d.alive = o["alive"].toBool(true);
    d.position = {o["latitude"].toDouble(1000), o["longitude"].toDouble(1000)};
    d.comment = o["comment"].toString(); d.path = o["path"].toString();
    d.table = first(o["table"].toString()); d.symbol = first(o["symbol"].toString());
    d.device = o["device"].toInt(-1); d.maximumSeconds = o["maximumSeconds"].toInt(1800);
    d.updatedAt = o["updatedAt"].toVariant().toLongLong();
    if (d.owner == config_.callsign && d.updatedAt > 0 && !information(d).isEmpty()) {
      entries_.insert(d.name, {d, {}, 0, tr("Restored; transmission paused")});
      const auto *current = stations_->record(d.name);
      if (!current || qint64(current->sec_heard) <= d.updatedAt) project(d);
    }
  }
  timer_.setParent(this); timer_.setInterval(1000); timer_.setObjectName("objectTransmitTimer");
  connect(&timer_, &QTimer::timeout, this, &ObjectControl::process); timer_.start();
  connect(this, &ObjectControl::changed, this, &ObjectControl::refresh);
  connect(this, &ObjectControl::statusChanged, window, [window](const QString &text) { window->statusBar()->showMessage(text, 15000); });
}
void ObjectControl::configure(const XastirConfig &config) { config_ = config; emit changed(); }
QList<OwnedEntity> ObjectControl::definitions() const
{
  QList<OwnedEntity> result; for (const auto &state : entries_) result.append(state.value); return result;
}
QByteArray ObjectControl::information(const OwnedEntity &d, QString *error) const
{
  const auto name = d.name.toLatin1(), comment = d.comment.toLatin1(), path = d.path.toLatin1();
  char packet[256]; const auto now = QDateTime::fromMSecsSinceEpoch(clock_()).toUTC();
  if (name.contains('\0') || comment.contains('\0') || path.contains('\0')
      || d.name != QString::fromLatin1(name) || d.name != d.name.trimmed() || d.comment != QString::fromLatin1(comment)
      || d.path != QString::fromLatin1(path) || !xastir_message_valid_path(path.constData())
      || d.maximumSeconds < 20 || d.maximumSeconds > 86400 || d.device < -1 || d.device > 255
      || d.table.unicode() > 127 || d.symbol.unicode() > 127
      || !xastir_build_object_item(packet, sizeof(packet), name.constData(), !d.item, d.alive,
          d.position.latitude, d.position.longitude, d.table.toLatin1(), d.symbol.toLatin1(), comment.constData(),
          now.date().day(), now.time().hour(), now.time().minute())) {
    reject(error, tr("Use a 1–9 character object name or 3–9 character item name (items exclude ! and _), valid coordinates/symbol, and at most 43 printable ASCII comment characters. Check the route and interval."));
    return {};
  }
  return packet;
}
bool ObjectControl::collision(const OwnedEntity &d, QString *error) const
{
  if (!stations_) return reject(error, tr("Station storage is unavailable."));
  const auto *row = stations_->record(d.name); if (!row) return true;
  const bool kind = row->flag & (d.item ? ST_ITEM : ST_OBJECT);
  if (!kind || QString::fromLatin1(row->origin) != d.owner)
    return reject(error, tr("This name is in use by another station or owner. Choose another name."));
  return true;
}
bool ObjectControl::persist(QString *error)
{
  QJsonArray data;
  for (const auto &state : entries_) {
    const auto &d = state.value;
    data.append(QJsonObject{{"name", d.name}, {"owner", d.owner}, {"item", d.item}, {"alive", d.alive},
      {"latitude", d.position.latitude}, {"longitude", d.position.longitude}, {"table", QString(d.table)}, {"symbol", QString(d.symbol)},
      {"comment", d.comment}, {"path", d.path}, {"device", d.device}, {"maximumSeconds", d.maximumSeconds}, {"updatedAt", d.updatedAt}});
  }
  QSettings settings; settings.setValue("objects/definitions", QJsonDocument(data).toJson(QJsonDocument::Compact)); settings.sync();
  return settings.status() == QSettings::NoError || reject(error, tr("Could not save object/item definitions."));
}
void ObjectControl::project(const OwnedEntity &d)
{
  if (!collision(d, nullptr)) return;
  if (!d.alive) { stations_->remove(d.name, d.item ? StationKind::Item : StationKind::Object); return; }
  const auto update = decodeAprs(d.owner.toLatin1() + ">APRS:" + information(d));
  if (!update) return;
  auto station = update->station; station.heardAt = d.updatedAt; station.dataVia = 'F'; stations_->upsert(station);
}
bool ObjectControl::saveDefinition(OwnedEntity d, QString *error)
{
  if (config_.callsign.isEmpty()) return reject(error, tr("Configure your station callsign first."));
  d.owner = config_.callsign;
  if (information(d, error).isEmpty() || !collision(d, error)) return false;
  if (!entries_.contains(d.name) && entries_.size() >= 200) return reject(error, tr("At most 200 owned definitions may be saved."));
  const auto previous = entries_; d.updatedAt = clock_() / 1000;
  entries_.insert(d.name, {d, {}, 0, tr("Saved locally; transmission paused")});
  if (!persist(error)) { entries_ = previous; return false; }
  project(d); emit changed(); return true;
}
bool ObjectControl::transmit(State &state, const OwnedEntity &d, QString *error)
{
  if (beacons_->transmitDisabled()) return reject(error, tr("All transmission is disabled."));
  if (disabled_) return reject(error, tr("Object/item transmission is disabled in the Interface menu."));
  if (d.owner != config_.callsign) return reject(error, tr("The object owner differs from the configured callsign."));
  if (!collision(d, error)) { state.schedule = {}; return false; }
  const auto packet = information(d, error); if (packet.isEmpty()) return false;
  for (auto *client : interfaces_()) if (client->deviceIndex() == d.device) {
    if (client->callsign() != d.owner) return reject(error, tr("Station callsign does not match the selected interface."));
    if (!client->transmit(packet, d.path.isEmpty() ? QString() : d.path, error)) return false;
    state.status = tr("Queued to %1 device %2; RF delivery unconfirmed").arg(client->transportName()).arg(d.device);
    if (state.schedule.remaining) xastir_object_schedule_accepted(&state.schedule, clock_(), d.maximumSeconds, QRandomGenerator::global()->bounded(1001u));
    emit statusChanged(d.name + ": " + state.status); return true;
  }
  return reject(error, tr("Select an available radio interface."));
}
bool ObjectControl::sendNow(const QString &name, QString *error)
{
  auto it = entries_.find(name); if (it == entries_.end()) return reject(error, tr("Select a saved definition."));
  const bool accepted = transmit(*it, it->value, error); emit changed(); return accepted;
}
bool ObjectControl::kill(const QString &name, QString *error)
{
  auto it = entries_.find(name); if (it == entries_.end()) return reject(error, tr("Select a saved definition."));
  const bool repeat = it->schedule.remaining; it->schedule = {}; // Stop live reports even if the kill cannot be queued.
  auto killed = it->value; killed.alive = false;
  if (repeat) xastir_object_schedule_start(&it->schedule, clock_(), killed.maximumSeconds, 1);
  if (!transmit(*it, killed, error)) { it->schedule = {}; emit changed(); return false; }
  it->value = killed; it->value.updatedAt = clock_() / 1000;
  const bool saved = persist(error);
  if (!saved && error) *error = tr("Kill report queued, but saving the killed state failed: %1").arg(*error);
  project(it->value); emit changed(); return saved;
}
bool ObjectControl::setRepeat(const QString &name, bool enabled, QString *error)
{
  auto it = entries_.find(name); if (it == entries_.end()) return reject(error, tr("Select a saved definition."));
  it->schedule = {}; it->retryAfter = 0;
  if (enabled) xastir_object_schedule_start(&it->schedule, clock_(), it->value.maximumSeconds, !it->value.alive);
  it->status = enabled ? tr("Repeating; first report waits 20 seconds (subject to TX controls)") : tr("Transmission paused");
  emit changed(); return true;
}
bool ObjectControl::repeating(const QString &name) const { return entries_.value(name).schedule.remaining; }
bool ObjectControl::forget(const QString &name, QString *error)
{
  auto it = entries_.find(name); if (it == entries_.end()) return false;
  if (it->value.alive) return reject(error, tr("Queue a kill report before forgetting a live definition."));
  const auto previous = *it; entries_.erase(it);
  if (!persist(error)) { entries_.insert(name, previous); return false; }
  emit changed(); return true;
}
void ObjectControl::process()
{
  const auto now = clock_(); int sent = 0;
  for (auto &state : entries_) {
    if (sent >= 4) break;
    if (now < state.retryAfter || !xastir_object_schedule_due(&state.schedule, now)) continue;
    ++sent; QString error;
    if (!transmit(state, state.value, &error)) { state.retryAfter = now + 20000; state.status = tr("Waiting: %1").arg(error); emit statusChanged(state.value.name + ": " + state.status); }
    emit changed();
  }
}
void ObjectControl::refresh()
{
  if (!dialog_) return;
  const auto selected = table_->currentRow() >= 0 ? table_->item(table_->currentRow(), 0)->text() : QString();
  table_->setRowCount(entries_.size()); int row = 0;
  for (const auto &state : entries_) {
    const auto &d = state.value;
    const QStringList values{d.name, d.item ? tr("Item") : tr("Object"), d.alive ? tr("Live") : tr("Killed"),
      QString::number(d.device), state.schedule.remaining ? tr("Repeating") : tr("Paused"), state.status};
    for (int col = 0; col < values.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(values[col]));
    if (d.name == selected) table_->selectRow(row);
    ++row;
  }
}
void ObjectControl::open()
{
  if (!dialog_) {
    auto *d = new QDialog(window_); dialog_ = d; d->setObjectName("ownObjectManager"); d->setWindowTitle(tr("Own Objects & Items")); d->resize(950, 450);
    auto *layout = new QVBoxLayout(d);
    auto *note = new QLabel(tr("Save creates a local draft. Send now queues a radio report. Repeat starts at 20 seconds and decays to the configured maximum. Killed reports repeat at most 20 times. Restart always pauses transmission. My Position and Objects/Items have separate transmit controls."), d);
    note->setWordWrap(true); layout->addWidget(note);
    table_ = new QTableWidget(d); table_->setObjectName("ownObjectTable"); table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({tr("Name"), tr("Kind"), tr("State"), tr("Device"), tr("Schedule"), tr("Status")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch); layout->addWidget(table_);
    auto *status = new QLabel(d); status->setObjectName("ownObjectStatus"); status->setWordWrap(true); status->setTextFormat(Qt::PlainText); layout->addWidget(status);
    auto *buttons = new QHBoxLayout; layout->addLayout(buttons);
    auto button = [&](const QString &text, const char *id, auto handler) { auto *b = new QPushButton(text, d); b->setObjectName(id); buttons->addWidget(b); connect(b, &QPushButton::clicked, this, handler); };
    auto selected = [this] { return table_->currentRow() >= 0 ? table_->item(table_->currentRow(), 0)->text() : QString(); };
    button(tr("New…"), "newOwnObject", [this] { edit(); });
    button(tr("Edit…"), "editOwnObject", [this, selected] { if (!selected().isEmpty()) edit(selected()); });
    button(tr("Send now"), "sendOwnObject", [this, selected, status] { QString error; status->setText(sendNow(selected(), &error) ? tr("Report queued; RF delivery unconfirmed.") : error); });
    button(tr("Repeat / Pause"), "repeatOwnObject", [this, selected, status] { QString error; if (!setRepeat(selected(), !repeating(selected()), &error)) status->setText(error); else status->setText(tr("Schedule updated; TX controls apply.")); });
    button(tr("Kill"), "killOwnObject", [this, selected, status] { QString error; status->setText(kill(selected(), &error) ? tr("Kill report queued; RF delivery unconfirmed.") : error); });
    button(tr("Forget killed"), "forgetOwnObject", [this, selected, status] { QString error; if (!forget(selected(), &error)) status->setText(error); });
    button(tr("Close"), "closeOwnObjects", [d] { d->close(); });
  }
  refresh(); dialog_->show(); dialog_->raise();
}
void ObjectControl::edit(const QString &name)
{
  if (editor_) { editor_->show(); editor_->raise(); return; }
  OwnedEntity value;
  if (name.isEmpty()) { value.position = map_ ? map_->centerCoordinate() : map::Coordinate{0, 0}; value.maximumSeconds = config_.objectIntervalSeconds; }
  else value = entries_.value(name).value;
  auto *d = new QDialog(window_); editor_ = d; d->setObjectName("ownObjectEditor"); d->setWindowTitle(name.isEmpty() ? tr("Create Object/Item") : tr("Edit Object/Item")); d->setAttribute(Qt::WA_DeleteOnClose);
  auto *form = new QFormLayout(d);
  auto *entityName = new QLineEdit(value.name, d); entityName->setObjectName("objectName"); entityName->setMaxLength(9); entityName->setReadOnly(!name.isEmpty()); form->addRow(tr("Name (case sensitive)"), entityName);
  auto *kind = new QComboBox(d); kind->setObjectName("objectKind"); kind->addItems({tr("Object"), tr("Item")}); kind->setCurrentIndex(value.item); kind->setEnabled(name.isEmpty()); form->addRow(tr("Kind"), kind);
  auto coordinate = [&](const QString &label, const char *id, double number, int maximum) {
    auto *field = new QDoubleSpinBox(d); field->setObjectName(id); field->setDecimals(7); field->setRange(-maximum, maximum); field->setValue(number); form->addRow(label, field); return field;
  };
  auto *latitude = coordinate(tr("Latitude (decimal degrees)"), "objectLatitude", value.position.latitude, 90);
  auto *longitude = coordinate(tr("Longitude (decimal degrees)"), "objectLongitude", value.position.longitude, 180);
  auto *table = new QLineEdit(QString(value.table), d); table->setObjectName("objectSymbolTable"); table->setMaxLength(1); form->addRow(tr("Symbol table /, \\, or overlay"), table);
  auto *symbol = new QLineEdit(QString(value.symbol), d); symbol->setObjectName("objectSymbol"); symbol->setMaxLength(1); form->addRow(tr("Symbol character"), symbol);
  auto *comment = new QLineEdit(value.comment, d); comment->setObjectName("objectComment"); comment->setMaxLength(43); form->addRow(tr("Comment (ASCII)"), comment);
  auto *device = new QComboBox(d); device->setObjectName("objectDevice"); device->addItem(tr("Select radio interface"), -1);
  for (auto *client : interfaces_()) device->addItem(client->description(), client->deviceIndex());
  const int index = device->findData(value.device); if (index >= 0) device->setCurrentIndex(index); form->addRow(tr("Transmit via"), device);
  auto *path = new QLineEdit(value.path, d); path->setObjectName("objectPath"); path->setPlaceholderText(tr("Blank uses the selected interface's path")); form->addRow(tr("RF path"), path);
  auto *interval = new QSpinBox(d); interval->setObjectName("objectInterval"); interval->setRange(20, 86400); interval->setValue(value.maximumSeconds); interval->setSuffix(tr(" seconds")); form->addRow(tr("Maximum repeat interval"), interval);
  auto draftValue = [=] {
    auto draft = value; draft.name = entityName->text().trimmed(); draft.item = kind->currentIndex() == 1; draft.alive = true;
    draft.position = {latitude->value(), longitude->value()}; draft.table = first(table->text()); draft.symbol = first(symbol->text());
    draft.comment = comment->text(); draft.path = path->text().trimmed().toUpper(); draft.device = device->currentData().toInt(); draft.maximumSeconds = interval->value();
    return draft;
  };
  auto *preview = new QLabel(d); preview->setObjectName("objectPacketPreview"); preview->setTextFormat(Qt::PlainText);
  preview->setWordWrap(true); preview->setTextInteractionFlags(Qt::TextSelectableByMouse); form->addRow(tr("Live report preview"), preview);
  auto updatePreview = [this, draftValue, preview] { QString error; const auto packet = information(draftValue(), &error); preview->setText(packet.isEmpty() ? error : QString::fromLatin1(packet)); };
  for (auto *field : {entityName, table, symbol, comment, path}) connect(field, &QLineEdit::textChanged, d, updatePreview);
  for (auto *field : {latitude, longitude}) connect(field, &QDoubleSpinBox::valueChanged, d, updatePreview);
  connect(kind, &QComboBox::currentIndexChanged, d, updatePreview); updatePreview();
  auto *note = new QLabel(tr("Save creates a live local draft and pauses repeats. It sends no packet. Editing a killed definition prepares it for a later explicit Send now."), d);
  note->setWordWrap(true); form->addRow(note);
  auto *status = new QLabel(d); status->setObjectName("objectEditorStatus"); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); form->addRow(status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, d); form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, [this, draftValue, status, d] {
    QString error; if (!saveDefinition(draftValue(), &error)) { status->setText(error); return; } d->accept();
  });
  d->resize(600, 480); d->show();
}
