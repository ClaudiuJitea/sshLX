// ScpClient - one file over the classic SCP protocol, for devices that have
// no SFTP (many switches and routers). Like SftpClient it only sees a byte
// stream; the file transfer tab runs commandFor() on a new SSH connection.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QFile;
class QSaveFile;

class ScpClient : public QObject {
    Q_OBJECT
public:
    explicit ScpClient(QObject *parent = nullptr);
    ~ScpClient() override;

    // Quotes only when it must: IOS takes flash:/x but no shell quoting.
    static QString commandFor(bool upload, const QString &remotePath);
    // The name sent with an upload never comes from the server.
    void startUpload(const QString &local, const QString &remoteName);
    void startDownload(const QString &local);
    void feed(const char *data, size_t len);
    // Sends more of an upload; call whenever the link has room.
    void pump();
    void cancel();
    void connectionLost(const QString &why);

signals:
    void send(const QByteArray &data);
    void progress(qint64 done, qint64 total);
    void finished(bool ok, const QString &error);

private:
    enum class Step {
        Idle, WaitReady, WaitHeaderAck, Sending, WaitDataAck,    // upload
        WaitHeader, Receiving, WaitTrailer                       // download
    };
    void step();
    bool takeAck();
    bool takeLine(QByteArray *line);
    void finish(bool ok, const QString &error);

    Step m_step = Step::Idle;
    QByteArray m_in;
    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QSaveFile> m_out;
    QString m_name, m_ackError;
    qint64 m_total = 0, m_done = 0;
};
