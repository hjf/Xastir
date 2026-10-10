// SPDX-License-Identifier: GPL-2.0-or-later
#include "interface_control.h"
#include "aprsis_client.h"
#include "message_service.h"
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
namespace
{
QJsonObject serialize(const XastirConfig &c)
{
  QJsonArray devices;
  for (const auto &s : c.agwpe)
    devices.append(QJsonObject{{"device", s.deviceIndex},
                               {"host", s.host},
                               {"port", s.port},
                               {"radio", int(s.radioPort)},
                               {"path", s.path},
                               {"auto", s.connectOnStartup},
                               {"reconnect", s.reconnect},
                               {"tx", s.transmitEnabled},
                               {"password", QString::fromUtf8(s.password)}});
  QJsonObject result{{"callsign", c.callsign},
                     {"agwpe", devices},
                     {"aprsis", QJsonObject{{"host", c.aprsis.host},
                                            {"port", c.aprsis.port},
                                            {"filter", c.aprsis.filter},
                                            {"enabled", c.aprsis.enabled},
                                            {"reconnect", c.aprsis.reconnect},
                                            {"tx", c.aprsis.transmitEnabled},
                                            {"passcode", c.aprsis.passcode}}}};
  if (c.localStation)
  {
    const auto &s = *c.localStation;
    result["local"] = QJsonObject{{"lat", s.position.latitude},
                                  {"lon", s.position.longitude},
                                  {"table", QString(s.symbolTable)},
                                  {"symbol", QString(s.symbolCode)},
                                  {"comment", s.comment}};
  }
  return result;
}
} // namespace
void readQtConfiguration(XastirConfig &c)
{
  QSettings settings;
  const auto raw = settings.value("interfaces/config").toByteArray();
  if (raw.isEmpty())
    return;
  const auto o = QJsonDocument::fromJson(raw).object();
  if (o.isEmpty())
    return;
  c.callsign = o["callsign"].toString();
  c.agwpe.clear();
  for (const auto &v : o["agwpe"].toArray())
  {
    const auto d = v.toObject();
    AgwpeSettings s;
    s.deviceIndex = d["device"].toInt();
    s.host = d["host"].toString();
    s.port = quint16(d["port"].toInt(8000));
    s.radioPort = quint32(d["radio"].toInt());
    s.path = d["path"].toString();
    s.callsign = c.callsign;
    s.connectOnStartup = d["auto"].toBool();
    s.reconnect = d["reconnect"].toBool(true);
    s.transmitEnabled = d["tx"].toBool();
    s.password = d["password"].toString().toUtf8();
    c.agwpe.append(s);
  }
  const auto internet = o["aprsis"].toObject();
  c.aprsis.host = internet["host"].toString("rotate.aprs2.net");
  c.aprsis.port = quint16(internet["port"].toInt(14580));
  c.aprsis.filter = internet["filter"].toString("r/0/0/100");
  c.aprsis.enabled = internet["enabled"].toBool();
  c.aprsis.reconnect = internet["reconnect"].toBool(true);
  c.aprsis.transmitEnabled = internet["tx"].toBool();
  c.aprsis.passcode = internet["passcode"].toString();
  c.localStation.reset();
  if (o.contains("local"))
  {
    const auto d = o["local"].toObject();
    Station s;
    s.callsign = c.callsign;
    s.position = {d["lat"].toDouble(), d["lon"].toDouble()};
    s.symbolTable =
        d["table"].toString("/").isEmpty() ? QChar('/') : d["table"].toString("/").at(0);
    s.symbolCode =
        d["symbol"].toString("-").isEmpty() ? QChar('-') : d["symbol"].toString("-").at(0);
    s.comment = d["comment"].toString();
    s.local = true;
    QString error;
    if (StationModel::validate(s, &error))
      c.localStation = s;
  }
}
InterfaceControl::InterfaceControl(QMainWindow *w, XastirConfig config)
    : QObject(w), window_(w), config_(std::move(config))
{
  if (auto *a = w->findChild<QAction *>("PULDNTNT04"))
  {
    disconnect(a, nullptr, w, nullptr);
    a->setProperty("implemented", true);
    connect(a, &QAction::triggered, this, &InterfaceControl::open);
  }
}
void InterfaceControl::open()
{
  if (dialog_)
  {
    dialog_->show();
    dialog_->raise();
    return;
  }
  auto *d = new QDialog(window_);
  dialog_ = d;
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->setObjectName("interfaceControl");
  d->setWindowTitle(tr("Interface Control"));
  d->resize(980, 500);
  auto *layout = new QVBoxLayout(d);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  auto *call = new QLineEdit(config_.callsign, d);
  call->setObjectName("interfaceCallsign");
  form->addRow(tr("Station callsign"), call);
  auto *lat = new QDoubleSpinBox(d), *lon = new QDoubleSpinBox(d);
  lat->setRange(-90, 90);
  lon->setRange(-180, 180);
  lat->setDecimals(6);
  lon->setDecimals(6);
  lat->setObjectName("stationLatitude");
  lon->setObjectName("stationLongitude");
  const auto local = config_.localStation.value_or(Station{});
  lat->setValue(local.position.latitude);
  lon->setValue(local.position.longitude);
  form->addRow(tr("Latitude"), lat);
  form->addRow(tr("Longitude"), lon);
  auto *symbol = new QLineEdit(QString(local.symbolTable) + local.symbolCode, d);
  symbol->setMaxLength(2);
  form->addRow(tr("APRS table + symbol"), symbol);
  auto *comment = new QLineEdit(local.comment, d);
  form->addRow(tr("Position comment"), comment);
  auto *internet = new QCheckBox(tr("Connect to APRS-IS"), d);
  internet->setObjectName("aprsisEnabled");
  internet->setChecked(config_.aprsis.enabled);
  form->addRow(internet);
  auto *internetHost = new QLineEdit(config_.aprsis.host, d);
  internetHost->setObjectName("aprsisHost");
  form->addRow(tr("APRS-IS host"), internetHost);
  auto *internetPort = new QSpinBox(d);
  internetPort->setRange(1, 65535);
  internetPort->setValue(config_.aprsis.port);
  form->addRow(tr("APRS-IS TCP port"), internetPort);
  auto *internetTx =
      new QCheckBox(tr("Enable APRS-IS message transmission and acknowledgments"), d);
  internetTx->setObjectName("aprsisTx");
  internetTx->setChecked(config_.aprsis.transmitEnabled);
  form->addRow(internetTx);
  auto *passcode = new QLineEdit(config_.aprsis.passcode, d);
  passcode->setObjectName("aprsisPasscode");
  passcode->setEchoMode(QLineEdit::Password);
  passcode->setMaxLength(5);
  form->addRow(tr("APRS-IS passcode"), passcode);
  QString initialFilter = config_.aprsis.filter;
  if (!config_.aprsis.enabled && initialFilter == "r/0/0/100")
    initialFilter =
        QString("r/%1/%2/100").arg(local.position.latitude).arg(local.position.longitude);
  auto *filter = new QLineEdit(initialFilter, d);
  filter->setObjectName("aprsisFilter");
  form->addRow(tr("APRS-IS filter"), filter);
  auto *reconnect = new QCheckBox(tr("Reconnect APRS-IS automatically"), d);
  reconnect->setChecked(config_.aprsis.reconnect);
  form->addRow(reconnect);
  auto *table = new QTableWidget(0, 9, d);
  table->setObjectName("agwpeInterfaces");
  table->setHorizontalHeaderLabels({tr("Device"), tr("Host"), tr("TCP port"), tr("Radio"),
                                    tr("Path"), tr("Startup"), tr("Reconnect"), tr("Enable TX"),
                                    tr("Password")});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  layout->addWidget(table);
  auto append = [table](const AgwpeSettings &s)
  {
    const int row = table->rowCount();
    table->insertRow(row);
    QStringList values{QString::number(s.deviceIndex), s.host, QString::number(s.port),
                       QString::number(s.radioPort + 1), s.path};
    for (int i = 0; i < 5; ++i)
      table->setItem(row, i, new QTableWidgetItem(values[i]));
    for (int i = 5; i < 8; ++i)
    {
      auto *item = new QTableWidgetItem;
      item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      item->setCheckState((i == 5   ? s.connectOnStartup
                           : i == 6 ? s.reconnect
                                    : s.transmitEnabled)
                              ? Qt::Checked
                              : Qt::Unchecked);
      table->setItem(row, i, item);
    }
    auto *password = new QLineEdit(QString::fromUtf8(s.password));
    password->setEchoMode(QLineEdit::Password);
    table->setCellWidget(row, 8, password);
  };
  for (const auto &s : config_.agwpe)
    append(s);
  auto *add = new QPushButton(tr("Add AGWPE"), d);
  layout->addWidget(add);
  connect(add, &QPushButton::clicked, d,
          [table, append]
          {
            AgwpeSettings s;
            s.deviceIndex = 0;
            while (true)
            {
              bool used = false;
              for (int i = 0; i < table->rowCount(); ++i)
                used |= table->item(i, 0)->text().toInt() == s.deviceIndex;
              if (!used)
                break;
              ++s.deviceIndex;
            }
            s.host = "127.0.0.1";
            append(s);
          });
  auto *remove = new QPushButton(tr("Remove selected interface"), d);
  layout->addWidget(remove);
  connect(remove, &QPushButton::clicked, d,
          [table]
          {
            if (table->currentRow() >= 0)
              table->removeRow(table->currentRow());
          });
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, d);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, d,
          [this, d, call, lat, lon, local, symbol, comment, table, internet, internetHost,
           internetPort, filter, reconnect, internetTx, passcode]
          {
            XastirConfig next = config_;
            next.callsign = call->text().trimmed().toUpper();
            Station station = local;
            station.callsign = next.callsign;
            station.position = {lat->value(), lon->value()};
            station.comment = comment->text();
            station.local = true;
            QString error;
            if (symbol->text().size() != 2)
              error = tr("Enter exactly a table and symbol character.");
            else
            {
              station.symbolTable = symbol->text()[0];
              station.symbolCode = symbol->text()[1];
              StationModel::validate(station, &error);
            }
            if (error.isEmpty() && !station.comment.isEmpty() &&
                !MessageService::validText(station.comment, &error))
            {
            }
            next.aprsis = {internetHost->text().trimmed(), quint16(internetPort->value()),
                           filter->text().trimmed(),       internet->isChecked(),
                           reconnect->isChecked(),         internetTx->isChecked(),
                           passcode->text().trimmed()};
            if (error.isEmpty() && next.aprsis.enabled)
              AprsisClient::validate(next.aprsis, next.callsign, &error);
            next.localStation = station;
            next.agwpe.clear();
            QSet<int> ids;
            for (int row = 0; row < table->rowCount() && error.isEmpty(); ++row)
            {
              AgwpeSettings s;
              bool ok;
              s.deviceIndex = table->item(row, 0)->text().toInt(&ok);
              if (!ok || s.deviceIndex < 0 || s.deviceIndex > 255 || ids.contains(s.deviceIndex))
              {
                error = tr("Device numbers must be unique and between 0 and 255.");
                break;
              }
              ids.insert(s.deviceIndex);
              s.callsign = next.callsign;
              s.host = table->item(row, 1)->text().trimmed();
              const int port = table->item(row, 2)->text().toInt(&ok);
              if (!ok || port < 1 || port > 65535 || s.host.isEmpty() || s.host.contains('\n') ||
                  s.host.contains('\r'))
              {
                error = tr("Enter a host and TCP port between 1 and 65535.");
                break;
              }
              s.port = quint16(port);
              const int radio = table->item(row, 3)->text().toInt(&ok);
              if (!ok || radio < 1 || radio > 256)
              {
                error = tr("Radio port must be between 1 and 256.");
                break;
              }
              s.radioPort = quint32(radio - 1);
              s.path = table->item(row, 4)->text().trimmed().toUpper();
              if (!MessageService::validPath(s.path, &error))
                break;
              s.connectOnStartup = table->item(row, 5)->checkState() == Qt::Checked;
              s.reconnect = table->item(row, 6)->checkState() == Qt::Checked;
              s.transmitEnabled = table->item(row, 7)->checkState() == Qt::Checked;
              s.password = qobject_cast<QLineEdit *>(table->cellWidget(row, 8))->text().toUtf8();
              next.agwpe.append(s);
            }
            if (!error.isEmpty())
            {
              QMessageBox::warning(d, tr("Interface settings"), error);
              return;
            }
            QSettings saved;
            saved.setValue("interfaces/config",
                           QJsonDocument(serialize(next)).toJson(QJsonDocument::Compact));
            saved.sync();
            if (saved.status() != QSettings::NoError)
            {
              QMessageBox::warning(d, tr("Interface settings"),
                                   tr("Could not save configuration."));
              return;
            }
            const bool changed = next.callsign != config_.callsign;
            config_ = next;
            emit configurationChanged(next);
            if (changed)
              QMessageBox::information(
                  d, tr("Station identity"),
                  tr("Settings saved. Restart Xastir Qt to apply the new callsign."));
            d->accept();
          });
  d->show();
}
