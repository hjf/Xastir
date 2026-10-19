// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_menu.h"
#include "map_widget.h"
#include "aprs_decoder.h"
#include "kml_export.h"
#include <QAction>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QUrlQuery>
#include <QXmlStreamWriter>
#include <QPushButton>
#include <QTimeZone>
#include <algorithm>

StationMenu::StationMenu(QMainWindow *window, StationModel *stations, MapWidget *map,
                         QString callsign, std::optional<Station> local, QString historyDirectory)
  : QObject(window), window_(window), stations_(stations), map_(map), callsign_(std::move(callsign)),
    historyDirectory_(historyDirectory.isEmpty() ? QDir::homePath() + "/.xastir/config" : historyDirectory), local_(std::move(local))
{
  auto bind = [this](const char *id, auto handler) {
    if (auto *action = window_->findChild<QAction *>(QString::fromLatin1(id))) {
      QObject::disconnect(action, nullptr, window_, nullptr); action->setProperty("implemented", true);
      connect(action, &QAction::triggered, this, handler);
    }
  };
  struct Selection { const char *id; int Selections::*field; };
  const Selection selections[] = {
    {"PULDNDP040",&Selections::none},{"PULDNDP041",&Selections::mine},{"PULDNDP042",&Selections::tnc},
    {"PULDNDP027",&Selections::direct},{"PULDNDP043",&Selections::via_digi},{"PULDNDP034",&Selections::net},
    {"PULDNDP051",&Selections::tactical},{"PULDNDP019",&Selections::old_data},{"PULDNDP044",&Selections::stations},
    {"PULDNDP028",&Selections::fixed_stations},{"PULDNDP029",&Selections::moving_stations},{"PULDNDP030",&Selections::weather_stations},
    {"PULDNDP053",&Selections::CWOP_wx_stations},{"PULDNDP045",&Selections::objects},{"PULDNDP026",&Selections::weather_objects},
    {"PULDNDP039",&Selections::gauge_objects},{"PULDNDP057",&Selections::aircraft_objects},{"PULDNDP058",&Selections::vessel_objects},
    {"PULDNDP031",&Selections::other_objects}};
  auto selected = map_->selections(); QSettings settings;
  for (const auto &entry : selections) {
    const QString key = QString("station/selection/%1").arg(entry.id);
    selected.*entry.field = settings.value(key, bool(selected.*entry.field)).toBool();
    if (auto *action = window_->findChild<QAction *>(entry.id)) action->setChecked(selected.*entry.field);
    bind(entry.id, [this, entry, key](bool checked) {
      auto value = map_->selections(); value.*entry.field = checked; map_->setSelections(value); QSettings().setValue(key, checked);
    });
  }
  map_->setSelections(selected);
  struct Display { const char *id; int What_to_display::*field; };
  const Display displays[] = {
    {"PULDNDP010",&What_to_display::callsign},{"PULDNDP052",&What_to_display::label_all_trackpoints},
    {"PULDNDP012",&What_to_display::symbol},{"PULDNDP011",&What_to_display::symbol_rotate},{"PULDNDP007",&What_to_display::trail},
    {"PULDNDP003",&What_to_display::course},{"PULDNDP004",&What_to_display::speed},{"PULDNDP017",&What_to_display::speed_short},
    {"PULDNDP002",&What_to_display::altitude},{"PULDNDP009",&What_to_display::weather},{"PULDNDP046",&What_to_display::weather_text},
    {"PULDNDP018",&What_to_display::temperature_only},{"PULDNDP047",&What_to_display::wind_barb},{"PULDNDP054",&What_to_display::aloha_circle},
    {"PULDNDP013",&What_to_display::ambiguity},{"PULDNDP008",&What_to_display::phg},{"PULDNDP021",&What_to_display::default_phg},
    {"PULDNDP020",&What_to_display::phg_of_moving},{"PULDNDP023",&What_to_display::df_data},{"PULDNDP123",&What_to_display::df_beamwidth_data},
    {"PULDNDP223",&What_to_display::df_bearing_data},{"PULDNDP035",&What_to_display::dr_data},{"PULDNDP036",&What_to_display::dr_arc},
    {"PULDNDP037",&What_to_display::dr_course},{"PULDNDP038",&What_to_display::dr_symbol},{"PULDNDP005",&What_to_display::dist_bearing},
    {"PULDNDP024",&What_to_display::last_heard}};
  auto display = map_->displayOptions();
  for (const auto &entry : displays) {
    const QString key = QString("station/display/%1").arg(entry.id);
    display.*entry.field = settings.value(key, bool(display.*entry.field)).toBool();
    if (auto *action = window_->findChild<QAction *>(entry.id)) action->setChecked(display.*entry.field);
    bind(entry.id, [this, entry, key](bool checked) {
      auto value = map_->displayOptions(); value.*entry.field = checked; map_->setDisplayOptions(value); QSettings().setValue(key, checked);
    });
  }
  map_->setDisplayOptions(display);
  bind("PULDNDP056", [this] {
    const auto path = QFileDialog::getSaveFileName(window_, tr("Export all stations and trails"), QDir::homePath()+"/stations.kml", tr("KML files (*.kml)"));
    if (!path.isEmpty()) { QString error; if (!exportKml(path,&error)) showError(error); else window_->statusBar()->showMessage(tr("Exported %1").arg(path),5000); }
  });
  if (auto *action = window_->findChild<QAction *>("Store to open databases")) {
    QObject::disconnect(action, nullptr, window_, nullptr); action->setEnabled(false);
    action->setProperty("unavailable", true);
    action->setToolTip(tr("No external database interfaces are configured in this frontend."));
  }
  bind("PULDNDP022", [this] { fetchTrail(); });
  bind("PULDNDP048", [this] {
    QString error; if (!reloadObjects(historyDirectory_+"/object.log",&error)) showError(error);
  });
  bind("PULDNDP025", [this] { clearHistory("object.log",false); });
  bind("PULDNDP050", [this] { clearHistory("tactical_calls.log",true); });
  bind("PULDNDP049", [this] {
    confirm(tr("Clear all tactical calls"),tr("Remove all tactical labels and comment out active assignments in tactical_calls.log?"),[this] {
      const auto path=historyDirectory_+"/tactical_calls.log";
      QFile in(path); QByteArray text;
      if (in.exists()) {
        if (!in.open(QIODevice::ReadOnly) || in.size()>1024*1024) { showError(tr("Cannot read tactical call history.")); return; }
        while (!in.atEnd()) { const auto line=in.readLine(); text += line.startsWith('#') ? line : '#'+line; }
        // Windows cannot replace a file while this read handle is open.
        in.close();
      }
      QSaveFile out(path);
      if (!out.open(QIODevice::WriteOnly) || out.write(text)!=text.size() || !out.commit()) { showError(out.errorString()); return; }
      stations_->clearTactical();
    });
  });
  bind("PULDNDP016", [this] {
    confirm(tr("Clear all trails"),tr("Delete all in-memory station and object trails?"),[this] { stations_->clearTrails(); });
  });
  bind("PULDNDP015", [this] {
    confirm(tr("Clear all stations"),tr("Remove received stations, objects, items and trails from the map? The configured local station will remain."),[this] {
      stations_->clear(); if (local_) stations_->upsert(*local_);
    });
  });
  // Read the original comma-separated tactical history without rewriting it.
  if (QFile::exists(historyDirectory_+"/tactical_calls.log")) {
    QString error; if (!reloadTactical(historyDirectory_+"/tactical_calls.log",&error)) window_->statusBar()->showMessage(error,10000);
  }
}
void StationMenu::showError(const QString &error)
{
  auto *box=new QMessageBox(QMessageBox::Warning,tr("Station"),error,QMessageBox::Ok,window_);
  box->setAttribute(Qt::WA_DeleteOnClose); box->open();
}
void StationMenu::confirm(const QString &title,const QString &text,std::function<void()> action)
{
  auto *box=new QMessageBox(QMessageBox::Question,title,text,QMessageBox::Yes|QMessageBox::No,window_);
  box->setDefaultButton(QMessageBox::No); box->setAttribute(Qt::WA_DeleteOnClose);
  connect(box,&QMessageBox::finished,this,[action](int result) { if(result==QMessageBox::Yes) action(); }); box->open();
}
void StationMenu::clearHistory(const QString &name,bool tactical)
{
  const auto path=historyDirectory_+'/'+name;
  confirm(tr("Clear history"),tr("Empty %1?%2").arg(path,tactical ? tr(" This also removes current tactical labels.") : tr(" Current map objects will remain.")),[this,path,tactical] {
    QSaveFile out(path);
    if(!out.open(QIODevice::WriteOnly) || !out.commit()) {showError(out.errorString());return;}
    if(tactical) stations_->clearTactical();
  });
}
bool StationMenu::reloadTactical(const QString &path,QString *error)
{
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly) || file.size()>1024*1024) {if(error)*error=tr("Cannot read tactical history: %1").arg(file.errorString());return false;}
  QMap<QString,QString> names;
  while(!file.atEnd()) {
    const auto line=QString::fromUtf8(file.readLine()).trimmed();
    if(line.isEmpty() || line.startsWith('#')) continue;
    const int comma=line.indexOf(','); if(comma<1) continue;
    const auto name=line.left(comma).trimmed(), label=line.mid(comma+1).trimmed();
    if(name.size()<=32 && label.size()<=100) names.insert(name,label);
  }
  stations_->setTactical(names);return true;
}
bool StationMenu::reloadObjects(const QString &path,QString *error)
{
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly) || file.size()>4*1024*1024) {if(error)*error=tr("Cannot read object history: %1").arg(file.errorString());return false;}
  QList<AprsUpdate> updates;
  while(!file.atEnd()) {
    auto line=file.readLine(4098); while(line.endsWith('\n') || line.endsWith('\r'))line.chop(1);
    if(line.isEmpty() || line.startsWith('#'))continue;
    if(line.size()>4096) {if(error)*error=tr("Object history contains an oversized record.");return false;}
    auto update=decodeAprs(callsign_.toLatin1()+">APXQT1:"+line);
    if(!update || update->station.kind==StationKind::Station) {if(error)*error=tr("Invalid object/item history record.");return false;}
    update->station.dataVia='L'; updates.append(*update);
  }
  for(const auto &update:updates) {
    if(update.removed)stations_->remove(update.station.callsign,update.station.kind);
    else if(!stations_->upsert(update.station,error))return false;
  }
  window_->statusBar()->showMessage(tr("Reloaded %1 object/item records (display only).").arg(updates.size()),5000);return true;
}
bool StationMenu::exportKml(const QString &path, QString *error) const
{
  QList<KmlStation> records;
  for (const DataRow *row = stations_->database()->first; row; row = row->n_next)
    if ((row->flag & ST_ACTIVE) && row->position_valid) records.append(copyKmlStation(row));
  return writeKmlFile(path, records, error);
}

int StationMenu::importFindu(const QByteArray &data,const QString &callsign,QString *error)
{
  if(data.size()>4*1024*1024) {if(error)*error=tr("Trail response exceeds 4 MiB.");return -1;}
  QString text=QString::fromLatin1(data);
  // findu's legacy raw CGI returns HTML breaks and escapes packet bytes.
  text.replace(QRegularExpression("<br\\s*/?>",QRegularExpression::CaseInsensitiveOption),"   \n");
  text.replace("&gt;",">");text.replace("&lt;","<");text.replace("&quot;","\"");text.replace("&#39;","'");text.replace("&amp;","&");
  QList<Station> points;
  const auto lines=text.split('\n');
  const qint64 now=QDateTime::currentSecsSinceEpoch();
  for(const auto &raw:lines) {
    QString line=raw;
    while(line.startsWith(' ') || line.startsWith('\t'))line.remove(0,1);
    if(line.endsWith('\r'))line.chop(1);
    if(line.startsWith('<') || line.startsWith('#') || line.isEmpty())continue;
    qint64 timestamp=now-std::max<qsizetype>(1,lines.size()-points.size());
    if(line.size()>15 && line[14]==',') {
      auto time=QDateTime::fromString(line.left(14),"yyyyMMddHHmmss");time.setTimeZone(QTimeZone::UTC);
      if(!time.isValid())continue;
      timestamp=time.toSecsSinceEpoch();line.remove(0,15);
    }
    auto update=decodeAprs(line.toLatin1());
    if(!update || update->removed || update->station.kind!=StationKind::Station || update->station.callsign!=callsign.toUpper())continue;
    update->station.heardAt=timestamp;update->station.dataVia='I';points.append(update->station);
    if(points.size()>=5000)break;
  }
  if(points.isEmpty()) {if(error)*error=tr("No valid positions for %1 were found in the response.").arg(callsign);return -1;}
  std::stable_sort(points.begin(),points.end(),[](const Station &a,const Station &b){return a.heardAt<b.heardAt;});
  if(!stations_->importTrail(points,error))return -1;
  return int(points.size());
}
void StationMenu::fetchTrail()
{
  auto *dialog=new QDialog(window_);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(tr("Fetch Findu Trail"));
  auto *layout=new QFormLayout(dialog);
  auto *call=new QLineEdit(dialog);call->setText(callsign_);layout->addRow(tr("Callsign:"),call);
  auto *start=new QSpinBox(dialog);start->setRange(1,240);start->setValue(24);layout->addRow(tr("Start (hours ago):"),start);
  auto *length=new QSpinBox(dialog);length->setRange(1,24);length->setValue(24);layout->addRow(tr("Duration (hours):"),length);
  connect(start,&QSpinBox::valueChanged,length,[length](int hours){length->setMaximum(hours);});
  auto *status=new QLabel(tr("Downloaded history updates trails without transmitting or replacing newer live positions."),dialog);status->setWordWrap(true);layout->addRow(status);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);layout->addRow(buttons);
  connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,call,start,length,status,buttons] {
    const QString name=call->text().trimmed().toUpper();
    if(!QRegularExpression("^[A-Z0-9]{1,6}(-([0-9]|1[0-5]))?$").match(name).hasMatch()) {status->setText(tr("Enter a valid AX.25 callsign."));return;}
    QUrl url("http://www.findu.com/cgi-bin/raw.cgi");QUrlQuery query;
    query.addQueryItem("call",name);query.addQueryItem("start",QString::number(start->value()));
    query.addQueryItem("length",QString::number(length->value()));query.addQueryItem("time","1");url.setQuery(query);
    QNetworkRequest request(url);request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::UserAgentHeader,"Xastir-Qt/0.1");
    auto *reply=network_.get(request);reply->setReadBufferSize(4*1024*1024+1);
    auto data=std::make_shared<QByteArray>();
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);status->setText(tr("Downloading…"));
    connect(dialog,&QObject::destroyed,reply,[reply]{reply->abort();});
    connect(reply,&QIODevice::readyRead,reply,[reply,data]{*data+=reply->readAll();if(data->size()>4*1024*1024)reply->abort();});
    connect(reply,&QNetworkReply::finished,dialog,[this,reply,data,name,status,buttons] {
      *data+=reply->readAll();QString error;
      if(reply->error()!=QNetworkReply::NoError)error=reply->errorString();
      else {const int count=importFindu(*data,name,&error);if(count>=0)status->setText(tr("Imported %1 positions for %2.").arg(count).arg(name));}
      if(!error.isEmpty())status->setText(tr("Download failed: %1").arg(error));
      buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    });
    connect(reply,&QNetworkReply::finished,reply,&QObject::deleteLater);
  });
  dialog->open();
}
