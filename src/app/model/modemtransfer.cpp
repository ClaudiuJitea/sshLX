#include "modemtransfer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <array>
#include <cstring>

// Xmodem and Ymodem control characters.
namespace {
constexpr uchar SOH = 0x01, STX = 0x02, EOT = 0x04, ACK = 0x06, NAK = 0x15, CAN = 0x18,
                CPMEOF = 0x1a;
constexpr int kMaxRetries = 10;

// Zmodem.
constexpr uchar ZPAD = '*', ZDLE = 0x18, ZBIN = 'A', ZHEX = 'B', ZBIN32 = 'C';
constexpr uchar ZCRCE = 'h', ZCRCG = 'i', ZCRCQ = 'j', ZCRCW = 'k', ZRUB0 = 'l', ZRUB1 = 'm';
enum {
    ZRQINIT = 0, ZRINIT = 1, ZSINIT = 2, ZACK = 3, ZFILE = 4, ZSKIP = 5, ZNAK = 6, ZABORT = 7,
    ZFIN = 8, ZRPOS = 9, ZDATA = 10, ZEOF = 11, ZFERR = 12, ZCRC = 13, ZCHALLENGE = 14,
    ZCOMPL = 15, ZCAN = 16, ZFREECNT = 17, ZCOMMAND = 18, ZSTDERR = 19
};
constexpr uchar CANFDX = 1, CANOVIO = 2, CANFC32 = 32, ESCCTL = 64;
constexpr uchar ZCBIN = 1, ZCRESUM = 3;
constexpr int kZBlock = 1024;
constexpr size_t kBacklogLimit = 16 * 1024;

quint16 crc16(const char *data, int len, quint16 crc = 0)
{
    for (int i = 0; i < len; i++) {
        crc ^= quint16(uchar(data[i])) << 8;
        for (int b = 0; b < 8; b++)
            crc = crc & 0x8000 ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

quint32 crc32Update(quint32 crc, const char *data, int len)
{
    static const auto table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; i++) {
            quint32 c = i;
            for (int b = 0; b < 8; b++)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    for (int i = 0; i < len; i++)
        crc = table[(crc ^ uchar(data[i])) & 0xff] ^ (crc >> 8);
    return crc;
}

quint32 crc32(const QByteArray &data)
{
    return ~crc32Update(0xffffffffu, data.constData(), int(data.size()));
}

int hexValue(uchar c)
{
    c &= 0x7f;
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

void putPosition(uchar d[4], quint32 pos)
{
    d[0] = uchar(pos);
    d[1] = uchar(pos >> 8);
    d[2] = uchar(pos >> 16);
    d[3] = uchar(pos >> 24);
}
} // namespace

quint32 ModemTransfer::ZHeader::position() const
{
    return quint32(d[0]) | quint32(d[1]) << 8 | quint32(d[2]) << 16 | quint32(d[3]) << 24;
}

ModemTransfer::ModemTransfer(QObject *parent) : QObject(parent)
{
    m_processTimer.setSingleShot(true);
    m_processTimer.setInterval(0);
    connect(&m_processTimer, &QTimer::timeout, this, &ModemTransfer::process);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &ModemTransfer::onTimeout);
    m_pumpTimer.setSingleShot(true);
    connect(&m_pumpTimer, &QTimer::timeout, this, [this] {
        if (!m_running)
            return;
        if (m_phase == Phase::ZSendData)
            zPumpData();
        else if (m_phase == Phase::XSendWaitAck && m_lastPacket.isEmpty())
            xSendBlock();
    });
}

ModemTransfer::~ModemTransfer()
{
    discardReceiveFile();
}

QString ModemTransfer::protocolName(Protocol p)
{
    switch (p) {
    case Protocol::Xmodem: return QStringLiteral("Xmodem");
    case Protocol::Xmodem1k: return QStringLiteral("Xmodem-1K");
    case Protocol::Ymodem: return QStringLiteral("Ymodem");
    case Protocol::Zmodem: return QStringLiteral("Zmodem");
    case Protocol::Kermit: return QStringLiteral("Kermit");
    }
    return {};
}

QString ModemTransfer::safeFileName(const QString &remoteName)
{
    QString n = remoteName;
    n.replace('\\', '/');
    n = n.section('/', -1);
    n.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
    n = n.trimmed();
    if (n == "." || n == "..")
        return {};
    // A leading dot would hide the file, or replace something like .bashrc.
    while (n.startsWith('.'))
        n.remove(0, 1);
    return n;
}

QString ModemTransfer::uniquePath(const QString &directory, const QString &name)
{
    QDir dir(directory);
    QString path = dir.filePath(name);
    QFileInfo fi(name);
    const QString base = fi.completeBaseName(), ext = fi.suffix();
    for (int i = 1; QFileInfo::exists(path) || QFileInfo(path).isSymLink(); i++)
        path = dir.filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(i)
                                .arg(ext.isEmpty() ? QString() : '.' + ext));
    return path;
}

int ModemTransfer::findZmodemStart(const QByteArray &data, bool *receiving)
{
    // ZRQINIT from sz, ZRINIT from rz: both in hex, the type as two digits.
    static const QByteArray tag("**\x18" "B0", 5);
    int from = 0;
    while (true) {
        const int i = int(data.indexOf(tag, from));
        if (i < 0 || i + 6 > data.size())
            return -1;
        const char type = data[i + 5];
        if (type == '0' || type == '1') {
            if (receiving)
                *receiving = type == '0';
            return i;
        }
        from = i + 1;
    }
}

void ModemTransfer::startSend(Protocol p, const QStringList &files, bool resume)
{
    if (m_running || files.isEmpty())
        return;
    m_protocol = p;
    m_sending = true;
    m_running = true;
    m_zResume = resume;
    m_files = files;
    m_totalFiles = files.size();
    m_filesDone = 0;
    m_saved.clear();
    m_in.clear();
    m_retries = 0;
    m_cans = 0;
    m_zp = ZParse::Hunt;
    if (p == Protocol::Zmodem) {
        zStartSend();
        return;
    }
    if (p == Protocol::Kermit) {
        kStart();
        return;
    }
    if (p != Protocol::Ymodem && files.size() > 1) {
        fail(tr("Xmodem sends one file at a time."));
        return;
    }
    if (p != Protocol::Ymodem && !openNextSendFile())
        return;
    m_phase = Phase::XSendWaitStart;
    armTimeout();
}

void ModemTransfer::startReceive(Protocol p, const QString &directory, const QString &saveAs)
{
    if (m_running)
        return;
    m_protocol = p;
    m_sending = false;
    m_running = true;
    m_directory = directory;
    m_saveAs = saveAs;
    m_saved.clear();
    m_in.clear();
    m_retries = 0;
    m_cans = 0;
    m_filesDone = 0;
    m_zp = ZParse::Hunt;
    if (p == Protocol::Zmodem) {
        zStartReceive();
        return;
    }
    if (p == Protocol::Kermit) {
        m_position = 0;
        m_fileSize = -1;
        kStart();
        return;
    }
    m_crc = true;
    m_block = p == Protocol::Ymodem ? 0 : 1;
    m_headerExpected = p == Protocol::Ymodem;
    m_eotSeen = false;
    m_fileSize = -1;
    m_position = 0;
    if (!m_headerExpected && !openReceiveFile(QFileInfo(saveAs).fileName(), -1))
        return;
    m_phase = Phase::XRecvBlock;
    xRecvPoke();
}

void ModemTransfer::cancel()
{
    if (!m_running)
        return;
    // Eight CANs stop every implementation; the backspaces wipe what is left
    // from a command line, should the far end have gone back to one.
    out(QByteArray(8, char(CAN)) + QByteArray(8, '\b'));
    fail(tr("Cancelled."));
}

void ModemTransfer::connectionLost()
{
    if (m_running)
        fail(tr("The connection was lost."));
}

void ModemTransfer::feed(const char *data, size_t len)
{
    if (!m_running)
        return;
    m_in.append(data, qsizetype(len));
    if (!m_processTimer.isActive())
        m_processTimer.start();
}

void ModemTransfer::out(const QByteArray &data)
{
    emit send(data);
}

void ModemTransfer::armTimeout()
{
    m_timeout.start(m_timeoutMs);
}

void ModemTransfer::stopAll()
{
    m_running = false;
    m_phase = Phase::Idle;
    m_timeout.stop();
    m_pumpTimer.stop();
    m_processTimer.stop();
    m_in.clear();
}

void ModemTransfer::fail(const QString &message)
{
    const bool was = m_running;
    stopAll();
    discardReceiveFile();
    m_file.reset();
    if (was)
        emit finished(false, message);
}

void ModemTransfer::succeed(const QString &message)
{
    const bool was = m_running;
    stopAll();
    m_file.reset();
    if (was)
        emit finished(true, message);
}

void ModemTransfer::process()
{
    if (!m_running)
        return;
    if (m_protocol == Protocol::Zmodem)
        zProcess();
    else if (m_protocol == Protocol::Kermit)
        kProcess();
    else
        xProcess();
}

void ModemTransfer::reportProgress()
{
    emit progress(m_fileName, m_position, m_fileSize);
}

bool ModemTransfer::openNextSendFile()
{
    if (m_files.isEmpty())
        return false;
    const QString path = m_files.takeFirst();
    m_file = std::make_unique<QFile>(path);
    if (!m_file->open(QIODevice::ReadOnly)) {
        fail(tr("Could not open %1: %2").arg(path, m_file->errorString()));
        return false;
    }
    m_fileName = QFileInfo(path).fileName();
    m_fileSize = m_file->size();
    m_position = 0;
    reportProgress();
    return true;
}

bool ModemTransfer::openReceiveFile(const QString &name, qint64 size, qint64 resumeAt)
{
    const QString safe = safeFileName(name);
    if (safe.isEmpty()) {
        fail(tr("The sender gave an unusable file name."));
        return false;
    }
    m_fileName = safe;
    m_finalPath = uniquePath(m_directory, safe);
    m_partPath = QDir(m_directory).filePath(safe + QStringLiteral(".part"));
    // Zmodem's part files stay for a resume. Anyone else's name is left alone.
    if (m_protocol != Protocol::Zmodem || QFileInfo(m_partPath).isSymLink())
        m_partPath = uniquePath(m_directory, safe + QStringLiteral(".part"));
    m_file = std::make_unique<QFile>(m_partPath);
    const QIODevice::OpenMode mode = resumeAt > 0 ? QIODevice::WriteOnly | QIODevice::Append
        : m_protocol == Protocol::Zmodem ? QIODevice::WriteOnly | QIODevice::Truncate
                                         : QIODevice::WriteOnly | QIODevice::NewOnly;
    if (!m_file->open(mode)) {
        const QString err = m_file->errorString();
        m_partPath.clear();
        fail(tr("Could not create %1: %2").arg(m_finalPath, err));
        return false;
    }
    m_fileSize = size;
    m_position = resumeAt;
    reportProgress();
    return true;
}

bool ModemTransfer::finishReceiveFile()
{
    if (!m_file)
        return true;
    m_file->close();
    m_file.reset();
    // The name may have been taken while this file was arriving.
    m_finalPath = uniquePath(m_directory, QFileInfo(m_finalPath).fileName());
    if (!QFile::rename(m_partPath, m_finalPath)) {
        fail(tr("Could not save %1.").arg(m_finalPath));
        return false;
    }
    m_partPath.clear();
    m_saved.append(m_finalPath);
    m_filesDone++;
    return true;
}

void ModemTransfer::discardReceiveFile()
{
    // What arrived of an interrupted Zmodem file is worth keeping.
    if (m_protocol == Protocol::Zmodem && m_file && !m_partPath.isEmpty() && m_position > 0) {
        m_file->close();
        m_file.reset();
        m_partPath.clear();
        return;
    }
    if (m_file && !m_partPath.isEmpty()) {
        m_file->close();
        m_file.reset();
    }
    if (!m_partPath.isEmpty()) {
        QFile::remove(m_partPath);
        m_partPath.clear();
    }
}

void ModemTransfer::onTimeout()
{
    if (!m_running)
        return;
    if (++m_retries > kMaxRetries) {
        fail(tr("The other end stopped answering."));
        return;
    }
    if (m_protocol == Protocol::Kermit) {
        kTimeout();
        return;
    }
    if (m_protocol != Protocol::Zmodem) {
        if (!m_sending) {
            xRecvPoke();
            return;
        }
        switch (m_phase) {
        case Phase::XSendWaitStart:
        case Phase::XSendWaitDataStart:
            armTimeout();
            break;
        case Phase::XSendWaitEotAck:
            xSendEot();
            break;
        case Phase::XSendWaitEndAck:
            // Every file was acknowledged. lrzsz's rb, for one, just exits on
            // the empty header that ends the batch.
            succeed(tr("%n file(s) sent.", nullptr, int(m_filesDone)));
            break;
        default:
            if (!m_lastPacket.isEmpty())
                out(m_lastPacket);
            armTimeout();
        }
        return;
    }
    // Zmodem: repeat what the other side is waiting for.
    switch (m_phase) {
    case Phase::ZSendInit: {
        const uchar z[4] = {0, 0, 0, 0};
        zSendHex(ZRQINIT, z);
        break;
    }
    case Phase::ZSendFileAck:
        zSendFileHeader();
        break;
    case Phase::ZSendEofAck: {
        uchar d[4];
        putPosition(d, quint32(m_position));
        zSendHex(ZEOF, d);
        break;
    }
    case Phase::ZSendFin: {
        const uchar z[4] = {0, 0, 0, 0};
        zSendHex(ZFIN, z);
        break;
    }
    case Phase::ZRecvHeader:
    case Phase::ZRecvFin: {
        const uchar z[4] = {0, 0, 0, uchar(CANFDX | CANOVIO | CANFC32)};
        zSendHex(ZRINIT, z);
        break;
    }
    case Phase::ZRecvData:
    case Phase::ZRecvFileInfo:
        zSendHexPos(ZRPOS, quint32(m_position));
        m_zp = ZParse::Hunt;
        break;
    case Phase::ZRecvResumeCheck:
        // A sender with no answer gets the file from the start.
        if (!openReceiveFile(m_zResumeName, m_zResumeSize))
            return;
        m_phase = Phase::ZRecvData;
        zSendHexPos(ZRPOS, 0);
        break;
    default:
        break;
    }
    armTimeout();
}

// ---------------------------------------------------------------------------
// Xmodem and Ymodem
// ---------------------------------------------------------------------------
void ModemTransfer::xRecvPoke()
{
    // Ask for CRC blocks; after a while, fall back to the old checksum for
    // senders that do not know it.
    if (m_block <= 1 && m_receivedBlocks == 0 && m_retries >= 3 && m_protocol != Protocol::Ymodem)
        m_crc = false;
    QByteArray b(1, m_crc && (m_receivedBlocks == 0 || m_headerExpected) ? 'C' : char(NAK));
    out(b);
    armTimeout();
}

void ModemTransfer::xSendByte(uchar c)
{
    out(QByteArray(1, char(c)));
}

// Fills the next block of the file. False at the end of the file.
bool ModemTransfer::xFillBlock()
{
    if (m_file->atEnd())
        return false;
    int size = m_protocol == Protocol::Xmodem ? 128 : 1024;
    // A short tail fits smaller blocks, and costs less padding.
    if (size == 1024 && m_fileSize - m_position <= 128)
        size = 128;
    QByteArray data = m_file->read(size);
    if (data.isEmpty())
        return false;
    m_position += data.size();
    data.append(QByteArray(size - data.size(), char(CPMEOF)));
    QByteArray p;
    p.reserve(size + 5);
    p.append(char(size == 1024 ? STX : SOH));
    p.append(char(m_block & 0xff));
    p.append(char(~m_block & 0xff));
    p.append(data);
    if (m_crc) {
        const quint16 c = crc16(data.constData(), int(data.size()));
        p.append(char(c >> 8));
        p.append(char(c & 0xff));
    } else {
        uchar sum = 0;
        for (char ch : data)
            sum = uchar(sum + uchar(ch));
        p.append(char(sum));
    }
    m_lastPacket = p;
    return true;
}

void ModemTransfer::xSendBlock()
{
    if (m_backlog && m_backlog() > kBacklogLimit) {
        m_lastPacket.clear();
        m_phase = Phase::XSendWaitAck;
        m_pumpTimer.start(20);
        return;
    }
    if (!xFillBlock()) {
        m_eotSeen = false;
        m_phase = Phase::XSendWaitEotAck;
        xSendEot();
        return;
    }
    m_retries = 0;
    m_phase = Phase::XSendWaitAck;
    out(m_lastPacket);
    reportProgress();
    armTimeout();
}

void ModemTransfer::xSendEot()
{
    xSendByte(EOT);
    armTimeout();
}

void ModemTransfer::xSendHeader(bool last)
{
    QByteArray info;
    if (!last) {
        info = m_fileName.toUtf8();
        info.append('\0');
        info.append(QByteArray::number(m_fileSize) + ' ' +
                    QByteArray::number(QFileInfo(*m_file).lastModified().toSecsSinceEpoch(), 8) + " 0");
    }
    const int size = info.size() < 128 ? 128 : 1024;
    if (info.size() > size) {
        fail(tr("The file name is too long for Ymodem."));
        return;
    }
    info.append(QByteArray(size - info.size(), '\0'));
    QByteArray p;
    p.append(char(size == 1024 ? STX : SOH));
    p.append(char(0));
    p.append(char(0xff));
    p.append(info);
    const quint16 c = crc16(info.constData(), int(info.size()));
    p.append(char(c >> 8));
    p.append(char(c & 0xff));
    m_lastPacket = p;
    out(p);
    m_phase = last ? Phase::XSendWaitEndAck : Phase::XSendWaitHeaderAck;
    if (last)
        m_timeout.start(qMin(3000, m_timeoutMs));
    else
        armTimeout();
}

bool ModemTransfer::xRecvHandleBlock(const QByteArray &data, int blockNo)
{
    const int expected = m_block & 0xff;
    if (blockNo == ((m_block - 1) & 0xff) && (m_receivedBlocks > 0 || m_block == 1)) {
        xSendByte(ACK);    // our ACK was lost: the sender repeated the block
        armTimeout();
        return true;
    }
    if (blockNo != expected) {
        out(QByteArray(8, char(CAN)));
        fail(tr("Blocks arrived out of order."));
        return false;
    }
    if (m_headerExpected) {
        const int zero = int(data.indexOf('\0'));
        const QByteArray nameBytes = data.left(zero < 0 ? data.size() : zero);
        if (nameBytes.isEmpty()) {
            // An empty header ends the batch.
            xSendByte(ACK);
            succeed(tr("%n file(s) received.", nullptr, int(m_filesDone)));
            return false;
        }
        qint64 size = -1;
        if (zero >= 0) {
            const QByteArray meta = data.mid(zero + 1);
            const int end = int(meta.indexOf('\0'));
            const QByteArray first = meta.left(end < 0 ? meta.size() : end).split(' ').value(0);
            bool ok = false;
            const qint64 v = first.toLongLong(&ok);
            if (ok && v >= 0)
                size = v;
        }
        if (!openReceiveFile(QString::fromUtf8(nameBytes), size))
            return false;
        m_headerExpected = false;
        m_block = 1;
        m_receivedBlocks = 0;
        m_eotSeen = false;
        xSendByte(ACK);
        out(QByteArray(1, 'C'));
        armTimeout();
        return true;
    }
    QByteArray body = data;
    if (m_fileSize >= 0 && m_position + body.size() > m_fileSize)
        body.truncate(m_fileSize - m_position);
    if (m_file->write(body) != body.size()) {
        const QString err = m_file->errorString();
        out(QByteArray(8, char(CAN)));
        fail(tr("Could not write the file: %1").arg(err));
        return false;
    }
    m_position += body.size();
    m_block++;
    m_receivedBlocks++;
    m_retries = 0;
    xSendByte(ACK);
    reportProgress();
    armTimeout();
    return true;
}

void ModemTransfer::xProcess()
{
    while (m_running && !m_in.isEmpty()) {
        const uchar c = uchar(m_in[0]);

        // Two CANs in a row mean the other end gave up.
        if (m_phase != Phase::XRecvBlock || (c == CAN)) {
            if (c == CAN) {
                if (m_in.size() < 2 && m_cans == 0) {
                    m_cans = 1;
                    m_in.remove(0, 1);
                    continue;
                }
                if (m_cans > 0 || uchar(m_in[1]) == CAN) {
                    fail(tr("The other end cancelled the transfer."));
                    return;
                }
            } else {
                m_cans = 0;
            }
        }

        if (m_sending) {
            m_in.remove(0, 1);
            switch (m_phase) {
            case Phase::XSendWaitStart:
                if (c == 'C' || c == NAK) {
                    m_crc = c == 'C' || m_protocol == Protocol::Ymodem;
                    m_block = 1;
                    if (m_protocol == Protocol::Ymodem) {
                        if (m_files.isEmpty()) {
                            xSendHeader(true);
                            break;
                        }
                        if (!openNextSendFile())
                            return;
                        xSendHeader(false);
                    } else {
                        xSendBlock();
                    }
                }
                break;
            case Phase::XSendWaitHeaderAck:
                if (c == ACK) {
                    m_phase = Phase::XSendWaitDataStart;
                    armTimeout();
                } else if (c == NAK) {
                    if (++m_retries > kMaxRetries) {
                        fail(tr("The other end refused the file name block."));
                        return;
                    }
                    out(m_lastPacket);
                    armTimeout();
                } else if (c == 'C') {
                    // Some receivers go straight on to asking for data.
                    m_block = 1;
                    xSendBlock();
                }
                break;
            case Phase::XSendWaitDataStart:
                if (c == 'C' || c == NAK) {
                    m_block = 1;
                    xSendBlock();
                }
                break;
            case Phase::XSendWaitAck:
                if (c == ACK) {
                    m_block++;
                    xSendBlock();
                } else if (c == NAK) {
                    if (++m_retries > kMaxRetries) {
                        fail(tr("Too many errors on the line."));
                        return;
                    }
                    if (!m_lastPacket.isEmpty())
                        out(m_lastPacket);
                    armTimeout();
                }
                break;
            case Phase::XSendWaitEotAck:
                if (c == NAK) {
                    if (++m_retries > kMaxRetries) {
                        fail(tr("The end of the file was not acknowledged."));
                        return;
                    }
                    xSendEot();
                } else if (c == ACK) {
                    m_file.reset();
                    m_filesDone++;
                    if (m_protocol == Protocol::Ymodem) {
                        m_retries = 0;
                        m_phase = Phase::XSendWaitStart;
                        armTimeout();
                    } else {
                        succeed(tr("%1 sent.").arg(m_fileName));
                        return;
                    }
                }
                break;
            case Phase::XSendWaitEndAck:
                if (c == NAK) {
                    if (++m_retries > kMaxRetries) {
                        fail(tr("The end of the batch was not acknowledged."));
                        return;
                    }
                    out(m_lastPacket);
                    m_timeout.start(qMin(3000, m_timeoutMs));
                } else {
                    // An ACK, or the shell's prompt from a receiver that exited.
                    succeed(tr("%n file(s) sent.", nullptr, int(m_filesDone)));
                    return;
                }
                break;
            default:
                break;
            }
            continue;
        }

        // Receiving.
        if (c == EOT) {
            m_in.remove(0, 1);
            if (m_protocol == Protocol::Ymodem && !m_eotSeen) {
                // The spec's double EOT guards against a damaged byte.
                m_eotSeen = true;
                xSendByte(NAK);
                armTimeout();
                continue;
            }
            xSendByte(ACK);
            if (!finishReceiveFile())
                return;
            if (m_protocol == Protocol::Ymodem) {
                m_headerExpected = true;
                m_block = 0;
                m_receivedBlocks = 0;
                m_retries = 0;
                out(QByteArray(1, 'C'));
                armTimeout();
                continue;
            }
            succeed(tr("%1 received.").arg(m_fileName));
            return;
        }
        if (c != SOH && c != STX) {
            m_in.remove(0, 1);    // line noise between blocks
            continue;
        }
        const int size = c == SOH ? 128 : 1024;
        const int total = 3 + size + (m_crc ? 2 : 1);
        if (m_in.size() < total)
            return;     // the rest is on its way
        const int blockNo = uchar(m_in[1]);
        const bool headerOk = uchar(m_in[2]) == uchar(~blockNo);
        const QByteArray body = m_in.mid(3, size);
        bool sumOk;
        if (m_crc) {
            const quint16 sent = quint16(uchar(m_in[3 + size])) << 8 | uchar(m_in[4 + size]);
            sumOk = crc16(body.constData(), size) == sent;
        } else {
            uchar sum = 0;
            for (char ch : body)
                sum = uchar(sum + uchar(ch));
            sumOk = sum == uchar(m_in[3 + size]);
        }
        m_in.remove(0, total);
        if (!headerOk || !sumOk) {
            if (++m_retries > kMaxRetries) {
                out(QByteArray(8, char(CAN)));
                fail(tr("Too many errors on the line."));
                return;
            }
            m_in.clear();
            xSendByte(NAK);
            armTimeout();
            continue;
        }
        if (!xRecvHandleBlock(body, blockNo))
            return;
    }
}

// ---------------------------------------------------------------------------
// Zmodem: framing
// ---------------------------------------------------------------------------
void ModemTransfer::zEscape(QByteArray &o, uchar c)
{
    bool esc = false;
    switch (c) {
    case ZDLE: case 0x10: case 0x90: case 0x11: case 0x91: case 0x13: case 0x93:
        esc = true;
        break;
    case 0x0d: case 0x8d:
        esc = (m_zLast & 0x7f) == '@';    // "@\r" drops a Telenet-style link
        break;
    default:
        esc = m_peerEscCtl && (c & 0x60) == 0;
    }
    if (esc) {
        o.append(char(ZDLE));
        o.append(char(c ^ 0x40));
    } else {
        o.append(char(c));
    }
    m_zLast = c;
}

void ModemTransfer::zSendHex(int type, const uchar d[4])
{
    QByteArray raw;
    raw.append(char(type));
    raw.append(reinterpret_cast<const char *>(d), 4);
    const quint16 crc = crc16(raw.constData(), 5);
    raw.append(char(crc >> 8));
    raw.append(char(crc & 0xff));
    QByteArray o("**\x18" "B", 4);
    o.append(raw.toHex());
    o.append("\r\x8a", 2);
    if (type != ZFIN && type != ZACK)
        o.append('\x11');    // in case a stray XOFF stopped the far end
    out(o);
}

void ModemTransfer::zSendHexPos(int type, quint32 pos)
{
    uchar d[4];
    putPosition(d, pos);
    zSendHex(type, d);
}

void ModemTransfer::zSendBin(int type, const uchar d[4])
{
    QByteArray o;
    o.append(char(ZPAD));
    o.append(char(ZDLE));
    o.append(char(m_useBin32 ? ZBIN32 : ZBIN));
    QByteArray raw;
    raw.append(char(type));
    raw.append(reinterpret_cast<const char *>(d), 4);
    m_zLast = 0;
    for (char c : raw)
        zEscape(o, uchar(c));
    if (m_useBin32) {
        quint32 crc = crc32(raw);
        for (int i = 0; i < 4; i++) {
            zEscape(o, uchar(crc));
            crc >>= 8;
        }
    } else {
        const quint16 crc = crc16(raw.constData(), 5);
        zEscape(o, uchar(crc >> 8));
        zEscape(o, uchar(crc & 0xff));
    }
    out(o);
}

void ModemTransfer::zSendSubpacket(const QByteArray &data, uchar end)
{
    QByteArray o;
    o.reserve(data.size() + data.size() / 8 + 16);
    for (char c : data)
        zEscape(o, uchar(c));
    o.append(char(ZDLE));
    o.append(char(end));
    QByteArray withEnd = data;
    withEnd.append(char(end));
    if (m_useBin32) {
        quint32 crc = crc32(withEnd);
        for (int i = 0; i < 4; i++) {
            zEscape(o, uchar(crc));
            crc >>= 8;
        }
    } else {
        const quint16 crc = crc16(withEnd.constData(), int(withEnd.size()));
        zEscape(o, uchar(crc >> 8));
        zEscape(o, uchar(crc & 0xff));
    }
    if (end == ZCRCW)
        o.append('\x11');
    out(o);
}

void ModemTransfer::zProcess()
{
    const QByteArray in = m_in;
    m_in.clear();
    for (int i = 0; i < in.size() && m_running; i++) {
        zByte(uchar(in[i]));
    }
}

void ModemTransfer::zExpectData()
{
    m_zp = ZParse::SubData;
    m_zBuf.clear();
    m_zEscaped = false;
}

void ModemTransfer::zByte(uchar c)
{
    // Five CANs in a row abort, wherever they arrive.
    if (c == ZDLE) {
        if (++m_zCans >= 5) {
            fail(tr("The other end cancelled the transfer."));
            return;
        }
    } else {
        m_zCans = 0;
    }

    const bool wantsEscape = m_zp == ZParse::BinHeader || m_zp == ZParse::BinHeaderCrc
        || m_zp == ZParse::SubData || m_zp == ZParse::SubCrc;
    uchar value = c;
    bool frameEnd = false;
    if (wantsEscape) {
        if (c == 0x11 || c == 0x13 || c == 0x91 || c == 0x93)
            return;     // flow control is never data: data has it escaped
        if (!m_zEscaped) {
            if (c == ZDLE) {
                m_zEscaped = true;
                return;
            }
        } else {
            m_zEscaped = false;
            switch (c) {
            case ZCRCE: case ZCRCG: case ZCRCQ: case ZCRCW:
                frameEnd = true;
                break;
            case ZRUB0: value = 0x7f; break;
            case ZRUB1: value = 0xff; break;
            default:
                if (c == ZDLE) {
                    m_zEscaped = true;   // a CAN run: stay ready for the next
                    return;
                }
                if ((c & 0x60) != 0x40) {
                    m_zp = ZParse::Hunt;   // a damaged frame
                    return;
                }
                value = c ^ 0x40;
            }
        }
    }

    switch (m_zp) {
    case ZParse::Hunt:
        if ((c & 0x7f) == ZPAD)
            m_zp = ZParse::AfterPad;
        break;
    case ZParse::AfterPad:
        if ((c & 0x7f) == ZPAD)
            break;
        m_zp = c == ZDLE ? ZParse::AfterZdle : ZParse::Hunt;
        break;
    case ZParse::AfterZdle: {
        const uchar f = c & 0x7f;
        m_zBuf.clear();
        m_zEscaped = false;
        if (f == ZHEX) {
            m_zp = ZParse::Hex;
        } else if (f == ZBIN || f == ZBIN32) {
            m_zFrameFormat = f;
            m_zp = ZParse::BinHeader;
        } else {
            m_zp = ZParse::Hunt;
        }
        break;
    }
    case ZParse::Hex: {
        if (hexValue(c) < 0) {
            m_zp = ZParse::Hunt;
            break;
        }
        m_zBuf.append(char(c & 0x7f));
        if (m_zBuf.size() < 14)
            break;
        const QByteArray raw = QByteArray::fromHex(m_zBuf);
        m_zp = ZParse::Hunt;
        if (crc16(raw.constData(), 5) !=
            (quint16(uchar(raw[5])) << 8 | uchar(raw[6]))) {
            if (m_phase == Phase::ZRecvData || m_phase == Phase::ZRecvHeader) {
                const uchar z[4] = {0, 0, 0, 0};
                zSendHex(ZNAK, z);
            }
            break;
        }
        ZHeader h;
        h.type = uchar(raw[0]);
        memcpy(h.d, raw.constData() + 1, 4);
        m_zFrameFormat = ZHEX;
        zHandleHeader(h);
        break;
    }
    case ZParse::BinHeader:
    case ZParse::BinHeaderCrc: {
        m_zBuf.append(char(value));
        const int need = 5 + (m_zFrameFormat == ZBIN32 ? 4 : 2);
        if (m_zBuf.size() < need)
            break;
        bool ok;
        if (m_zFrameFormat == ZBIN32) {
            const quint32 got = quint32(uchar(m_zBuf[5])) | quint32(uchar(m_zBuf[6])) << 8
                | quint32(uchar(m_zBuf[7])) << 16 | quint32(uchar(m_zBuf[8])) << 24;
            ok = crc32(m_zBuf.left(5)) == got;
        } else {
            ok = crc16(m_zBuf.constData(), 5) ==
                 (quint16(uchar(m_zBuf[5])) << 8 | uchar(m_zBuf[6]));
        }
        m_zp = ZParse::Hunt;
        if (!ok) {
            const uchar z[4] = {0, 0, 0, 0};
            zSendHex(ZNAK, z);
            break;
        }
        ZHeader h;
        h.type = uchar(m_zBuf[0]);
        memcpy(h.d, m_zBuf.constData() + 1, 4);
        zHandleHeader(h);
        break;
    }
    case ZParse::SubData:
        if (frameEnd) {
            m_zSubEnd = c;
            m_zCrcTail.clear();
            m_zp = ZParse::SubCrc;
            m_zCans = 0;
            break;
        }
        m_zBuf.append(char(value));
        if (m_zBuf.size() > 8 * 1024 + 64) {
            m_zp = ZParse::Hunt;
            zSendHexPos(ZRPOS, quint32(m_position));
        }
        break;
    case ZParse::SubCrc: {
        QByteArray &tail = m_zCrcTail;
        tail.append(char(value));
        const int need = m_zFrameFormat == ZBIN32 ? 4 : 2;
        if (tail.size() < need)
            break;
        QByteArray check = m_zBuf;
        check.append(char(m_zSubEnd));
        bool ok;
        if (m_zFrameFormat == ZBIN32) {
            const quint32 got = quint32(uchar(tail[0])) | quint32(uchar(tail[1])) << 8
                | quint32(uchar(tail[2])) << 16 | quint32(uchar(tail[3])) << 24;
            ok = crc32(check) == got;
        } else {
            ok = crc16(check.constData(), int(check.size())) ==
                 (quint16(uchar(tail[0])) << 8 | uchar(tail[1]));
        }
        tail.clear();
        if (!ok) {
            m_zp = ZParse::Hunt;
            if (m_retries++ > kMaxRetries) {
                fail(tr("Too many errors on the line."));
                return;
            }
            zSendHexPos(ZRPOS, quint32(m_position));
            armTimeout();
            return;
        }
        const QByteArray data = m_zBuf;
        const uchar end = m_zSubEnd;
        // More data follows an ending that says so; otherwise a header does.
        if (end == ZCRCG || end == ZCRCQ) {
            m_zBuf.clear();
            m_zp = ZParse::SubData;
        } else {
            m_zp = ZParse::Hunt;
        }
        zHandleSubpacket(data, end);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Zmodem: sending
// ---------------------------------------------------------------------------
void ModemTransfer::zStartSend()
{
    m_phase = Phase::ZSendInit;
    m_useBin32 = false;
    m_peerEscCtl = false;
    m_peerBuffer = 0;
    const uchar z[4] = {0, 0, 0, 0};
    zSendHex(ZRQINIT, z);
    armTimeout();
}

void ModemTransfer::zSendFileHeader()
{
    // ZF0 is d[3]: binary as is, or carry on after what the receiver holds.
    const uchar d[4] = {0, 0, 0, uchar(m_zResume ? ZCRESUM : ZCBIN)};
    zSendBin(ZFILE, d);
    qint64 left = 0;
    for (const QString &f : std::as_const(m_files))
        left += QFileInfo(f).size();
    QByteArray info = m_fileName.toUtf8();
    info.append('\0');
    info.append(QByteArray::number(m_fileSize) + ' ' +
                QByteArray::number(QFileInfo(*m_file).lastModified().toSecsSinceEpoch(), 8) +
                " 100644 0 " + QByteArray::number(m_files.size() + 1) + ' ' +
                QByteArray::number(left + m_fileSize));
    info.append('\0');
    m_zLast = 0;
    zSendSubpacket(info, ZCRCW);
    m_phase = Phase::ZSendFileAck;
    armTimeout();
}

void ModemTransfer::zPumpData()
{
    for (int sent = 0; m_running && m_phase == Phase::ZSendData; sent++) {
        // Back to the event loop often enough for the receiver's replies,
        // such as a ZRPOS after a damaged block, to be seen.
        if (sent == 8) {
            m_pumpTimer.start(0);
            return;
        }
        if (m_backlog && m_backlog() > kBacklogLimit) {
            m_pumpTimer.start(10);
            return;
        }
        const QByteArray data = m_file->read(kZBlock);
        const bool atEnd = m_file->atEnd();
        const qint64 next = m_position + data.size();
        // A receiver with a small buffer must have each stretch acknowledged.
        const bool waitAck = !atEnd && m_peerBuffer > 0
            && next - m_zAcked + kZBlock > m_peerBuffer;
        m_zLast = 0;
        zSendSubpacket(data, atEnd ? ZCRCE : waitAck ? ZCRCW : ZCRCG);
        m_position = next;
        reportProgress();
        if (atEnd) {
            uchar d[4];
            putPosition(d, quint32(m_position));
            zSendBin(ZEOF, d);
            m_phase = Phase::ZSendEofAck;
            armTimeout();
            return;
        }
        if (waitAck) {
            m_phase = Phase::ZSendWaitAck;
            armTimeout();
            return;
        }
        armTimeout();
    }
}

void ModemTransfer::zFinishSending()
{
    const uchar z[4] = {0, 0, 0, 0};
    zSendHex(ZFIN, z);
    m_phase = Phase::ZSendFin;
    armTimeout();
}

void ModemTransfer::zHandleHeader(const ZHeader &h)
{
    if (h.type == ZCAN || h.type == ZABORT) {
        if (h.type == ZABORT && !m_sending) {
            const uchar z[4] = {0, 0, 0, 0};
            zSendHex(ZFIN, z);
        }
        fail(tr("The other end cancelled the transfer."));
        return;
    }
    m_retries = 0;

    if (m_sending) {
        switch (h.type) {
        case ZRINIT:
            armTimeout();
            if (m_phase == Phase::ZSendInit) {
                m_peerBuffer = h.d[0] | h.d[1] << 8;
                const uchar f0 = h.d[3];
                m_peerEscCtl = f0 & ESCCTL;
                m_useBin32 = f0 & CANFC32;
                if (!(f0 & CANFDX) || !(f0 & CANOVIO))
                    m_peerBuffer = m_peerBuffer ? m_peerBuffer : kZBlock;
            }
            if (m_phase == Phase::ZSendInit || m_phase == Phase::ZSendEofAck ||
                m_phase == Phase::ZSendFileAck) {
                if (m_phase == Phase::ZSendEofAck)
                    m_filesDone++;
                if (m_phase == Phase::ZSendFileAck)
                    break;    // repeated: the ZFILE is on its way
                if (m_files.isEmpty()) {
                    zFinishSending();
                } else {
                    if (!openNextSendFile())
                        return;
                    zSendFileHeader();
                }
            }
            break;
        case ZSKIP:
            m_file.reset();
            m_filesDone++;
            if (m_files.isEmpty()) {
                zFinishSending();
            } else {
                if (!openNextSendFile())
                    return;
                zSendFileHeader();
            }
            break;
        case ZRPOS: {
            const quint32 pos = h.position();
            if (!m_file || !m_file->seek(pos)) {
                fail(tr("The other end asked for an invalid position."));
                return;
            }
            m_position = pos;
            m_zAcked = pos;
            uchar d[4];
            putPosition(d, pos);
            zSendBin(ZDATA, d);
            m_phase = Phase::ZSendData;
            armTimeout();
            m_pumpTimer.start(0);
            break;
        }
        case ZACK:
            m_zAcked = h.position();
            if (m_phase == Phase::ZSendWaitAck) {
                uchar d[4];
                putPosition(d, quint32(m_position));
                zSendBin(ZDATA, d);
                m_phase = Phase::ZSendData;
                m_pumpTimer.start(0);
            }
            armTimeout();
            break;
        case ZNAK:
            onTimeout();
            break;
        case ZCRC: {
            // The receiver asks whether what it holds is the start of this file.
            if (!m_file)
                break;
            const qint64 was = m_file->pos();
            m_file->seek(0);
            quint32 crc = 0xffffffffu;
            qint64 left = h.position() ? qint64(h.position()) : m_file->size();
            while (left > 0) {
                const QByteArray b = m_file->read(qMin<qint64>(left, 65536));
                if (b.isEmpty())
                    break;
                crc = crc32Update(crc, b.constData(), int(b.size()));
                left -= b.size();
            }
            m_file->seek(was);
            uchar d[4];
            putPosition(d, ~crc);
            zSendBin(ZCRC, d);
            armTimeout();
            break;
        }
        case ZFIN:
            if (m_phase == Phase::ZSendFin) {
                out(QByteArrayLiteral("OO"));
                succeed(tr("%n file(s) sent.", nullptr, int(m_filesDone)));
            }
            break;
        case ZFERR:
            fail(tr("The other end could not write the file."));
            break;
        default:
            break;
        }
        return;
    }

    // Receiving.
    switch (h.type) {
    case ZRQINIT: {
        const uchar z[4] = {0, 0, 0, uchar(CANFDX | CANOVIO | CANFC32)};
        zSendHex(ZRINIT, z);
        armTimeout();
        break;
    }
    case ZSINIT:
        m_phase = Phase::ZRecvSinit;
        zExpectData();
        armTimeout();
        break;
    case ZFILE:
        m_phase = Phase::ZRecvFileInfo;
        zExpectData();
        armTimeout();
        break;
    case ZDATA:
        if (!m_file) {
            zSendHexPos(ZRPOS, 0);
            break;
        }
        if (h.position() != quint32(m_position)) {
            zSendHexPos(ZRPOS, quint32(m_position));
            armTimeout();
            break;
        }
        m_phase = Phase::ZRecvData;
        zExpectData();
        armTimeout();
        break;
    case ZEOF: {
        if (h.position() != quint32(m_position)) {
            armTimeout();
            break;     // stale: the data it follows was rejected
        }
        if (!finishReceiveFile())
            return;
        m_phase = Phase::ZRecvHeader;
        const uchar z[4] = {0, 0, 0, uchar(CANFDX | CANOVIO | CANFC32)};
        zSendHex(ZRINIT, z);
        armTimeout();
        break;
    }
    case ZFIN: {
        const uchar z[4] = {0, 0, 0, 0};
        zSendHex(ZFIN, z);
        succeed(tr("%n file(s) received.", nullptr, int(m_filesDone)));
        break;
    }
    case ZCRC:
        if (m_phase == Phase::ZRecvResumeCheck) {
            QFile part(QDir(m_directory).filePath(safeFileName(m_zResumeName) + QStringLiteral(".part")));
            quint32 crc = 0xffffffffu;
            if (part.open(QIODevice::ReadOnly)) {
                qint64 left = m_zResumeAt;
                while (left > 0) {
                    const QByteArray b = part.read(qMin<qint64>(left, 65536));
                    if (b.isEmpty())
                        break;
                    crc = crc32Update(crc, b.constData(), int(b.size()));
                    left -= b.size();
                }
            }
            const qint64 from = ~crc == h.position() ? m_zResumeAt : 0;
            if (!openReceiveFile(m_zResumeName, m_zResumeSize, from))
                return;
            m_phase = Phase::ZRecvData;
            zSendHexPos(ZRPOS, quint32(from));
            armTimeout();
            break;
        }
        [[fallthrough]];
    case ZCOMMAND:
    case ZFREECNT:
    case ZCHALLENGE:
        // A remote must never run commands here: ZCOMMAND is how "sz -c"
        // does that. None of these is needed to receive a file.
        out(QByteArray(8, char(CAN)));
        fail(tr("The other end asked for something other than a file transfer."));
        break;
    default:
        break;
    }
}

void ModemTransfer::zHandleSubpacket(const QByteArray &data, uchar end)
{
    m_retries = 0;
    switch (m_phase) {
    case Phase::ZRecvSinit:
        m_phase = Phase::ZRecvHeader;
        {
            const uchar z[4] = {0, 0, 0, 0};
            zSendHex(ZACK, z);
        }
        armTimeout();
        break;
    case Phase::ZRecvFileInfo: {
        const int zero = int(data.indexOf('\0'));
        const QByteArray name = data.left(zero < 0 ? data.size() : zero);
        qint64 size = -1;
        if (zero >= 0) {
            bool ok = false;
            const qint64 v = data.mid(zero + 1).split(' ').value(0).toLongLong(&ok);
            if (ok && v >= 0)
                size = v;
        }
        // A part from an earlier try that this file could continue.
        const QFileInfo part(QDir(m_directory).filePath(safeFileName(QString::fromUtf8(name))
                                                        + QStringLiteral(".part")));
        if (part.isFile() && !part.isSymLink() && part.size() > 0
            && (size < 0 || part.size() < size) && part.size() < 0x7fffffff) {
            m_zResumeName = QString::fromUtf8(name);
            m_zResumeSize = size;
            m_zResumeAt = part.size();
            m_phase = Phase::ZRecvResumeCheck;
            zSendHexPos(ZCRC, quint32(m_zResumeAt));
            armTimeout();
            break;
        }
        if (!openReceiveFile(QString::fromUtf8(name), size))
            return;
        m_phase = Phase::ZRecvData;
        zSendHexPos(ZRPOS, 0);
        armTimeout();
        break;
    }
    case Phase::ZRecvData: {
        if (m_file->write(data) != data.size()) {
            const QString err = m_file->errorString();
            const uchar z[4] = {0, 0, 0, 0};
            zSendHex(ZFERR, z);
            out(QByteArray(8, char(CAN)));
            fail(tr("Could not write the file: %1").arg(err));
            return;
        }
        m_position += data.size();
        reportProgress();
        if (end == ZCRCQ || end == ZCRCW)
            zSendHexPos(ZACK, quint32(m_position));
        armTimeout();
        break;
    }
    default:
        break;
    }
}

void ModemTransfer::zStartReceive()
{
    m_phase = Phase::ZRecvHeader;
    m_fileSize = -1;
    m_position = 0;
    const uchar z[4] = {0, 0, 0, uchar(CANFDX | CANOVIO | CANFC32)};
    zSendHex(ZRINIT, z);
    armTimeout();
}
