// AsciiSender - types a text file into a session at a chosen pace, for
// devices that lose characters when a file is simply pasted. Qt Core only.
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

class AsciiSender : public QObject {
    Q_OBJECT
public:
    struct Options {
        int lineDelayMs = 100;
        int charDelayMs = 0;
    };
    explicit AsciiSender(QObject *parent = nullptr);

    bool start(const QString &path, const Options &options, QString *error);
    void stop();
    bool running() const { return m_timer.isActive(); }

signals:
    void send(const QByteArray &data);   // lines end in CR, as Enter does
    void progress(int line, int total);
    void finished(bool complete);

private:
    void step();

    QList<QByteArray> m_lines;
    Options m_opts;
    int m_line = 0, m_char = 0;
    QTimer m_timer;
};
