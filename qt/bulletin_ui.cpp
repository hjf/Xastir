// SPDX-License-Identifier: GPL-2.0-or-later
#include "bulletin_ui.h"
#include "message_service.h"
#include <QAction>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

BulletinUi::BulletinUi(QMainWindow *window, MessageService *service)
  : QObject(window), window_(window), service_(service), timer_(this)
{
  if (auto *action = window_->findChild<QAction *>("PULDNVI001")) {
    disconnect(action, nullptr, window_, nullptr); action->setProperty("implemented", true);
    connect(action, &QAction::triggered, this, &BulletinUi::open);
  }
  connect(service, &MessageService::changed, this, [this] { dirty_ = true; });
  connect(service, &QObject::destroyed, this, [this] { if (model_ && dialog_) model_->removeRows(0, model_->rowCount()); });
  timer_.setInterval(500); connect(&timer_, &QTimer::timeout, this, &BulletinUi::refresh); timer_.start();
}
void BulletinUi::open()
{
  if (dialog_) { dialog_->show(); dialog_->raise(); dirty_ = true; refresh(); return; }
  auto *dialog = new QDialog(window_); dialog_ = dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setObjectName("bulletinBrowser"); dialog->setWindowTitle(tr("Bulletins and Announcements"));
  auto *layout = new QVBoxLayout(dialog);
  auto *filter = new QLineEdit(dialog); filter->setObjectName("bulletinFilter"); filter->setPlaceholderText(tr("Filter sender, bulletin group, or text…")); layout->addWidget(filter);
  auto *proxy = new QSortFilterProxyModel(dialog);
  model_ = new QStandardItemModel(dialog);
  model_->setHorizontalHeaderLabels({tr("Time"), tr("Sender"), tr("Destination"), tr("Kind"), tr("Source"), tr("Text")});
  proxy->setSourceModel(model_); proxy->setFilterKeyColumn(-1); proxy->setFilterCaseSensitivity(Qt::CaseInsensitive); proxy->setSortRole(Qt::UserRole);
  connect(filter, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
  table_ = new QTableView(dialog); table_->setObjectName("bulletinTable"); table_->setModel(proxy);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setSortingEnabled(true); table_->sortByColumn(0, Qt::DescendingOrder);
  table_->horizontalHeader()->setStretchLastSection(true); table_->setColumnWidth(0, 160); table_->setColumnWidth(5, 400); layout->addWidget(table_);
  auto *note = new QLabel(tr("Current bulletin lines are keyed by sender and BLN destination. New text replaces that line. The bounded message working set is shown here; archived messages and raw packets remain available in Message Traffic. Bulletins are received without ACKs or auto-replies."), dialog);
  note->setWordWrap(true); layout->addWidget(note);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog); connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close); layout->addWidget(buttons);
  dialog->resize(1000, 420); dialog->show(); dirty_ = true; refresh();
}
void BulletinUi::refresh()
{
  if (!service_ || !dialog_ || !dialog_->isVisible() || !dirty_) return;
  dirty_ = false;
  const auto selected = table_->currentIndex().siblingAtColumn(0).data(Qt::UserRole + 1).toULongLong();
  model_->removeRows(0, model_->rowCount());
  for (const auto &message : service_->messages()) {
    if (!message.bulletin || message.outgoing) continue;
    const QStringList values{QDateTime::fromMSecsSinceEpoch(message.timestamp).toString("yyyy-MM-dd HH:mm:ss"), message.sender,
      message.recipient, message.bulletin == 2 ? tr("Announcement") : tr("Bulletin"), message.device == 256 ? tr("APRS-IS") : tr("AGWPE"), message.text};
    QList<QStandardItem *> row;
    for (int column = 0; column < values.size(); ++column) {
      auto *item = new QStandardItem(values[column]); item->setData(column == 0 ? QVariant(message.timestamp) : QVariant(values[column]), Qt::UserRole);
      item->setData(message.key, Qt::UserRole + 1); item->setToolTip(values[column]); row.append(item);
    }
    model_->appendRow(row);
  }
  for (int row = 0; row < table_->model()->rowCount(); ++row)
    if (table_->model()->index(row, 0).data(Qt::UserRole + 1).toULongLong() == selected) { table_->setCurrentIndex(table_->model()->index(row, 0)); break; }
}
