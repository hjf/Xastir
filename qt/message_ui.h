// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "agwpe_client.h"
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
            QList<AgwpeClient *> clients, const QString &legacyAutoReply);
  void openConversation(QString peer = {}, bool group = false);
  void openPending();
  void setClients(const QList<AgwpeClient *> &clients, AprsisClient *internet = nullptr);
private:
  void editAutoReply();
  void query(const QByteArray &query, const QString &title);
  void populateInterfaces(QComboBox *combo);
  AgwpeClient *selectedClient(QComboBox *combo) const;
  void saveSettings();
  QMainWindow *window_;
  MessageService *service_;
  QList<QPointer<AgwpeClient>> clients_;
  QHash<QString, QPointer<QDialog>> conversations_;
  QPointer<QDialog> pending_;
  QPointer<AprsisClient> internet_;
  QSettings settings_;
};
