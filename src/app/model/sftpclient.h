// SftpClient - an SFTP version 3 client over any byte stream. It knows
// nothing about SSH: bytes go out through send() and come back through
// feed(), so the file transfer tab joins it to a session and tests to a pipe.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QFile;
class QSaveFile;

class SftpClient : public QObject {
    Q_OBJECT
public:
    struct Entry {
        QString name;
        qint64 size = 0;
        qint64 mtime = 0;
        uint perms = 0;
        bool isDir = false;
        bool isLink = false;
    };
    using Done = std::function<void(bool ok, const QString &error)>;
    using Progress = std::function<void(qint64 done, qint64 total)>;

    explicit SftpClient(QObject *parent = nullptr);
    ~SftpClient() override;

    // A server's file name, safe to join to a local folder: no path
    // separators, and not "." or "..".
    static bool isSafeName(const QString &name);
    static QString joinPath(const QString &dir, const QString &name);

    void start();
    void feed(const char *data, size_t len);
    // The stream ended: everything outstanding fails.
    void connectionLost(const QString &why);

    void realPath(const QString &path, std::function<void(bool, const QString &, const QString &)> fn);
    void list(const QString &dir, std::function<void(bool, const QString &, const QList<Entry> &)> fn);
    void stat(const QString &path, std::function<void(bool, const QString &, const Entry &)> fn);
    void makeDir(const QString &path, Done fn);
    void removeDir(const QString &path, Done fn);
    void removeFile(const QString &path, Done fn);
    void rename(const QString &from, const QString &to, Done fn);

    // One transfer at a time. A download appears at local only once complete.
    void download(const QString &remote, const QString &local, qint64 size,
                  Progress progress, Done done);
    void upload(const QString &local, const QString &remote, Progress progress, Done done);
    void cancelTransfer();
    bool transferring() const { return m_xfer != nullptr; }

signals:
    void send(const QByteArray &data);
    void ready();
    // The stream broke the protocol; the connection is no use any more.
    void protocolError(const QString &why);

private:
    class Reader;
    struct Transfer;
    using Handler = std::function<void(int type, Reader &r)>;

    quint32 request(int type, const QByteArray &body, Handler h);
    void sendPacket(int type, const QByteArray &payload);
    void handlePacket(const QByteArray &packet);
    void statusRequest(int type, const QByteArray &body, Done fn);
    void continueList(const QByteArray &handle, QList<Entry> soFar,
                      std::function<void(bool, const QString &, const QList<Entry> &)> fn);
    void closeHandle(const QByteArray &handle, Done fn);
    void pumpDownload();
    void pumpUpload();
    void finishTransfer(bool ok, const QString &error);
    static Entry readAttrs(Reader &r);

    QByteArray m_in;
    bool m_started = false, m_broken = false;
    quint32 m_nextId = 1;
    QHash<quint32, Handler> m_pending;
    std::unique_ptr<Transfer> m_xfer;
};
