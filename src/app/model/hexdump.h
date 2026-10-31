// HexDump - bytes as "offset  hex  text" lines, whole lines at a time however
// the bytes arrive. Qt Core only.
#pragma once

#include <QByteArray>
#include <QString>

class HexDump {
public:
    // Returns the complete lines this data finished, each ending in \n.
    QString append(const QByteArray &data);
    // The bytes of an unfinished last line, as a line of their own.
    QString tail() const;
    void clear();
    qint64 total() const { return m_offset + m_pending.size(); }

    static QString line(qint64 offset, const QByteArray &bytes);
    static constexpr int kWidth = 16;

private:
    QByteArray m_pending;
    qint64 m_offset = 0;
};
