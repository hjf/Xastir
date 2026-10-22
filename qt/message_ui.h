// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "radio_interface.h"
#include "message_service.h"
#include <QDialog>
#include <QPointer>
#include <QSettings>
class QMainWindow; class AprsisClient;
class QComboBox;
class MessageUi : public QObject {
  Q_OBJECT
public:
  MessageUi(QMainWindow *window, MessageService *service,
            QList<RadioInterface *> clients, const QString &legacyAutoReply);
  void openConversation(QString peer = {}, bool group = false);
  void openPending();
  void setClients(const QList<RadioInterface *> &clients, AprsisClient *internet = nullptr);
private:
  void editAutoReply();
  void query(const QByteArray &query, const QString &title);
  void populateInterfaces(QComboBox *combo);
  RadioInterface *selectedClient(QComboBox *combo) const;
  void saveSettings();
  QMainWindow *window_;
  MessageService *service_;
  QList<QPointer<RadioInterface>> clients_;
  QHash<QString, QPointer<QDialog>> conversations_;
  QPointer<QDialog> pending_;
  QPointer<AprsisClient> internet_;
  QSettings settings_;
};
