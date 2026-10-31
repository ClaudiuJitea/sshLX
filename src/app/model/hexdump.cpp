#include "hexdump.h"

QString HexDump::line(qint64 offset, const QByteArray &bytes)
{
    QString hex, text;
    for (int i = 0; i < kWidth; i++) {
        if (i == kWidth / 2)
            hex += ' ';
        if (i < bytes.size()) {
            const uchar c = uchar(bytes[i]);
            hex += QStringLiteral("%1 ").arg(c, 2, 16, QChar('0'));
            text += c >= 0x20 && c < 0x7f ? QChar(c) : QChar('.');
        } else {
            hex += QStringLiteral("   ");
        }
    }
    return QStringLiteral("%1  %2 %3").arg(offset, 8, 16, QChar('0')).arg(hex, text);
}

QString HexDump::append(const QByteArray &data)
{
    m_pending += data;
    QString out;
    int at = 0;
    while (m_pending.size() - at >= kWidth) {
        out += line(m_offset, m_pending.mid(at, kWidth)) + '\n';
        m_offset += kWidth;
        at += kWidth;
    }
    m_pending.remove(0, at);
    return out;
}

QString HexDump::tail() const
{
    return m_pending.isEmpty() ? QString() : line(m_offset, m_pending);
}

void HexDump::clear()
{
    m_pending.clear();
    m_offset = 0;
}
