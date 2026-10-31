// TftpServer - a small TFTP server (RFC 1350, with the blksize, tsize and
// timeout options of RFCs 2347-2349) for switches and routers that fetch
// images and configurations. Everything stays inside one folder: downloads
// by default, and uploads only when turned on.
#pragma once

#include <QFile>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class QUdpSocket;

class TftpServer : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString root;
        QHostAddress address = QHostAddress::AnyIPv4;
        quint16 port = 69;
        bool allowUploads = false;
        bool allowOverwrite = false;
        int retransmitMs = 1000;
        int maxRetries = 5;
    };

    explicit TftpServer(QObject *parent = nullptr);
    ~TftpServer() override;

    // False with *error set when the folder or the port can't be used.
    bool start(const Options &options, QString *error = nullptr);
    void stop();
    bool running() const { return m_socket != nullptr; }
    quint16 port() const;
    int activeTransfers() const { return int(m_transfers.size()); }

    // The real path for a requested name, or empty if it leads out of root.
    static QString resolve(const QString &root, const QString &requested);

    static constexpr int kMaxTransfers = 16;

signals:
    void logLine(const QString &line);
    void transferFinished(const QString &peer, const QString &file, bool upload, bool ok,
                          qint64 bytes);

private:
    struct Transfer;
    void onRequest();
    void handleRequest(const QByteArray &packet, const QHostAddress &peer, quint16 port);
    void onTransferData(Transfer *t);
    void sendNextBlock(Transfer *t);
    void resend(Transfer *t);
    void finish(Transfer *t, bool ok, const QString &message);
    void sendError(QUdpSocket *socket, const QHostAddress &peer, quint16 port, int code,
                   const QString &message);

    Options m_opts;
    QUdpSocket *m_socket = nullptr;
    QList<Transfer *> m_transfers;
};
