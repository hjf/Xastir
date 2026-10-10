// SPDX-License-Identifier: GPL-2.0-or-later
#include "aprsis_client.h"
#include "aprs_decoder.h"
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <algorithm>
bool AprsisClient::validate(const AprsisSettings &s, const QString &call, QString *error)
{
  auto bad = [error](const QString &text)
  {
    if (error)
      *error = text;
    return false;
  };
  if (s.host.trimmed().isEmpty() || s.host.contains('\r') || s.host.contains('\n') || s.port == 0)
    return bad(tr("Enter an APRS-IS host and valid TCP port."));
  if (call.size() > 9 ||
      !QRegularExpression("^[A-Z0-9]{3,9}(-[A-Z0-9]{1,2})?$").match(call).hasMatch())
    return bad(tr("APRS-IS requires an ASCII callsign of up to 9 characters."));
  if (s.filter.isEmpty() || s.filter.size() > 400)
    return bad(tr("Enter a server-side APRS-IS filter."));
  for (const auto c : s.filter)
    if (c.unicode() < 32 || c.unicode() > 126)
      return bad(tr("The APRS-IS filter must contain printable ASCII characters."));
  return true;
}
AprsisClient::AprsisClient(AprsisSettings settings, QString call, StationModel *stations,
                           QObject *parent)
    : QObject(parent), settings_(std::move(settings)), callsign_(call.trimmed().toUpper()),
      stations_(stations)
{
  socket_.setProxy(QNetworkProxy::NoProxy);
  retry_.setSingleShot(true);
  timeout_.setSingleShot(true);
  connect(&retry_, &QTimer::timeout, this, &AprsisClient::connectToServer);
  connect(&timeout_, &QTimer::timeout, this, [this] { fail(tr("Connection or feed timed out")); });
  connect(&socket_, &QTcpSocket::connected, this,
          [this]
          {
            socket_.setSocketOption(QAbstractSocket::KeepAliveOption, 1);
            timeout_.start(10000);
            emit statusChanged(tr("APRS-IS connected; awaiting server greeting"));
          });
  connect(&socket_, &QTcpSocket::readyRead, this, &AprsisClient::receive);
  connect(&socket_, &QTcpSocket::errorOccurred, this, [this] { fail(socket_.errorString()); });
  connect(&socket_, &QTcpSocket::disconnected, this,
          [this]
          {
            if (running_)
              fail(tr("Disconnected"));
          });
}
AprsisClient::~AprsisClient()
{
  socket_.disconnect(this);
  socket_.abort();
}
void AprsisClient::start()
{
  QString error;
  if (!validate(settings_, callsign_, &error))
  {
    emit statusChanged(error);
    return;
  }
  if (running_)
    return;
  running_ = true;
  retryDelay_ = 2000;
  connectToServer();
}
void AprsisClient::stop()
{
  running_ = false;
  retry_.stop();
  timeout_.stop();
  input_.clear();
  loggedIn_ = false;
  {
    QSignalBlocker blocker(socket_);
    socket_.abort();
  }
  emit statusChanged(tr("APRS-IS stopped"));
}
void AprsisClient::connectToServer()
{
  if (!running_)
    return;
  input_.clear();
  loggedIn_ = false;
  timeout_.start(10000);
  emit statusChanged(tr("APRS-IS %1:%2 — connecting").arg(settings_.host).arg(settings_.port));
  socket_.connectToHost(settings_.host, settings_.port);
}
void AprsisClient::fail(const QString &reason)
{
  timeout_.stop();
  input_.clear();
  loggedIn_ = false;
  {
    QSignalBlocker blocker(socket_);
    socket_.abort();
  }
  if (!running_)
    return;
  emit statusChanged(tr("APRS-IS: %1").arg(reason));
  if (settings_.reconnect)
  {
    retry_.start(retryDelay_);
    retryDelay_ = std::min(30000, retryDelay_ * 2);
  }
}
void AprsisClient::receive()
{
  if (processing_ || !running_)
    return;
  if (input_.size() + socket_.bytesAvailable() > 1024 * 1024)
  {
    fail(tr("Receive buffer exceeded limit"));
    return;
  }
  input_ += socket_.readAll();
  processing_ = true;
  int offset = 0, count = 0;
  while (count++ < 128)
  {
    const int end = input_.indexOf('\n', offset);
    if (end < 0)
      break;
    QByteArray line = input_.mid(offset, end - offset);
    offset = end + 1;
    if (line.endsWith('\r'))
      line.chop(1);
    if (line.size() > 510)
    {
      processing_ = false;
      fail(tr("APRS-IS line exceeds 512 bytes"));
      return;
    }
    timeout_.start(120000);
    if (line.startsWith('#'))
    {
      if (!loggedIn_)
      {
        const QByteArray login = "user " + callsign_.toLatin1() +
                                 " pass -1 vers XastirQt 0.1 filter " +
                                 settings_.filter.toLatin1() + "\r\n";
        socket_.write(login);
        loggedIn_ = true;
        retryDelay_ = 2000;
        emit statusChanged(tr("APRS-IS — receiving only"));
      }
      continue;
    }
    if (line.isEmpty() || !loggedIn_)
      continue;
    emit packetReceived(line);
    if(stations_) stations_->applyPacket(line,'I');
  }
  input_.remove(0, offset);
  processing_ = false;
  if (!input_.contains('\n') &&
      (input_.size() > 511 || (input_.size() == 511 && !input_.endsWith('\r'))))
  {
    fail(tr("APRS-IS line exceeds 512 bytes"));
    return;
  }
  if (input_.contains('\n'))
    QTimer::singleShot(0, this, &AprsisClient::receive);
}
