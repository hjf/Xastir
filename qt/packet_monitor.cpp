// SPDX-License-Identifier: GPL-2.0-or-later
#include "packet_monitor.h"
#include <QAction>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QHeaderView>
#include <QVBoxLayout>

namespace {
bool sameFile(const QString &first, const QString &second)
{
  const QFileInfo a(first), b(second);
  return a.absoluteFilePath() == b.absoluteFilePath()
    || (!a.canonicalFilePath().isEmpty() && a.canonicalFilePath() == b.canonicalFilePath());
}
}

PacketMonitor::PacketMonitor(QMainWindow *window) : QObject(window), window_(window),
  model_(new QStandardItemModel(this))
{
  model_->setHorizontalHeaderLabels({tr("Time"), tr("Source"), tr("Packet (TNC2)")});
  timer_.setInterval(100);
  connect(&timer_, &QTimer::timeout, this, &PacketMonitor::replayNext);
  connect(this, &PacketMonitor::errorOccurred, this, [this](const QString &error) {
    auto *box = new QMessageBox(QMessageBox::Warning, tr("Incoming Data"), error, QMessageBox::Ok, window_);
    box->setAttribute(Qt::WA_DeleteOnClose); box->open();
  });
  if (auto *action = window->findChild<QAction *>("PULDNFI002")) {
    QObject::disconnect(action, nullptr, window, nullptr);
    action->setProperty("implemented", true);
    action->setShortcut(QKeySequence::Open);
    connect(action, &QAction::triggered, this, &PacketMonitor::openLogFile);
  }
  if (auto *action = window->findChild<QAction *>("PULDNVI002")) {
    QObject::disconnect(action, nullptr, window, nullptr);
    action->setProperty("implemented", true);
    connect(action, &QAction::triggered, this, &PacketMonitor::open);
  }
}
void PacketMonitor::append(const QByteArray &packet, int device, qint64 timestamp, bool replay, bool outgoing)
{
  if (model_->rowCount() >= 2000) model_->removeRow(0);
  model_->appendRow({new QStandardItem(QDateTime::fromMSecsSinceEpoch(timestamp).toString("MM-dd HH:mm:ss.zzz")),
    new QStandardItem(replay ? (outgoing ? tr("Replay TX (queued)") : tr("Replay")) : tr("Device %1").arg(device)),
    new QStandardItem(trafficUtf8_ ? QString::fromUtf8(packet) : QString::fromLatin1(packet))});
}
void PacketMonitor::setTrafficUtf8(bool enabled)
{
  trafficUtf8_ = enabled;
}
void PacketMonitor::receive(const QByteArray &packet, int device)
{
  const auto now = QDateTime::currentMSecsSinceEpoch();
  ++received_; append(packet, device, now, false);
  emit packetObserved(packet, device, now, false);
  if (capturing()) {
    QJsonObject record{{"timestamp", now}, {"device", device}, {"packet", QString::fromLatin1(packet.toBase64())}};
    const auto line = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    if (capture_.write(line) != line.size() || !capture_.flush()) {
      const auto error = capture_.errorString(); stopCapture(); emit errorOccurred(tr("Capture failed: %1").arg(error));
    }
  }
  updateStatus();
}
void PacketMonitor::recordTransmitted(const QByteArray &packet, int device)
{
  emit packetObserved(packet, device, QDateTime::currentMSecsSinceEpoch(), true);
}
bool PacketMonitor::startCapture(const QString &path, QString *error)
{
  if (replaying() && sameFile(path, replay_.fileName())) {
    if (error) *error = tr("Cannot overwrite the file being replayed.");
    return false;
  }
  stopCapture(); capture_.setFileName(path);
  if (!capture_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error) *error = capture_.errorString();
    return false;
  }
  updateStatus(); return true;
}
void PacketMonitor::stopCapture() { capture_.close(); updateStatus(); }
bool PacketMonitor::startReplay(const QString &path, QString *error)
{
  if (capturing() && sameFile(path, capture_.fileName())) {
    if (error) *error = tr("Stop capture before replaying its file.");
    return false;
  }
  stopReplay(); replay_.setFileName(path);
  if (!replay_.open(QIODevice::ReadOnly)) {
    if (error) *error = replay_.errorString();
    return false;
  }
  replayed_ = 0; timer_.start(); updateStatus(); return true;
}
void PacketMonitor::stopReplay() { timer_.stop(); replay_.close(); updateStatus(); }
void PacketMonitor::replayNext()
{
  if (replay_.atEnd()) { stopReplay(); return; }
  auto line = replay_.readLine(16385);
  if (line.isEmpty() && replay_.error() != QFileDevice::NoError) {
    const auto error = replay_.errorString(); stopReplay(); emit errorOccurred(error); return;
  }
  if (line.size() > 16384) { stopReplay(); emit errorOccurred(tr("Replay record is too large.")); return; }
  while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
  if (line.isEmpty() || line.startsWith('#')) return;
  QByteArray packet = line;
  qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
  bool outgoing = false;
  if (line.startsWith('{')) {
    const auto object = QJsonDocument::fromJson(line).object();
    const auto decoded = QByteArray::fromBase64Encoding(object.value("packet").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!object.value("packet").isString() || !decoded) {
      stopReplay(); emit errorOccurred(tr("Invalid capture record.")); return;
    }
    packet = decoded.decoded;
    outgoing = object.value("outgoing").toBool(false);
    if (object.contains("outgoing") && !object.value("outgoing").isBool()) {
      stopReplay(); emit errorOccurred(tr("Invalid direction in capture record.")); return;
    }
    bool validTimestamp = false;
    timestamp = object.value("timestamp").toVariant().toLongLong(&validTimestamp);
    if (!validTimestamp || timestamp < 1000) {
      stopReplay(); emit errorOccurred(tr("Invalid timestamp in capture record.")); return;
    }
  }
  if (packet.isEmpty() || packet.size() > 4096 || packet.contains('\0') || packet.contains('\n') || packet.contains('\r')
      || packet.indexOf('>') < 1 || packet.indexOf(':') < packet.indexOf('>')) {
    stopReplay(); emit errorOccurred(tr("Invalid TNC2 packet in replay file.")); return;
  }
  ++replayed_; append(packet, -1, timestamp, true, outgoing);
  if (!outgoing) emit replayPacket(packet, timestamp);
  updateStatus();
}
void PacketMonitor::updateStatus()
{
  if (status_) status_->setText(tr("Received: %1  Replayed: %2  |  Capture: %3  |  Replay: %4")
    .arg(received_).arg(replayed_).arg(capturing() ? capture_.fileName() : tr("off"))
    .arg(replaying() ? replay_.fileName() : tr("stopped")));
}
void PacketMonitor::open()
{
  if (dialog_) { dialog_->show(); dialog_->raise(); dialog_->activateWindow(); return; }
  auto *dialog = new QDialog(window_); dialog_ = dialog;
  dialog->setObjectName("incomingDataDialog"); dialog->setWindowTitle(tr("Incoming Data")); dialog->resize(1000, 550);
  auto *layout = new QVBoxLayout(dialog);
  auto *filter = new QLineEdit(dialog); filter->setPlaceholderText(tr("Filter packets by callsign or text…")); layout->addWidget(filter);
  auto *proxy = new QSortFilterProxyModel(dialog); proxy->setSourceModel(model_);
  proxy->setFilterKeyColumn(2); proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
  connect(filter, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
  auto *table = new QTableView(dialog); table->setModel(proxy); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table->setWordWrap(false); layout->addWidget(table);
  connect(proxy, &QAbstractItemModel::rowsInserted, table, [table] { table->scrollToBottom(); });
  status_ = new QLabel(dialog); layout->addWidget(status_);
  auto *buttons = new QHBoxLayout; layout->addLayout(buttons);
  auto button = [&](const QString &text, auto handler) {
    auto *item = new QPushButton(text, dialog); buttons->addWidget(item); connect(item, &QPushButton::clicked, this, handler);
  };
  button(tr("Clear"), [this] { model_->removeRows(0, model_->rowCount()); });
  button(tr("Capture…"), [this] {
    const auto path = QFileDialog::getSaveFileName(dialog_, tr("Capture incoming packets"), {}, tr("Packet capture (*.jsonl)"));
    if (!path.isEmpty()) { QString error; if (!startCapture(path, &error)) emit errorOccurred(error); }
  });
  button(tr("Stop capture"), [this] { stopCapture(); });
  button(tr("Replay…"), [this] { openLogFile(); });
  button(tr("Stop replay"), [this] { stopReplay(); });
  button(tr("Close"), [dialog] { dialog->close(); });
  layout->addWidget(new QLabel(tr("Replay updates stations and objects on the map. Recorded messages do not send replies."), dialog));
  updateStatus(); dialog->show();
}

void PacketMonitor::openLogFile()
{
  auto *chooser = new QFileDialog(window_, tr("Open log file — replay on map (10 packets/sec)"));
  chooser->setObjectName("openLogFileDialog");
  chooser->setAttribute(Qt::WA_DeleteOnClose);
  chooser->setFileMode(QFileDialog::ExistingFile);
  chooser->setNameFilter(tr("Packet logs (*.jsonl *.log *.txt);;All files (*)"));
  connect(chooser, &QFileDialog::fileSelected, this, [this](const QString &path) {
    QString error;
    if (!startReplay(path, &error)) emit errorOccurred(error);
    else open();
  });
  chooser->open();
}
