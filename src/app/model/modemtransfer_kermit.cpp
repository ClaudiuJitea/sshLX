// Kermit, the ModemTransfer way: 94-byte packets, one packet at a time, with
// the single-character checksum every Kermit has. Slow, and it gets through
// lines that nothing else does.
#include "modemtransfer.h"

#include <QFileInfo>

namespace {
constexpr uchar MARK = 0x01, EOL_CHAR = 0x0d;
constexpr int kMaxLen = 94;
constexpr int kKermitTimeoutMs = 5000;

char toChar(int x) { return char(x + 32); }
int unChar(uchar c) { return int(c) - 32; }

char checksum(const QByteArray &bytes)
{
    int s = 0;
    for (char c : bytes)
        s += uchar(c);
    return toChar((s + ((s & 192) >> 6)) & 63);
}

QByteArray initParams()
{
    QByteArray p;
    p.append(toChar(kMaxLen));   // longest packet we take
    p.append(toChar(5));         // timeout
    p.append(toChar(0));         // no padding
    p.append('@');               // pad character, unused
    p.append(toChar(EOL_CHAR));
    p.append('#');               // control prefix
    p.append('Y');               // eighth-bit prefix if the other end needs it
    p.append('1');               // checksum type
    return p;
}
} // namespace

void ModemTransfer::kArm()
{
    m_timeout.start(qMin(kKermitTimeoutMs, m_timeoutMs));
}

void ModemTransfer::kSend(char type, int seq, const QByteArray &data)
{
    QByteArray body;
    body.append(toChar(int(data.size()) + 3));   // SEQ, TYPE and the check
    body.append(toChar(seq & 63));
    body.append(type);
    body.append(data);
    QByteArray p(1, char(MARK));
    p.append(body);
    p.append(checksum(body));
    p.append(char(EOL_CHAR));
    m_lastPacket = p;
    out(p);
    kArm();
}

void ModemTransfer::kApplyParams(const QByteArray &p)
{
    m_kMaxData = kMaxLen - 5;
    if (!p.isEmpty()) {
        const int maxl = unChar(uchar(p[0]));
        if (maxl >= 20 && maxl <= kMaxLen)
            m_kMaxData = maxl - 5;
    }
    m_kQctl = p.size() > 5 ? p[5] : '#';
    const char qbin = p.size() > 6 ? p[6] : 'N';
    // A real prefix character, rather than Y (willing) or N (no).
    m_kQbin = (uchar(qbin) >= 33 && uchar(qbin) <= 62) || (uchar(qbin) >= 96 && uchar(qbin) <= 126)
        ? qbin : 0;
    m_kRept = p.size() > 8 && p[8] == '~' ? '~' : 0;
}

QByteArray ModemTransfer::kEncodeByte(uchar b) const
{
    QByteArray o;
    if (m_kQbin && (b & 0x80)) {
        o.append(m_kQbin);
        b &= 0x7f;
    }
    const uchar low = b & 0x7f;
    if (low < 32 || low == 127) {
        o.append('#');
        o.append(char(b ^ 64));
    } else if (low == '#' || (m_kQbin && low == uchar(m_kQbin))) {
        o.append('#');
        o.append(char(b));
    } else {
        o.append(char(b));
    }
    return o;
}

QByteArray ModemTransfer::kDecode(const QByteArray &data) const
{
    QByteArray o;
    for (int i = 0; i < data.size(); i++) {
        int repeat = 1;
        uchar c = uchar(data[i]);
        if (m_kRept && c == uchar(m_kRept) && i + 1 < data.size()) {
            repeat = unChar(uchar(data[++i]));
            if (++i >= data.size())
                break;
            c = uchar(data[i]);
        }
        uchar high = 0;
        if (m_kQbin && c == uchar(m_kQbin) && i + 1 < data.size()) {
            high = 0x80;
            c = uchar(data[++i]);
        }
        if (c == '#' && i + 1 < data.size()) {
            c = uchar(data[++i]);
            const uchar low = c & 0x7f;
            if ((low >= 64 && low <= 95) || low == 63)
                c ^= 64;
        }
        o.append(QByteArray(repeat, char(c | high)));
    }
    return o;
}

void ModemTransfer::kFileHeader()
{
    QByteArray name;
    for (char c : m_fileName.toUtf8())
        name += kEncodeByte(uchar(c));
    m_phase = Phase::KSendFile;
    kSend('F', ++m_kSeq, name);
}

void ModemTransfer::kNextData()
{
    QByteArray data;
    while (data.size() < m_kMaxData - 2 && !m_file->atEnd()) {
        char c;
        m_file->getChar(&c);
        m_position++;
        data += kEncodeByte(uchar(c));
    }
    reportProgress();
    if (data.isEmpty()) {
        m_phase = Phase::KSendEof;
        m_file.reset();
        m_filesDone++;
        kSend('Z', ++m_kSeq, {});
        return;
    }
    m_phase = Phase::KSendData;
    kSend('D', ++m_kSeq, data);
}

void ModemTransfer::kStart()
{
    m_kSeq = 0;
    m_kQctl = '#';
    m_kQbin = 0;
    m_kRept = 0;
    m_kMaxData = kMaxLen - 5;
    if (m_sending) {
        m_phase = Phase::KSendInit;
        kSend('S', 0, initParams());
    } else {
        m_phase = Phase::KRecv;
        kArm();
    }
}

void ModemTransfer::kAfterAck(const QByteArray &data)
{
    switch (m_phase) {
    case Phase::KSendInit:
        kApplyParams(data);
        m_kRept = 0;       // this end doesn't offer repeats, or send them
        if (m_files.isEmpty() || !openNextSendFile())
            return;
        kFileHeader();
        break;
    case Phase::KSendFile:
        kNextData();
        break;
    case Phase::KSendData:
        // The receiver can refuse the rest of a file in its ACK.
        if (data.startsWith('X') || data.startsWith('Z')) {
            m_file.reset();
            m_phase = Phase::KSendEof;
            kSend('Z', ++m_kSeq, QByteArrayLiteral("D"));
            return;
        }
        kNextData();
        break;
    case Phase::KSendEof:
        if (m_files.isEmpty()) {
            m_phase = Phase::KSendBreak;
            kSend('B', ++m_kSeq, {});
        } else {
            if (!openNextSendFile())
                return;
            kFileHeader();
        }
        break;
    case Phase::KSendBreak:
        succeed(tr("%n file(s) sent.", nullptr, int(m_filesDone)));
        break;
    default:
        break;
    }
}

void ModemTransfer::kHandlePacket(char type, int seq, const QByteArray &data)
{
    if (type == 'E') {
        fail(tr("The other end stopped: %1").arg(QString::fromLatin1(kDecode(data))));
        return;
    }
    if (m_sending) {
        const int mine = m_kSeq & 63;
        if ((type == 'Y' && seq == mine) || (type == 'N' && seq == ((mine + 1) & 63))) {
            m_retries = 0;
            kAfterAck(type == 'Y' ? data : QByteArray());
        } else if (type == 'N') {
            if (++m_retries > kMaxKermitRetries) {
                fail(tr("Too many errors on the line."));
                return;
            }
            out(m_lastPacket);
            kArm();
        }
        return;
    }

    // Receiving.
    const int expected = m_kSeq & 63;
    if (seq == ((expected - 1) & 63) && !m_lastPacket.isEmpty() && type != 'S') {
        out(m_lastPacket);    // our ACK was lost: say it again
        kArm();
        return;
    }
    if (seq != expected) {
        kSendNak(expected);
        return;
    }
    m_retries = 0;
    switch (type) {
    case 'S': {
        kApplyParams(data);
        QByteArray reply = initParams();
        // Agree on a prefix: use none unless the sender insists on one.
        reply[6] = m_kQbin ? m_kQbin : 'N';
        reply.append(m_kRept ? m_kRept : ' ');   // repeats count only if agreed here
        kSend('Y', seq, reply);
        m_kSeq++;
        break;
    }
    case 'F': {
        if (!openReceiveFile(QString::fromUtf8(kDecode(data)), -1))
            return;
        kSend('Y', seq, {});
        m_kSeq++;
        break;
    }
    case 'A':
        kSend('Y', seq, {});
        m_kSeq++;
        break;
    case 'D': {
        const QByteArray bytes = kDecode(data);
        if (!m_file || m_file->write(bytes) != bytes.size()) {
            const QString err = m_file ? m_file->errorString() : tr("no file is open");
            kSend('E', seq, err.toLatin1());
            fail(tr("Could not write the file: %1").arg(err));
            return;
        }
        m_position += bytes.size();
        reportProgress();
        kSend('Y', seq, {});
        m_kSeq++;
        break;
    }
    case 'Z':
        if (data.startsWith('D')) {
            discardReceiveFile();
        } else if (!finishReceiveFile()) {
            return;
        }
        kSend('Y', seq, {});
        m_kSeq++;
        break;
    case 'B':
        kSend('Y', seq, {});
        succeed(tr("%n file(s) received.", nullptr, int(m_filesDone)));
        break;
    default:
        // A Kermit server's commands, among them running programs (C) and
        // sending files on request (R). None of that is asked for here.
        kSend('E', seq, QByteArrayLiteral("Not a Kermit server"));
        fail(tr("The other end asked for something other than a file transfer."));
    }
}

void ModemTransfer::kSendNak(int seq)
{
    QByteArray body;
    body.append(toChar(3));
    body.append(toChar(seq & 63));
    body.append('N');
    QByteArray p(1, char(MARK));
    p.append(body);
    p.append(checksum(body));
    p.append(char(EOL_CHAR));
    out(p);
    kArm();
}

void ModemTransfer::kProcess()
{
    while (m_running) {
        const int start = int(m_in.indexOf(char(MARK)));
        if (start < 0) {
            m_in.clear();
            return;
        }
        m_in.remove(0, start);
        if (m_in.size() < 2)
            return;
        const int len = unChar(uchar(m_in[1]));
        if (len < 3 || len > kMaxLen) {
            m_in.remove(0, 1);      // not a packet this code ever agreed to
            continue;
        }
        if (m_in.size() < len + 2)
            return;
        const QByteArray body = m_in.mid(1, len);     // LEN to the data
        const char check = m_in[len + 1];
        const int seq = unChar(uchar(m_in[2]));
        const char type = m_in[3];
        const QByteArray data = m_in.mid(4, len - 3);
        m_in.remove(0, len + 2);
        if (checksum(body) != check || seq < 0 || seq > 63) {
            if (++m_retries > kMaxKermitRetries) {
                fail(tr("Too many errors on the line."));
                return;
            }
            if (m_sending) {
                out(m_lastPacket);
                kArm();
            } else {
                kSendNak(m_kSeq);
            }
            continue;
        }
        kHandlePacket(type, seq, data);
    }
}

void ModemTransfer::kTimeout()
{
    if (m_sending) {
        out(m_lastPacket);
        kArm();
    } else {
        kSendNak(m_kSeq);
    }
}
