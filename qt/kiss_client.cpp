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
bool KissClient::parseEntrySequence(const QString &text, QByteArray *bytes, QString *error)
{
  auto reject = [error] { if (error) *error = tr("KISS entry must contain 1–256 bytes. Use printable ASCII and escapes: \\r, \\n, \\t, \\\\ or \\xHH."); return false; };
  if (text.isEmpty() || text.size() > 1024) return reject();
  QByteArray result;
  for (qsizetype i = 0; i < text.size(); ++i) {
    ushort c = text[i].unicode();
    if (c == '\\') {
      if (++i == text.size()) return reject();
      const auto escape = text[i].unicode();
      if (escape == 'r') c = '\r';
      else if (escape == 'n') c = '\n';
      else if (escape == 't') c = '\t';
      else if (escape == '\\') c = '\\';
      else if (escape == 'x') {
        if (i + 2 >= text.size()) return reject();
        auto hex = [](ushort digit) -> int {
          if (digit >= '0' && digit <= '9') return digit - '0';
          if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
          if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
          return -1;
        };
        const int high = hex(text[i+1].unicode()), low = hex(text[i+2].unicode());
        if (high < 0 || low < 0) return reject();
        c = ushort(high * 16 + low); i += 2;
      } else return reject();
    } else if (c < 32 || c > 126) return reject();
    result += char(c);
    if (result.size() > 256) return reject();
  }
  if (bytes) *bytes = result;
  return true;
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
  if (s.kissEntryDelayMs < 0 || s.kissEntryDelayMs > 10000)
    return reject(tr("KISS entry wait must be between 0 and 10,000 ms."));
  if (s.initializeKiss && !parseEntrySequence(s.kissEntrySequence, nullptr, error)) return false;
  return true;
}
KissClient::KissClient(KissSettings settings, StationModel *stations, QObject *parent)
  : RadioInterface(parent), settings_(std::move(settings)), stations_(stations)
{
  serial_.setReadBufferSize(64 * 1024); retry_.setSingleShot(true); reader_.setSingleShot(true); writer_.setSingleShot(true);
  entryDelay_.setSingleShot(true);
  entryDelay_.setTimerType(Qt::PreciseTimer);
  connect(&entryDelay_, &QTimer::timeout, this, &KissClient::finishInitialization);
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
  if (portOpen()) return;
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
  if (settings_.initializeKiss) {
    parseEntrySequence(settings_.kissEntrySequence, &entryBytes_);
    entryOffset_ = 0; phase_ = Phase::EnteringKiss;
    emit statusChanged(tr("KISS %1 — sending mode-entry sequence").arg(settings_.portName));
    writer_.start(0); return;
  }
  phase_ = Phase::Ready;
  emit statusChanged(tr("KISS %1 — port open, awaiting frames").arg(settings_.portName));
}
void KissClient::finishInitialization()
{
  if (!running_ || !portOpen() || phase_ != Phase::WaitingForKiss) return;
  // Terminal banners and prompts are not APRS packets. Start fresh after the
  // entry sequence has drained and the configured settling interval elapsed.
  bool cleared;
  { QSignalBlocker blocker(&serial_); cleared = serial_.clear(QSerialPort::Input); }
  if (!cleared) { fail(serial_.errorString()); return; }
  xastir_kiss_init(&decoder_); entryBytes_.clear(); entryOffset_ = 0; phase_ = Phase::Ready;
  emit statusChanged(tr("KISS %1 — mode-entry wait complete, awaiting frames (mode unconfirmed)").arg(settings_.portName));
}
void KissClient::discardOutput()
{
  writer_.stop(); output_.clear(); queuedBytes_ = 0;
  if (serial_.isOpen()) { QSignalBlocker blocker(&serial_); serial_.clear(QSerialPort::Output); }
}
void KissClient::stop()
{
  running_ = false; retry_.stop(); reader_.stop(); entryDelay_.stop();
  phase_ = Phase::Closed; entryBytes_.clear(); entryOffset_ = 0; discardOutput();
  { QSignalBlocker blocker(&serial_); serial_.close(); }
  xastir_kiss_init(&decoder_); emit statusChanged(tr("Serial KISS stopped"));
}
void KissClient::fail(const QString &error)
{
  reader_.stop(); entryDelay_.stop();
  phase_ = Phase::Closed; entryBytes_.clear(); entryOffset_ = 0; discardOutput();
  { QSignalBlocker blocker(&serial_); serial_.close(); }
  xastir_kiss_init(&decoder_);
  emit statusChanged(tr("KISS %1: %2%3").arg(settings_.portName, error, settings_.reconnect && running_ ? tr(" — retrying") : QString()));
  if (running_ && settings_.reconnect) { retry_.start(retryDelay_); retryDelay_ = qMin(30000, retryDelay_ * 2); }
  else running_ = false;
}
qsizetype KissClient::pendingBytes() const { return queuedBytes_ + entryBytes_.size() - entryOffset_ + serial_.bytesToWrite(); }
void KissClient::setTransmitBlocked(bool blocked)
{
  RadioInterface::setTransmitBlocked(blocked);
  // Mode entry changes the host/TNC protocol and is permitted for RX-only
  // operation, like explicit KISS parameters. No RF data is queued during it.
  if (blocked && !initializing()) discardOutput();
}
bool KissClient::transmit(const QByteArray &information, const QString &path, QString *error)
{
  if (initializing()) { if (error) *error = tr("Entering KISS mode; wait for initialization to finish."); return false; }
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
  if (!connected()) { if (error) *error = initializing() ? tr("Entering KISS mode; wait before applying TNC parameters.") : tr("Serial KISS port is not open."); return false; }
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
  if (portOpen() && phase_ == Phase::EnteringKiss) {
    if (entryOffset_ < entryBytes_.size()) {
      qint64 written;
      {
        QSignalBlocker blocker(&serial_);
        written = serial_.write(entryBytes_.constData() + entryOffset_, entryBytes_.size() - entryOffset_);
      }
      if (written < 0) { fail(serial_.errorString()); return; }
      entryOffset_ += written;
      if (entryOffset_ < entryBytes_.size()) { writer_.start(10); return; }
    }
    if (!serial_.bytesToWrite()) {
      phase_ = Phase::WaitingForKiss;
      // bytesWritten means driver admission. Allow the 8N1 wire time too,
      // so slow baud rates still get the full configured settling interval.
      const int wireMs = int((entryBytes_.size() * 10000 + settings_.baudRate - 1) / settings_.baudRate);
      entryDelay_.start(settings_.kissEntryDelayMs + wireMs);
      emit statusChanged(tr("KISS %1 — waiting for mode entry").arg(settings_.portName));
    }
    return;
  }
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
  if (!running_ || !portOpen() || processing_) return;
  processing_ = true; int bytes = 0, frames = 0;
  while (serial_.bytesAvailable() && bytes++ < 16384 && frames < 64) {
    char byte; qint64 count;
    { QSignalBlocker blocker(&serial_); count = serial_.read(&byte, 1); }
    if (count < 0) { fail(serial_.errorString()); break; }
    if (!count) break;
    if (initializing()) continue;
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
  if (portOpen() && serial_.bytesAvailable()) reader_.start(0);
}
