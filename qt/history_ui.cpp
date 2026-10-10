// SPDX-License-Identifier: GPL-2.0-or-later
#include "history_ui.h"
#include "history_store.h"
#include "message_service.h"
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
HistoryUi::HistoryUi(QMainWindow *window, HistoryStore *store, QString identity)
    : QObject(window), window_(window), store_(store), identity_(std::move(identity))
{
  if (auto *menu = window->findChild<QMenu *>("MENUTB0002"))
  {
    menu->addSeparator();
    auto *action = menu->addAction(tr("History…"));
    action->setObjectName("actionHistory");
    action->setProperty("implemented", true);
    connect(action, &QAction::triggered, this, &HistoryUi::open);
  }
}
void HistoryUi::open()
{
  if (!dialog_)
  {
    auto *d = new QDialog(window_);
    dialog_ = d;
    d->setObjectName("historyDialog");
    d->setWindowTitle(tr("APRS History"));
    d->resize(950, 550);
    auto *layout = new QVBoxLayout(d);
    auto *info =
        new QLabel(tr("Traffic: last 7 days, at most 100,000 packets / 64 MiB of packet data.\n"
                      "Messages: retained indefinitely. Restart restores station state without "
                      "transmitting.\nDatabase: %1")
                       .arg(store_->path()),
                   d);
    info->setTextFormat(Qt::PlainText);
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(info);
    tabs_ = new QTabWidget(d);
    layout->addWidget(tabs_);
    auto table = [d](QStringList labels)
    {
      auto *t = new QTableWidget(d);
      t->setColumnCount(labels.size());
      t->setHorizontalHeaderLabels(labels);
      t->setEditTriggers(QAbstractItemView::NoEditTriggers);
      t->setSelectionBehavior(QAbstractItemView::SelectRows);
      t->horizontalHeader()->setStretchLastSection(true);
      return t;
    };
    traffic_ = table({tr("Received"), tr("Interface"), tr("Packet")});
    tabs_->addTab(traffic_, tr("Traffic"));
    messages_ = table({tr("Time"), tr("From"), tr("To"), tr("Message"), tr("Status / ID")});
    tabs_->addTab(messages_, tr("Messages"));
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { refresh(); });
    status_ = new QLabel(d);
    layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    auto *latest = new QPushButton(tr("Latest / Refresh"), d);
    buttons->addWidget(latest);
    connect(latest, &QPushButton::clicked, this,
            [this]
            {
              packetBefore_ = messageBefore_ = 0;
              refresh();
            });
    auto *older = new QPushButton(tr("Older"), d);
    buttons->addWidget(older);
    connect(older, &QPushButton::clicked, this, [this] { refresh(true); });
    buttons->addStretch();
    auto *close = new QPushButton(tr("Close"), d);
    buttons->addWidget(close);
    connect(close, &QPushButton::clicked, d, &QDialog::close);
  }
  packetBefore_ = messageBefore_ = 0;
  refresh();
  dialog_->show();
  dialog_->raise();
}
void HistoryUi::refresh(bool older)
{
  auto put = [](QTableWidget *table, int row, int col, const QString &text)
  { table->setItem(row, col, new QTableWidgetItem(text)); };
  auto time = [](qint64 timestamp)
  {
    return QDateTime::fromMSecsSinceEpoch(timestamp).toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
  };
  if (tabs_->currentIndex() == 0)
  {
    const auto before = older ? oldestPacket_ : packetBefore_;
    const auto rows = store_->packets(before);
    if (older && rows.isEmpty())
    {
      status_->setText(tr("No older traffic."));
      return;
    }
    packetBefore_ = before;
    traffic_->setRowCount(rows.size());
    oldestPacket_ = 0;
    for (int i = 0; i < rows.size(); ++i)
    {
      const auto &p = rows[i];
      oldestPacket_ = p.key;
      put(traffic_, i, 0, time(p.timestamp));
      put(traffic_, i, 1,
          p.device == 256
              ? tr("APRS-IS")
              : tr("AGWPE %1%2").arg(p.device).arg(p.outgoing ? tr(" TX queued") : QString()));
      put(traffic_, i, 2, QString::fromLatin1(p.packet));
    }
    traffic_->resizeColumnsToContents();
    status_->setText(tr("%1 packets, newest first.").arg(rows.size()));
  }
  else
  {
    const auto before = older ? oldestMessage_ : messageBefore_;
    const auto rows = store_->messages(identity_, before);
    if (older && rows.isEmpty())
    {
      status_->setText(tr("No older messages."));
      return;
    }
    messageBefore_ = before;
    messages_->setRowCount(rows.size());
    oldestMessage_ = 0;
    for (int i = 0; i < rows.size(); ++i)
    {
      const auto o = rows[i].toObject();
      oldestMessage_ = o["key"].toString().toLongLong();
      put(messages_, i, 0, time(o["timestamp"].toString().toLongLong()));
      put(messages_, i, 1, o["sender"].toString());
      put(messages_, i, 2, o["recipient"].toString());
      put(messages_, i, 3, o["text"].toString());
      put(messages_, i, 4,
          MessageService::statusText(o["status"].toInt(), o["error"].toString()) + " / " +
              o["id"].toString());
    }
    messages_->resizeColumnsToContents();
    status_->setText(
        tr("%1 archived messages for %2, newest first.").arg(rows.size()).arg(identity_));
  }
}
