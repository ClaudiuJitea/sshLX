#include "tftpserver.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

namespace {
enum { RRQ = 1, WRQ = 2, DATA = 3, ACK = 4, ERROR = 5, OACK = 6 };
enum {
    ErrUndefined = 0, ErrNotFound = 1, ErrAccess = 2, ErrDiskFull = 3, ErrIllegal = 4,
    ErrExists = 6, ErrOption = 8
};

QByteArray u16(int v)
{
    QByteArray b(2, '\0');
    b[0] = char((v >> 8) & 0xff);
    b[1] = char(v & 0xff);
    return b;
}

int readU16(const QByteArray &b, int at)
{
    return (uchar(b[at]) << 8) | uchar(b[at + 1]);
}
} // namespace

struct TftpServer::Transfer {
    QUdpSocket *socket = nullptr;
    QTimer *timer = nullptr;
    QHostAddress peer;
    quint16 peerPort = 0;
    std::unique_ptr<QFile> file;
    QString name, finalPath, partPath;
    bool upload = false, finishedSending = false, overwrite = false;
    int blockSize = 512;
    quint16 block = 0;         // last block sent or acknowledged
    QByteArray lastPacket;
    int retries = 0;
    qint64 bytes = 0;
};

TftpServer::TftpServer(QObject *parent) : QObject(parent) {}

TftpServer::~TftpServer()
{
    stop();
}

QString TftpServer::resolve(const QString &root, const QString &requested)
{
    QString r = requested;
    r.replace('\\', '/');   // Windows-minded devices
    while (r.startsWith('/'))
        r.remove(0, 1);
    if (r.isEmpty() || r.contains(QChar(0)))
        return {};
    for (const QString &part : r.split('/'))
        if (part == "..")
            return {};
    const QString base = QFileInfo(root).canonicalFilePath();
    if (base.isEmpty())
        return {};
    const QString path = QDir(base).filePath(r);
    // A link inside the folder must not lead outside it. For a new file,
    // check the folder that would hold it.
    QFileInfo fi(path);
    const QString real = fi.exists() ? fi.canonicalFilePath()
                                     : QFileInfo(fi.path()).canonicalFilePath();
    if (real.isEmpty() || (real != base && !real.startsWith(base + '/')))
        return {};
    if (fi.isSymLink() && !fi.exists())
        return {};   // a dangling link: writing through it would escape the check
    return path;
}

bool TftpServer::start(const Options &options, QString *error)
{
    stop();
    if (!QFileInfo(options.root).isDir()) {
        if (error)
            *error = tr("The folder %1 does not exist.").arg(options.root);
        return false;
    }
    m_opts = options;
    auto *socket = new QUdpSocket(this);
    if (!socket->bind(options.address, options.port)) {
        if (error)
            *error = socket->errorString();
        delete socket;
        return false;
    }
    m_socket = socket;
    connect(socket, &QUdpSocket::readyRead, this, &TftpServer::onRequest);
    emit logLine(tr("Serving %1 on %2 port %3").arg(options.root, options.address.toString())
                     .arg(socket->localPort()));
    return true;
}

void TftpServer::stop()
{
    if (!m_socket)
        return;
    while (!m_transfers.isEmpty())
        finish(m_transfers.first(), false, tr("server stopped"));
    delete m_socket;
    m_socket = nullptr;
    emit logLine(tr("Stopped"));
}

quint16 TftpServer::port() const
{
    return m_socket ? m_socket->localPort() : 0;
}

void TftpServer::onRequest()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram d = m_socket->receiveDatagram();
        handleRequest(d.data(), d.senderAddress(), quint16(d.senderPort()));
    }
}

void TftpServer::sendError(QUdpSocket *socket, const QHostAddress &peer, quint16 port, int code,
                           const QString &message)
{
    QByteArray p = u16(ERROR) + u16(code) + message.toUtf8();
    p.append('\0');
    socket->writeDatagram(p, peer, port);
}

void TftpServer::handleRequest(const QByteArray &packet, const QHostAddress &peer, quint16 port)
{
    if (packet.size() < 4)
        return;
    const int op = readU16(packet, 0);
    if (op != RRQ && op != WRQ)
        return;      // anything else belongs to a transfer's own socket
    const QString who = peer.toString();
    const QList<QByteArray> fields = packet.mid(2).split('\0');
    if (fields.size() < 2) {
        sendError(m_socket, peer, port, ErrIllegal, tr("Malformed request"));
        return;
    }
    const QString name = QString::fromUtf8(fields[0]);
    const QByteArray mode = fields[1].toLower();
    if (mode != "octet" && mode != "netascii") {
        sendError(m_socket, peer, port, ErrIllegal, tr("Only octet and netascii are supported"));
        return;
    }
    if (m_transfers.size() >= kMaxTransfers) {
        sendError(m_socket, peer, port, ErrUndefined, tr("Too many transfers"));
        return;
    }
    // A client repeats its request until answered; that is not a second transfer.
    for (const Transfer *t : std::as_const(m_transfers))
        if (t->peer == peer && t->peerPort == port && t->name == name)
            return;
    const QString path = resolve(m_opts.root, name);
    if (path.isEmpty()) {
        emit logLine(tr("%1: refused %2 (outside the folder)").arg(who, name));
        sendError(m_socket, peer, port, ErrAccess, tr("Access violation"));
        return;
    }

    const bool upload = op == WRQ;
    if (upload && !m_opts.allowUploads) {
        emit logLine(tr("%1: refused upload of %2 (uploads are off)").arg(who, name));
        sendError(m_socket, peer, port, ErrAccess, tr("Uploads are not allowed"));
        return;
    }
    const bool exists = QFileInfo::exists(path);
    if (upload) {
        // Two uploads of one file would write over each other, and a planted
        // link in place of the part file would lead out of the folder.
        for (const Transfer *t : std::as_const(m_transfers)) {
            if (t->upload && t->finalPath == path) {
                sendError(m_socket, peer, port, ErrAccess, tr("That file is being uploaded"));
                return;
            }
        }
        if (QFileInfo(path + QStringLiteral(".tftp-part")).isSymLink()) {
            sendError(m_socket, peer, port, ErrAccess, tr("Access violation"));
            return;
        }
    }
    if (upload && exists && !m_opts.allowOverwrite) {
        emit logLine(tr("%1: refused upload of %2 (it exists)").arg(who, name));
        sendError(m_socket, peer, port, ErrExists, tr("File already exists"));
        return;
    }
    if (!upload && (!exists || !QFileInfo(path).isFile())) {
        emit logLine(tr("%1: %2 not found").arg(who, name));
        sendError(m_socket, peer, port, ErrNotFound, tr("File not found"));
        return;
    }

    auto *t = new Transfer;
    t->peer = peer;
    t->peerPort = port;
    t->name = name;
    t->upload = upload;
    t->overwrite = exists;
    t->finalPath = path;
    if (upload) {
        t->partPath = path + QStringLiteral(".tftp-part");
        t->file = std::make_unique<QFile>(t->partPath);
        if (!t->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            sendError(m_socket, peer, port, ErrAccess, t->file->errorString());
            delete t;
            return;
        }
    } else {
        t->file = std::make_unique<QFile>(path);
        if (!t->file->open(QIODevice::ReadOnly)) {
            sendError(m_socket, peer, port, ErrAccess, t->file->errorString());
            delete t;
            return;
        }
    }

    // Each transfer has its own port, as the protocol requires.
    t->socket = new QUdpSocket(this);
    if (!t->socket->bind(m_opts.address, 0)) {
        sendError(m_socket, peer, port, ErrUndefined, t->socket->errorString());
        if (upload) {
            t->file->close();
            QFile::remove(t->partPath);
        }
        delete t->socket;
        delete t;
        return;
    }
    t->timer = new QTimer(this);
    t->timer->setInterval(m_opts.retransmitMs);
    connect(t->timer, &QTimer::timeout, this, [this, t] { resend(t); });
    connect(t->socket, &QUdpSocket::readyRead, this, [this, t] { onTransferData(t); });
    m_transfers.append(t);
    emit logLine(tr("%1: %2 %3").arg(who, upload ? tr("receiving") : tr("sending"), name));

    // Options, answered with an OACK that holds the ones accepted.
    QByteArray oack = u16(OACK);
    bool haveOptions = false;
    for (int i = 2; i + 1 < fields.size(); i += 2) {
        const QByteArray opt = fields[i].toLower();
        const QByteArray value = fields[i + 1];
        bool ok = false;
        if (opt == "blksize") {
            const int v = value.toInt(&ok);
            if (ok && v >= 8) {
                t->blockSize = qMin(v, 65464);
                oack += "blksize" + QByteArray(1, '\0') + QByteArray::number(t->blockSize) +
                        QByteArray(1, '\0');
                haveOptions = true;
            }
        } else if (opt == "tsize") {
            const qint64 size = upload ? value.toLongLong(&ok) : t->file->size();
            oack += "tsize" + QByteArray(1, '\0') + QByteArray::number(size) + QByteArray(1, '\0');
            haveOptions = true;
        } else if (opt == "timeout") {
            const int v = value.toInt(&ok);
            if (ok && v >= 1 && v <= 255) {
                t->timer->setInterval(v * 1000);
                oack += "timeout" + QByteArray(1, '\0') + value + QByteArray(1, '\0');
                haveOptions = true;
            }
        }
    }

    if (haveOptions) {
        t->lastPacket = oack;
        t->socket->writeDatagram(oack, peer, port);
        t->timer->start();
    } else if (upload) {
        t->lastPacket = u16(ACK) + u16(0);
        t->socket->writeDatagram(t->lastPacket, peer, port);
        t->timer->start();
    } else {
        sendNextBlock(t);
    }
}

void TftpServer::sendNextBlock(Transfer *t)
{
    const QByteArray data = t->file->read(t->blockSize);
    t->block++;     // wraps at 65536, as big images need
    t->bytes += data.size();
    t->lastPacket = u16(DATA) + u16(t->block) + data;
    t->retries = 0;
    t->finishedSending = data.size() < t->blockSize;
    t->socket->writeDatagram(t->lastPacket, t->peer, t->peerPort);
    t->timer->start();
}

void TftpServer::resend(Transfer *t)
{
    if (++t->retries > m_opts.maxRetries) {
        finish(t, false, tr("timed out"));
        return;
    }
    t->socket->writeDatagram(t->lastPacket, t->peer, t->peerPort);
}

void TftpServer::onTransferData(Transfer *t)
{
    while (t->socket->hasPendingDatagrams()) {
        const QNetworkDatagram d = t->socket->receiveDatagram();
        if (d.senderAddress() != t->peer || quint16(d.senderPort()) != t->peerPort) {
            // Another host, or a stale port: RFC 1350's "unknown TID".
            sendError(t->socket, d.senderAddress(), quint16(d.senderPort()), 5,
                      tr("Unknown transfer ID"));
            continue;
        }
        const QByteArray p = d.data();
        if (p.size() < 4)
            continue;
        const int op = readU16(p, 0);
        if (op == ERROR) {
            finish(t, false, tr("the client gave up: %1")
                                 .arg(QString::fromUtf8(p.mid(4).split('\0').value(0))));
            return;
        }
        if (t->upload) {
            if (op != DATA)
                continue;
            const quint16 block = quint16(readU16(p, 2));
            if (block == quint16(t->block + 1)) {
                const QByteArray data = p.mid(4);
                if (t->file->write(data) != data.size()) {
                    sendError(t->socket, t->peer, t->peerPort, ErrDiskFull, t->file->errorString());
                    finish(t, false, t->file->errorString());
                    return;
                }
                t->block = block;
                t->bytes += data.size();
                t->retries = 0;
                t->lastPacket = u16(ACK) + u16(block);
                t->socket->writeDatagram(t->lastPacket, t->peer, t->peerPort);
                if (data.size() < t->blockSize) {
                    finish(t, true, QString());
                    return;
                }
                t->timer->start();
            } else if (block == t->block) {
                // Our ACK was lost.
                t->socket->writeDatagram(t->lastPacket, t->peer, t->peerPort);
            }
        } else {
            if (op != ACK)
                continue;
            const quint16 block = quint16(readU16(p, 2));
            if (block != t->block)
                continue;     // a duplicate: answering it would double the traffic
            if (t->finishedSending) {
                finish(t, true, QString());
                return;
            }
            sendNextBlock(t);
        }
    }
}

void TftpServer::finish(Transfer *t, bool ok, const QString &message)
{
    m_transfers.removeOne(t);
    t->timer->stop();
    t->file->close();
    if (t->upload) {
        if (ok) {
            if (t->overwrite)
                QFile::remove(t->finalPath);
            ok = QFile::rename(t->partPath, t->finalPath);
        }
        if (!ok)
            QFile::remove(t->partPath);
    }
    const QString who = t->peer.toString();
    if (ok)
        emit logLine(tr("%1: finished %2 (%n byte(s))", nullptr, int(t->bytes)).arg(who, t->name));
    else
        emit logLine(tr("%1: %2 failed: %3").arg(who, t->name, message));
    emit transferFinished(who, t->name, t->upload, ok, t->bytes);
    t->timer->deleteLater();
    t->socket->deleteLater();
    delete t;
}
