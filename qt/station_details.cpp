// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_details.h"
#include "station_model.h"
#include "map_widget.h"
#include "station_navigator.h"
#include "message_ui.h"
#include <QDialog>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
StationDetails::StationDetails(QMainWindow *w, StationModel *s, MapWidget *m, StationNavigator *n, MessageUi *messages, QString own)
 : QObject(w), window_(w), stations_(s), map_(m), navigator_(n), messages_(messages), own_(own) {
  connect(m, &MapWidget::stationClicked, this, &StationDetails::open);
  connect(s, &StationModel::stationsChanged, this, &StationDetails::refresh);
}
void StationDetails::open(const QString &name) {
  if (!stations_->record(name)) return;
  name_ = name;
  if (!dialog_) {
    dialog_ = new QDialog(window_); dialog_->setObjectName("stationDetails"); dialog_->resize(540,400);
    auto *layout = new QVBoxLayout(dialog_);
    text_ = new QLabel(dialog_); text_->setObjectName("stationDetailsText"); text_->setTextFormat(Qt::PlainText);
    text_->setWordWrap(true); text_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(text_);
    auto *buttons = new QHBoxLayout; layout->addLayout(buttons);
    auto add = [&](const QString &label, auto callback) { auto *b=new QPushButton(label,dialog_); buttons->addWidget(b); connect(b,&QPushButton::clicked,this,callback); };
    add(tr("Send Message"),[this] { const auto *r=stations_->record(name_); if(r) messages_->openConversation((r->flag & (ST_OBJECT|ST_ITEM)) ? QString::fromLatin1(r->origin) : name_); });
    add(tr("Track"),[this] { navigator_->locate(name_,true); });
    add(tr("Show Trail"),[this] { auto d=map_->displayOptions(); d.trail=1; map_->setDisplayOptions(d); navigator_->locate(name_,false); });
    add(tr("Close"),[this] { dialog_->close(); });
  }
  refresh(); dialog_->show(); dialog_->raise(); dialog_->activateWindow();
}
void StationDetails::refresh() {
  if (!dialog_) return;
  dialog_->setWindowTitle(tr("Details — %1").arg(name_));
  const auto *r=stations_->record(name_);
  if(!r) { text_->setText(tr("This station or object is no longer in the database.")); return; }
  const auto station=stations_->stations().value(name_);
  QStringList lines{tr("%1: %2").arg((r->flag&ST_OBJECT)?tr("Object"):(r->flag&ST_ITEM)?tr("Item"):tr("Station"),name_)};
  if(r->origin[0]) lines << tr("Owner: %1").arg(QString::fromLatin1(r->origin));
  lines << tr("Last heard: %1").arg(QDateTime::fromSecsSinceEpoch(r->sec_heard).toString(Qt::ISODate));
  lines << tr("Position: %1°, %2°").arg(station.position.latitude,0,'f',5).arg(station.position.longitude,0,'f',5);
  if(stations_->stations().contains(own_)) { const auto local=stations_->stations().value(own_); double bearing=0; const double distance=xastir_distance(local.position.latitude,local.position.longitude,station.position.latitude,station.position.longitude,&bearing); lines << tr("Distance / bearing: %1 km / %2°").arg(distance/1000,0,'f',2).arg(bearing,0,'f',0); }
  if(r->course[0]) lines << tr("Course: %1°").arg(QString::fromLatin1(r->course));
  if(r->speed[0]) lines << tr("Speed: %1 knots").arg(QString::fromLatin1(r->speed));
  if(r->altitude[0]) lines << tr("Altitude: %1 m").arg(QString::fromLatin1(r->altitude));
  if(r->node_path_ptr) lines << tr("Path: %1").arg(QString::fromLatin1(r->node_path_ptr));
  for(auto *c=r->comment_data;c;c=c->next) if(c->text_ptr) lines << tr("Comment: %1").arg(QString::fromUtf8(c->text_ptr));
  if(r->weather_data) { const auto *wx=r->weather_data; lines << tr("Weather: %1°F; wind %2 mph from %3°; gust %4 mph; humidity %5%; pressure %6 hPa").arg(wx->wx_temp,wx->wx_speed,wx->wx_course,wx->wx_gust,wx->wx_hum,wx->wx_baro); }
  text_->setText(lines.join('\n'));
}
