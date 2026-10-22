// SPDX-License-Identifier: GPL-2.0-or-later
#include "message_ui.h"
#include "aprsis_client.h"
#include "bulletin_ui.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QTableWidget *messageTable(QWidget *parent)
{
  auto *table = new QTableWidget(parent);
  table->setObjectName("messageTable");
  table->setColumnCount(8);
  table->setHorizontalHeaderLabels({QObject::tr("Time"), QObject::tr("From"), QObject::tr("To"),
    QObject::tr("Message"), QObject::tr("Status"), QObject::tr("ID"), QObject::tr("Attempts"), QObject::tr("Next retry")});
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  return table;
}
void fillTable(QTableWidget *table, const QList<AprsMessage> &messages)
{
  const bool atBottom = table->verticalScrollBar()->value() == table->verticalScrollBar()->maximum();
  quint64 selected = 0;
  if (table->currentRow() >= 0 && table->item(table->currentRow(), 0))
    selected = table->item(table->currentRow(), 0)->data(Qt::UserRole).toULongLong();
  table->setRowCount(messages.size());
  int row = 0;
  for (const auto &message : messages) {
    const QString retry = message.pending ? QObject::tr("%1 s").arg(std::max<qint64>(0,
      (message.nextRetry - QDateTime::currentMSecsSinceEpoch() + 999) / 1000)) : QString();
    const QStringList values{QDateTime::fromMSecsSinceEpoch(message.timestamp).toString("MM-dd HH:mm:ss"),
      message.sender, message.recipient, message.text, message.status, message.id, QString::number(message.attempts), retry};
    for (int col = 0; col < values.size(); ++col) {
      auto *item = new QTableWidgetItem(values[col]);
      item->setData(Qt::UserRole, QVariant::fromValue(message.key));
      table->setItem(row, col, item);
    }
    if (selected == message.key) table->selectRow(row);
    ++row;
  }
  if (atBottom) table->scrollToBottom();
}
void showError(QWidget *parent, const QString &error)
{
  auto *dialog = new QMessageBox(QMessageBox::Warning, QObject::tr("APRS Messages"), error, QMessageBox::Ok, parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->open();
}
QDialog *makeDialog(QMainWindow *window, const QString &title, const char *name)
{
  auto *dialog = new QDialog(window);
  dialog->setObjectName(name); dialog->setWindowTitle(title);
  dialog->setAttribute(Qt::WA_DeleteOnClose); return dialog;
}
}
MessageUi::MessageUi(QMainWindow *window, MessageService *service,
                     QList<RadioInterface *> clients, const QString &legacyAutoReply)
  : QObject(window), window_(window), service_(service)
{
  new BulletinUi(window_, service_);
  for (auto *client : clients) clients_.append(client);
  service_->setAutoReplyText(settings_.value("messages/autoReplyText", legacyAutoReply).toString());
  service_->setAutoReply(false); // APRS auto-answer defaults off on startup.
  service_->setSatelliteMode(settings_.value("messages/satelliteAckMode", false).toBool());
  for (const auto &group : settings_.value("messages/groups").toStringList()) service_->addGroup(group);
  auto bind = [this](const char *id, auto handler) {
    auto *action = window_->findChild<QAction *>(QString::fromLatin1(id));
    if (!action) return;
    QObject::disconnect(action, nullptr, window_, nullptr);
    action->setProperty("implemented", true);
    connect(action, &QAction::triggered, this, handler);
  };
  bind("PULDNMG001", [this] { openConversation(); });
  bind("PULDNMG007", [this] { openPending(); });
  bind("PULDNMG002", [this] { openConversation("ALL", true); });
  bind("PULDNMG003", [this] {
    auto *confirm = new QMessageBox(QMessageBox::Question, tr("Clear All Outgoing Messages"),
      tr("Cancel all pending messages and clear outgoing message history?"), QMessageBox::Yes | QMessageBox::No, window_);
    confirm->setDefaultButton(QMessageBox::No); confirm->setAttribute(Qt::WA_DeleteOnClose);
    connect(confirm, &QDialog::finished, this, [this](int result) { if (result == QMessageBox::Yes) service_->clearOutgoing(); });
    confirm->open();
  });
  bind("PULDQUS001", [this] { query("?APRS?", tr("General Stations Query")); });
  bind("PULDQUS002", [this] { query("?IGATE?", tr("IGate Stations Query")); });
  bind("PULDQUS003", [this] { query("?WX?", tr("WX Stations Query")); });
  bind("PULDNMG004", [this] { editAutoReply(); });
  bind("PULDNMG005", [this](bool enabled) {
    if (enabled && service_->autoReplyText().isEmpty()) {
      window_->findChild<QAction *>("PULDNMG005")->setChecked(false); editAutoReply(); return;
    }
    service_->setAutoReply(enabled); saveSettings();
  });
  bind("PULDNMG006", [this](bool enabled) { service_->setSatelliteMode(enabled); saveSettings(); });
  window_->findChild<QAction *>("PULDNMG005")->setChecked(service_->autoReply());
  window_->findChild<QAction *>("PULDNMG006")->setChecked(service_->satelliteMode());
  connect(service_, &MessageService::autoReplyChanged, this, [this](bool enabled) {
    window_->findChild<QAction *>("PULDNMG005")->setChecked(enabled);
  });
  connect(service_, &MessageService::incomingMessage, this, [this](const QString &peer, bool group) {
    openConversation(peer, group);
  });
  connect(service_, &MessageService::operationError, window_, [window](const QString &error) { showError(window, error); });
}
void MessageUi::saveSettings()
{
  settings_.setValue("messages/autoReplyText", service_->autoReplyText());
  settings_.setValue("messages/satelliteAckMode", service_->satelliteMode());
  settings_.setValue("messages/groups", QStringList(service_->groups().values()));
}
void MessageUi::populateInterfaces(QComboBox *combo)
{
  for (const auto &client : clients_) if (client) {
    combo->addItem(client->description(), client->deviceIndex());
  }
  if(internet_)combo->addItem(tr("APRS-IS%1").arg(internet_->transmitEnabled()?QString():tr(" — receive only")),256);
  if (combo->count() == 0) combo->addItem(tr("No radio interfaces configured"), -1);
  for (int i = 0; i < combo->count(); ++i) {
    for (const auto &client : clients_) if (client && client->deviceIndex() == combo->itemData(i).toInt()
        && client->transmitEnabled()) { combo->setCurrentIndex(i); return; }
  }
}
RadioInterface *MessageUi::selectedClient(QComboBox *combo) const
{
  for (const auto &client : clients_)
    if (client && client->deviceIndex() == combo->currentData().toInt()) return client;
  return nullptr;
}
void MessageUi::openConversation(QString peer, bool group)
{
  peer = peer.trimmed().toUpper();
  const QString key = (group ? "group:" : "station:") + peer;
  if (conversations_.value(key)) { conversations_[key]->show(); conversations_[key]->raise(); return; }
  auto *dialog = makeDialog(window_, group ? tr("Group Messages") : tr("Send Message To"), "messageConversation");
  conversations_.insert(key, dialog); dialog->resize(1000, 480);
  auto *layout = new QVBoxLayout(dialog);
  auto *form = new QFormLayout;
  auto *recipient = new QComboBox(dialog);
  recipient->setObjectName("messageRecipient"); recipient->setEditable(true);
  if (group) { QStringList groups(service_->groups().values()); groups.sort(); recipient->addItems(groups); }
  recipient->setCurrentText(peer);
  auto *interfaces = new QComboBox(dialog); interfaces->setObjectName("messageInterface"); populateInterfaces(interfaces);
  for(auto i=service_->messages().crbegin();i!=service_->messages().crend();++i)if(i->sender==peer && i->recipient==service_->callsign()) {
    const int index=interfaces->findData(i->device);if(index>=0)interfaces->setCurrentIndex(index);break;
  }
  auto *path = new QLineEdit(dialog); path->setObjectName("messagePath"); path->setPlaceholderText(tr("Empty = direct, e.g. WIDE1-1,WIDE2-1"));
  form->addRow(group ? tr("Group:") : tr("To callsign:"), recipient);
  form->addRow(tr("Interface:"), interfaces); form->addRow(tr("Path:"), path); layout->addLayout(form);
  auto *connection = new QLabel(dialog); layout->addWidget(connection);
  auto *table = messageTable(dialog); layout->addWidget(table);
  auto *text = new QLineEdit(dialog); text->setObjectName("messageText"); text->setMaxLength(67);
  text->setPlaceholderText(tr("Message text (up to 67 ASCII characters)")); layout->addWidget(text);
  auto *count = new QLabel(dialog); layout->addWidget(count);
  connect(text, &QLineEdit::textChanged, count, [count](const QString &value) { count->setText(QObject::tr("%1 / 67 characters").arg(value.size())); });
  count->setText(tr("0 / 67 characters"));
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  auto *send = buttons->addButton(tr("Send"), QDialogButtonBox::ActionRole); send->setObjectName("sendMessageButton");
  auto *join = group ? buttons->addButton(tr("Join Group"), QDialogButtonBox::ActionRole) : nullptr;
  layout->addWidget(buttons);
  // Enter is handled by the message field. QDialog's automatic default-button
  // handling would otherwise click Send again after the first send clears it.
  for (auto *button : buttons->findChildren<QPushButton *>()) {
    button->setAutoDefault(false);
    button->setDefault(false);
  }
  auto refresh = [this, recipient, table, group] {
    const QString address = recipient->currentText().trimmed().toUpper();
    QList<AprsMessage> visible;
    for (const auto &message : service_->messages()) {
      if (group ? message.recipient == address
          : ((message.sender == service_->callsign() && message.recipient == address)
             || (message.sender == address && message.recipient == service_->callsign()))) visible.append(message);
    }
    fillTable(table, visible);
  };
  connect(service_, &MessageService::changed, dialog, refresh);
  connect(recipient, &QComboBox::currentTextChanged, dialog, [this, dialog, group, refresh](const QString &name) {
    for (auto it = conversations_.begin(); it != conversations_.end(); )
      if (it.value() == dialog) it = conversations_.erase(it); else ++it;
    const QString key = (group ? "group:" : "station:") + name.trimmed().toUpper();
    if (!conversations_.value(key)) conversations_.insert(key, dialog);
    refresh();
  }); refresh();
  auto updateConnection = [this, interfaces, path, connection, send](bool resetPath) {
    if(interfaces->currentData().toInt()==256) {
      send->setEnabled(internet_ && internet_->transmitEnabled());
      path->setEnabled(false);if(resetPath)path->clear();
      connection->setText(internet_ && internet_->canTransmit()?tr("APRS-IS verified — ready to send."):tr("APRS-IS receive only or awaiting verification. Enable TX and enter your passcode in Interface Control."));return;
    }
    path->setEnabled(true);
    auto *client = selectedClient(interfaces);
    const bool enabled = client && client->transmitEnabled();
    if (enabled && !client->connected()) client->start();
    send->setEnabled(enabled);
    connection->setText(!client ? tr("Configure a radio interface to send messages.")
      : !enabled ? tr("Transmission is disabled for this interface.")
      : client->connected() ? tr("Connected — ready to send.") : tr("Disconnected — messages will wait for reconnection."));
    if (resetPath) path->setText(client ? client->path() : QString());
  };
  connect(interfaces, &QComboBox::currentIndexChanged, dialog, [updateConnection] { updateConnection(true); });
  for (const auto &client : clients_) if (client)
    connect(client, &RadioInterface::statusChanged, dialog, [updateConnection] { updateConnection(false); });
  if(internet_)connect(internet_,&AprsisClient::statusChanged,dialog,[updateConnection]{updateConnection(false);});
  updateConnection(true);
  connect(send, &QPushButton::clicked, dialog, [this, dialog, recipient, interfaces, text, path, group] {
    QString error;
    const QString route = path->text().isEmpty() ? QStringLiteral("") : path->text();
    if (!service_->sendMessage(recipient->currentText(), text->text(), group, interfaces->currentData().toInt(), route, &error))
      showError(dialog, error);
    else { text->clear(); if (group) saveSettings(); }
  });
  connect(text, &QLineEdit::returnPressed, send, [send] { if (send->isEnabled()) send->click(); });
  if (join) connect(join, &QPushButton::clicked, dialog, [this, dialog, recipient, refresh] {
    if (!service_->addGroup(recipient->currentText())) { showError(dialog, tr("Group must be 1–9 letters, digits or hyphens.")); return; }
    saveSettings(); refresh();
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  dialog->show(); text->setFocus();
}
void MessageUi::openPending()
{
  if (pending_) { pending_->show(); pending_->raise(); return; }
  auto *dialog = makeDialog(window_, tr("Pending and Outgoing Messages"), "pendingMessagesDialog");
  pending_ = dialog; dialog->resize(1100, 420);
  auto *layout = new QVBoxLayout(dialog);
  auto *pendingOnly = new QCheckBox(tr("Show only pending messages"), dialog); pendingOnly->setChecked(true);
  layout->addWidget(pendingOnly);
  auto *table = messageTable(dialog); layout->addWidget(table);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  auto *cancel = buttons->addButton(tr("Cancel Selected"), QDialogButtonBox::ActionRole);
  auto *retry = buttons->addButton(tr("Retry Selected"), QDialogButtonBox::ActionRole);
  cancel->setObjectName("cancelMessageButton"); retry->setObjectName("retryMessageButton");
  auto *refreshButton = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
  layout->addWidget(buttons);
  auto refresh = [this, table, pendingOnly] {
    QList<AprsMessage> visible;
    for (const auto &message : service_->messages())
      if (message.outgoing && (!pendingOnly->isChecked() || message.pending)) visible.append(message);
    fillTable(table, visible);
  };
  connect(service_, &MessageService::changed, dialog, refresh);
  connect(pendingOnly, &QCheckBox::toggled, dialog, refresh);
  connect(refreshButton, &QPushButton::clicked, dialog, refresh);
  auto *timer = new QTimer(dialog); timer->setInterval(1000);
  connect(timer, &QTimer::timeout, dialog, refresh); timer->start(); refresh();
  auto selected = [table] { return table->currentRow() >= 0 && table->item(table->currentRow(), 0)
    ? table->item(table->currentRow(), 0)->data(Qt::UserRole).toULongLong() : 0ULL; };
  auto updateButtons = [this, cancel, retry, selected] {
    cancel->setEnabled(false); retry->setEnabled(false);
    for (const auto &message : service_->messages()) if (message.key == selected()) {
      cancel->setEnabled(message.pending);
      retry->setEnabled(message.retryable && message.status != tr("Acknowledged") && message.status != tr("Rejected"));
      break;
    }
  };
  connect(table, &QTableWidget::itemSelectionChanged, dialog, updateButtons);
  connect(service_, &MessageService::changed, dialog, updateButtons); updateButtons();
  connect(cancel, &QPushButton::clicked, dialog, [this, selected] { service_->cancel(selected()); });
  connect(retry, &QPushButton::clicked, dialog, [this, selected] { service_->retry(selected()); });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close); dialog->show();
}
void MessageUi::editAutoReply()
{
  auto *dialog = makeDialog(window_, tr("Modify Auto Reply Message"), "autoReplyDialog");
  auto *layout = new QVBoxLayout(dialog);
  layout->addWidget(new QLabel(tr("Replies use the AA: prefix. Enter up to 64 ASCII characters."), dialog));
  auto *text = new QLineEdit(service_->autoReplyText(), dialog); text->setMaxLength(64); text->setObjectName("autoReplyText");
  layout->addWidget(text);
  auto *enabled = new QCheckBox(tr("Enable auto reply"), dialog); enabled->setChecked(service_->autoReply()); layout->addWidget(enabled);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog); layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, text, enabled] {
    QString error;
    if (!service_->setAutoReplyText(text->text(), &error)) { showError(dialog, error); return; }
    service_->setAutoReply(enabled->isChecked());
    window_->findChild<QAction *>("PULDNMG005")->setChecked(service_->autoReply());
    saveSettings(); dialog->close();
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close); dialog->show();
}
void MessageUi::query(const QByteArray &queryText, const QString &title)
{
  auto *dialog = makeDialog(window_, title, "stationQueryDialog");
  auto *layout = new QVBoxLayout(dialog);
  layout->addWidget(new QLabel(tr("Broadcast %1 to stations on the selected interface.").arg(QString::fromLatin1(queryText)), dialog));
  auto *interfaces = new QComboBox(dialog); populateInterfaces(interfaces); layout->addWidget(interfaces);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
  auto *send = buttons->addButton(tr("Send Query"), QDialogButtonBox::ActionRole); layout->addWidget(buttons);
  auto update = [this, send, interfaces] { auto *client = selectedClient(interfaces);
    if (client && client->transmitEnabled() && !client->connected()) client->start();
    send->setEnabled(client && client->transmitEnabled() && client->connected()); };
  for (const auto &client : clients_) if (client) connect(client, &RadioInterface::statusChanged, dialog, update);
  connect(interfaces, &QComboBox::currentIndexChanged, dialog, update); update();
  connect(send, &QPushButton::clicked, dialog, [this, dialog, interfaces, queryText] {
    QString error;
    if (!service_->sendQuery(queryText, interfaces->currentData().toInt(), &error)) showError(dialog, error);
    else dialog->close();
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close); dialog->show();
}

void MessageUi::setClients(const QList<RadioInterface *> &clients, AprsisClient *internet) {
  internet_=internet;
  for(const auto &dialog:conversations_) if(dialog) dialog->close();
  conversations_.clear(); if(pending_) pending_->close();
  clients_.clear(); for(auto *client:clients) clients_.append(client);
}
