// SPDX-License-Identifier: GPL-2.0-or-later
#include "kiss_client.h"
#include "ax25_core.h"
#include "message_core.h"
#include <QSignalBlocker>

namespace {
constexpr qsizetype QueueLimit = 64 * 1024, DriverLimit = 16 * 1024;
QByteArray encode(unsigned port, unsigned command, const QByteArray &data)
{
  QByteArray bytes(XASTIR_KISS_MAX_ENCODED, Qt::Uninitialized);
  const auto size = xastir_kiss_encode(port, command, reinterpret_cast<const unsigned char *>(data.constData()),
    size_t(data.size()), reinterpret_cast<unsigned char *>(bytes.data()), size_t(bytes.size()));
  bytes.resize(qsizetype(size)); return bytes;
}
}
bool KissClient::validate(const KissSettings &s, QString *error)
{
  auto reject = [error](const QString &text) { if (error) *error = text; return false; };
  if (s.deviceIndex < 0 || s.deviceIndex > 255 || s.radioPort > 15)
    return reject(tr("KISS device must be 0–255 and channel 0–15."));
  if (s.portName.trimmed().isEmpty() || s.portName != s.portName.trimmed() || s.portName.contains('\0') || s.portName.contains('\n') || s.portName.contains('\r'))
    return reject(tr("Enter a serial port name or device path."));
  if (s.baudRate < 300 || s.baudRate > 4000000) return reject(tr("Enter the serial baud rate, between 300 and 4,000,000."));
  const auto path = s.path.toLatin1();
  if (QString::fromLatin1(path) != s.path || path.contains('\0') || !xastir_message_valid_path(path.constData()))
    return reject(tr("Invalid RF path for serial KISS."));
  for (int value : {s.txDelay, s.persistence, s.slotTime, s.txTail})
    if (value < 0 || value > 255) return reject(tr("KISS parameters must be between 0 and 255."));
  return true;
}
KissClient::KissClient(KissSettings settings, StationModel *stations, QObject *parent)
  : RadioInterface(parent), settings_(std::move(settings)), stations_(stations)
{
  serial_.setReadBufferSize(64 * 1024); retry_.setSingleShot(true); reader_.setSingleShot(true); writer_.setSingleShot(true);
  xastir_kiss_init(&decoder_);
  connect(&retry_, &QTimer::timeout, this, &KissClient::open);
  connect(&reader_, &QTimer::timeout, this, &KissClient::receive);
  connect(&writer_, &QTimer::timeout, this, &KissClient::pump);
  connect(&serial_, &QSerialPort::readyRead, this, &KissClient::receive);
  connect(&serial_, &QSerialPort::bytesWritten, this, [this] { if (!writer_.isActive()) writer_.start(0); });
  connect(&serial_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
    if (running_ && error != QSerialPort::NoError) fail(serial_.errorString());
  });
}
KissClient::~KissClient() { serial_.disconnect(this); stop(); }
QString KissClient::description() const
{
  return tr("Device %1 — KISS %2, %3 baud, channel %4%5").arg(settings_.deviceIndex).arg(settings_.portName)
    .arg(settings_.baudRate).arg(settings_.radioPort).arg(settings_.transmitEnabled ? QString() : tr(" (receive only)"));
}
void KissClient::start()
{
  if (connected()) return;
  retry_.stop();
  running_ = true; retryDelay_ = 2000; open();
}
void KissClient::open()
{
  if (!running_) return;
  QString error;
  if (!validate(settings_, &error)) { running_ = false; emit statusChanged(error); return; }
  xastir_kiss_init(&decoder_); bool opened;
  {
    QSignalBlocker blocker(&serial_);
    serial_.setPortName(settings_.portName);
    opened = serial_.setBaudRate(settings_.baudRate) && serial_.setDataBits(QSerialPort::Data8)
      && serial_.setParity(QSerialPort::NoParity) && serial_.setStopBits(QSerialPort::OneStop)
      && serial_.setFlowControl(QSerialPort::NoFlowControl) && serial_.open(QIODevice::ReadWrite);
  }
  if (!opened) { fail(serial_.errorString()); return; }
  retryDelay_ = 2000;
  emit statusChanged(tr("KISS %1 — port open, awaiting frames").arg(settings_.portName));
}
void KissClient::discardOutput()
{
  writer_.stop(); output_.clear(); queuedBytes_ = 0;
  if (serial_.isOpen()) { QSignalBlocker blocker(&serial_); serial_.clear(QSerialPort::Output); }
}
void KissClient::stop()
{
  running_ = false; retry_.stop(); reader_.stop(); discardOutput();
  { QSignalBlocker blocker(&serial_); serial_.close(); }
  xastir_kiss_init(&decoder_); emit statusChanged(tr("Serial KISS stopped"));
}
void KissClient::fail(const QString &error)
{
  reader_.stop(); discardOutput();
  { QSignalBlocker blocker(&serial_); serial_.close(); }
  xastir_kiss_init(&decoder_);
  emit statusChanged(tr("KISS %1: %2%3").arg(settings_.portName, error, settings_.reconnect && running_ ? tr(" — retrying") : QString()));
  if (running_ && settings_.reconnect) { retry_.start(retryDelay_); retryDelay_ = qMin(30000, retryDelay_ * 2); }
  else running_ = false;
}
qsizetype KissClient::pendingBytes() const { return queuedBytes_ + serial_.bytesToWrite(); }
void KissClient::setTransmitBlocked(bool blocked)
{
  RadioInterface::setTransmitBlocked(blocked); if (blocked) discardOutput();
}
bool KissClient::transmit(const QByteArray &information, const QString &path, QString *error)
{
  const auto frame = prepareFrame(information, path.isNull() ? settings_.path : path, error);
  if (frame.isEmpty()) return false;
  auto bytes = encode(settings_.radioPort, 0, frame);
  if (output_.size() >= 128 || pendingBytes() + bytes.size() > QueueLimit) {
    if (error) *error = tr("Serial KISS transmit queue is full.");
    return false;
  }
  queuedBytes_ += bytes.size(); output_.append({std::move(bytes), 0}); writer_.start(0);
  // Admission to the bounded host queue. KISS has no frame acknowledgement.
  emit packetTransmitted(tnc2(frame)); return true;
}
bool KissClient::applyParameters(const KissSettings &requested, QString *error)
{
  if (!validate(requested, error)) return false;
  if (requested.portName != settings_.portName || requested.baudRate != settings_.baudRate || requested.radioPort != settings_.radioPort) {
    if (error) *error = tr("Save connection settings before applying parameters to another port, baud rate or channel.");
    return false;
  }
  if (!connected()) { if (error) *error = tr("Serial KISS port is not open."); return false; }
  QList<Output> commands; qsizetype size = 0;
  const int values[] = {requested.txDelay, requested.persistence, requested.slotTime, requested.txTail, requested.fullDuplex};
  for (unsigned command = 1; command <= 5; ++command) {
    auto bytes = encode(settings_.radioPort, command, QByteArray(1, char(values[command-1])));
    size += bytes.size(); commands.append({std::move(bytes), 0});
  }
  if (output_.size() + commands.size() > 128 || pendingBytes() + size > QueueLimit) {
    if (error) *error = tr("Serial KISS transmit queue is full.");
    return false;
  }
  output_.append(commands); queuedBytes_ += size; writer_.start(0); return true;
}
void KissClient::pump()
{
  if (!connected() || output_.isEmpty()) return;
  qsizetype budget = qMin<qsizetype>(DriverLimit - serial_.bytesToWrite(), DriverLimit);
  while (budget > 0 && !output_.isEmpty()) {
    auto &frame = output_.first();
    // Handle synchronous write errors here so the error signal cannot clear
    // output_ while this reference is in use.
    qint64 written;
    {
      QSignalBlocker blocker(&serial_);
      written = serial_.write(frame.bytes.constData() + frame.offset, qMin(budget, frame.bytes.size() - frame.offset));
    }
    if (written < 0) { fail(serial_.errorString()); return; }
    if (!written) { writer_.start(10); return; }
    frame.offset += written; queuedBytes_ -= written; budget -= written;
    if (frame.offset == frame.bytes.size()) output_.removeFirst();
  }
}
void KissClient::receive()
{
  if (!running_ || !connected() || processing_) return;
  processing_ = true; int bytes = 0, frames = 0;
  while (serial_.bytesAvailable() && bytes++ < 16384 && frames < 64) {
    char byte; qint64 count;
    { QSignalBlocker blocker(&serial_); count = serial_.read(&byte, 1); }
    if (count < 0) { fail(serial_.errorString()); break; }
    if (!count) break;
    XastirKissFrame frame; const int result = xastir_kiss_decode_byte(&decoder_, (unsigned char)byte, &frame);
    if (result < 0) { ++rejected_; continue; }
    if (!result) continue;
    ++frames;
    if (frame.command != 0 || frame.port != settings_.radioPort) continue;
    QByteArray packet(XASTIR_AX25_MAX_FRAME + 128, Qt::Uninitialized);
    auto size = xastir_ax25_to_tnc2(frame.data, frame.length, reinterpret_cast<unsigned char *>(packet.data()), size_t(packet.size()));
    if (!size) { ++rejected_; continue; }
    packet.resize(qsizetype(size)); ++packets_; emit packetReceived(packet);
    if (stations_) stations_->applyPacket(packet, 'T');
  }
  processing_ = false;
  if (bytes > 0 && connected()) emit statusChanged(tr("KISS %1 — receiving (%2 packets, %3 rejected frames)").arg(settings_.portName).arg(packets_).arg(rejected_));
  if (connected() && serial_.bytesAvailable()) reader_.start(0);
}
