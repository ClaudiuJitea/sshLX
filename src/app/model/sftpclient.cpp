#include "sftpclient.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <limits>
#include <utility>

namespace {
enum {
    FXP_INIT = 1, FXP_VERSION = 2, FXP_OPEN = 3, FXP_CLOSE = 4, FXP_READ = 5, FXP_WRITE = 6,
    FXP_OPENDIR = 11, FXP_READDIR = 12, FXP_REMOVE = 13, FXP_MKDIR = 14, FXP_RMDIR = 15,
    FXP_REALPATH = 16, FXP_STAT = 17, FXP_RENAME = 18,
    FXP_STATUS = 101, FXP_HANDLE = 102, FXP_DATA = 103, FXP_NAME = 104, FXP_ATTRS = 105
};
enum { FX_OK = 0, FX_EOF = 1, FX_NO_SUCH_FILE = 2, FX_PERMISSION_DENIED = 3 };
enum { ATTR_SIZE = 1, ATTR_UIDGID = 2, ATTR_PERMS = 4, ATTR_TIME = 8, ATTR_EXTENDED = 0x80000000u };
enum { OPEN_READ = 1, OPEN_WRITE = 2, OPEN_CREATE = 8, OPEN_TRUNCATE = 16 };

constexpr int kVersion = 3;
constexpr int kChunk = 32768;
constexpr int kWindow = 16;                  // requests in flight per transfer
constexpr quint32 kMaxPacket = 1024 * 1024;  // refuse anything larger

void putU32(QByteArray &b, quint32 v)
{
    const char c[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)};
    b.append(c, 4);
}
void putU64(QByteArray &b, quint64 v)
{
    putU32(b, quint32(v >> 32));
    putU32(b, quint32(v));
}
void putBytes(QByteArray &b, const QByteArray &s)
{
    putU32(b, quint32(s.size()));
    b.append(s);
}
void putPath(QByteArray &b, const QString &s) { putBytes(b, s.toUtf8()); }
} // namespace

// Bounds-checked: past the end it sets failed and yields zeroes.
class SftpClient::Reader {
public:
    explicit Reader(const QByteArray &d, int pos = 0) : m_d(d), m_pos(pos) {}
    bool failed() const { return m_failed; }
    int remaining() const { return int(m_d.size()) - m_pos; }
    quint32 u32()
    {
        if (remaining() < 4) {
            m_failed = true;
            return 0;
        }
        auto *p = reinterpret_cast<const uchar *>(m_d.constData() + m_pos);
        m_pos += 4;
        return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | p[3];
    }
    quint64 u64()
    {
        const quint64 hi = u32();
        return hi << 32 | u32();
    }
    QByteArray bytes()
    {
        const quint32 n = u32();
        if (m_failed || n > quint32(remaining())) {
            m_failed = true;
            return {};
        }
        QByteArray r = m_d.mid(m_pos, int(n));
        m_pos += int(n);
        return r;
    }
    QString text() { return QString::fromUtf8(bytes()); }

private:
    const QByteArray &m_d;
    int m_pos;
    bool m_failed = false;
};

struct SftpClient::Transfer {
    bool upload = false;
    bool cancelled = false, failed = false, eof = false, closing = false;
    QString error, remote;
    QByteArray handle;
    std::unique_ptr<QSaveFile> out;
    std::unique_ptr<QFile> in;
    qint64 total = 0, done = 0, offset = 0;
    int inFlight = 0;
    QList<QPair<qint64, int>> gaps;   // short reads still to ask for
    Progress progress;
    Done finished;
};

static QString statusText(quint32 code, const QString &message)
{
    if (!message.isEmpty())
        return message;
    switch (code) {
    case FX_NO_SUCH_FILE: return QStringLiteral("No such file");
    case FX_PERMISSION_DENIED: return QStringLiteral("Permission denied");
    default: return QStringLiteral("Server error %1").arg(code);
    }
}

SftpClient::SftpClient(QObject *parent) : QObject(parent) {}
SftpClient::~SftpClient() = default;

bool SftpClient::isSafeName(const QString &name)
{
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/')
        && !name.contains('\\') && !name.contains(QChar(0));
}

QString SftpClient::joinPath(const QString &dir, const QString &name)
{
    return dir.endsWith('/') ? dir + name : dir + '/' + name;
}

void SftpClient::start()
{
    m_in.clear();
    m_pending.clear();
    m_started = true;
    m_broken = false;
    QByteArray b;
    putU32(b, kVersion);
    sendPacket(FXP_INIT, b);
}

void SftpClient::sendPacket(int type, const QByteArray &payload)
{
    QByteArray b;
    b.reserve(payload.size() + 5);
    putU32(b, quint32(payload.size() + 1));
    b.append(char(type));
    b.append(payload);
    emit send(b);
}

quint32 SftpClient::request(int type, const QByteArray &body, Handler h)
{
    const quint32 id = m_nextId++;
    m_pending.insert(id, std::move(h));
    QByteArray b;
    putU32(b, id);
    b.append(body);
    sendPacket(type, b);
    return id;
}

void SftpClient::feed(const char *data, size_t len)
{
    if (m_broken)
        return;
    m_in.append(data, qsizetype(len));
    while (m_in.size() >= 4) {
        Reader r(m_in);
        const quint32 n = r.u32();
        if (n == 0 || n > kMaxPacket) {
            m_broken = true;
            connectionLost(tr("The server sent something that is not SFTP."));
            emit protocolError(tr("The server sent something that is not SFTP."));
            return;
        }
        if (quint32(m_in.size()) - 4 < n)
            return;
        const QByteArray packet = m_in.mid(4, n);
        m_in.remove(0, qsizetype(n) + 4);
        handlePacket(packet);
        if (m_broken)
            return;
    }
}

void SftpClient::handlePacket(const QByteArray &packet)
{
    const int type = uchar(packet[0]);
    Reader r(packet, 1);
    if (type == FXP_VERSION) {
        if (r.u32() < 3) {
            m_broken = true;
            emit protocolError(tr("The server's SFTP version is too old."));
            return;
        }
        emit ready();
        return;
    }
    const quint32 id = r.u32();
    auto it = m_pending.find(id);
    if (r.failed() || it == m_pending.end()) {
        m_broken = true;
        connectionLost(tr("The server's reply makes no sense."));
        emit protocolError(tr("The server's reply makes no sense."));
        return;
    }
    Handler h = std::move(it.value());
    m_pending.erase(it);
    h(type, r);
}

void SftpClient::connectionLost(const QString &why)
{
    const auto pending = std::exchange(m_pending, {});
    Q_UNUSED(pending);   // their callers learn through finishTransfer or the tab
    if (m_xfer)
        finishTransfer(false, why);
}

void SftpClient::statusRequest(int type, const QByteArray &body, Done fn)
{
    request(type, body, [fn](int t, Reader &r) {
        if (t != FXP_STATUS) {
            fn(false, QStringLiteral("Unexpected reply"));
            return;
        }
        const quint32 code = r.u32();
        const QString msg = r.text();
        fn(code == FX_OK, code == FX_OK ? QString() : statusText(code, msg));
    });
}

void SftpClient::closeHandle(const QByteArray &handle, Done fn)
{
    QByteArray b;
    putBytes(b, handle);
    statusRequest(FXP_CLOSE, b, std::move(fn));
}

SftpClient::Entry SftpClient::readAttrs(Reader &r)
{
    Entry e;
    const quint32 flags = r.u32();
    if (flags & ATTR_SIZE)
        e.size = qint64(qMin(r.u64(), quint64(std::numeric_limits<qint64>::max())));
    if (flags & ATTR_UIDGID) {
        r.u32();
        r.u32();
    }
    if (flags & ATTR_PERMS) {
        e.perms = r.u32();
        e.isDir = (e.perms & 0170000) == 0040000;
        e.isLink = (e.perms & 0170000) == 0120000;
    }
    if (flags & ATTR_TIME) {
        r.u32();
        e.mtime = r.u32();
    }
    if (flags & ATTR_EXTENDED) {
        const quint32 n = r.u32();
        for (quint32 i = 0; i < n && !r.failed(); i++) {
            r.bytes();
            r.bytes();
        }
    }
    return e;
}

void SftpClient::realPath(const QString &path,
                          std::function<void(bool, const QString &, const QString &)> fn)
{
    QByteArray b;
    putPath(b, path);
    request(FXP_REALPATH, b, [fn](int t, Reader &r) {
        if (t == FXP_NAME && r.u32() >= 1) {
            fn(true, QString(), r.text());
            return;
        }
        fn(false, QStringLiteral("The server could not resolve the path"), QString());
    });
}

void SftpClient::stat(const QString &path,
                      std::function<void(bool, const QString &, const Entry &)> fn)
{
    QByteArray b;
    putPath(b, path);
    request(FXP_STAT, b, [fn](int t, Reader &r) {
        if (t == FXP_ATTRS) {
            fn(true, QString(), readAttrs(r));
            return;
        }
        const quint32 code = r.u32();
        fn(false, statusText(code, r.text()), Entry());
    });
}

void SftpClient::list(const QString &dir,
                      std::function<void(bool, const QString &, const QList<Entry> &)> fn)
{
    QByteArray b;
    putPath(b, dir);
    request(FXP_OPENDIR, b, [this, fn](int t, Reader &r) {
        if (t != FXP_HANDLE) {
            const quint32 code = r.u32();
            fn(false, statusText(code, r.text()), {});
            return;
        }
        continueList(r.bytes(), {}, fn);
    });
}

void SftpClient::continueList(const QByteArray &handle, QList<Entry> soFar,
                              std::function<void(bool, const QString &, const QList<Entry> &)> fn)
{
    QByteArray b;
    putBytes(b, handle);
    request(FXP_READDIR, b, [this, handle, soFar, fn](int t, Reader &r) mutable {
        if (t == FXP_NAME) {
            const quint32 n = r.u32();
            for (quint32 i = 0; i < n && !r.failed(); i++) {
                const QString name = r.text();
                r.bytes();           // the long form, ls -l style
                Entry e = readAttrs(r);
                e.name = name;
                if (!r.failed() && isSafeName(name))
                    soFar.append(e);
            }
            continueList(handle, soFar, fn);
            return;
        }
        const quint32 code = r.u32();
        const QString msg = statusText(code, r.text());
        const bool ok = code == FX_EOF;
        closeHandle(handle, [](bool, const QString &) {});
        fn(ok, ok ? QString() : msg, soFar);
    });
}

void SftpClient::makeDir(const QString &path, Done fn)
{
    QByteArray b;
    putPath(b, path);
    putU32(b, 0);        // no attributes
    statusRequest(FXP_MKDIR, b, std::move(fn));
}

void SftpClient::removeDir(const QString &path, Done fn)
{
    QByteArray b;
    putPath(b, path);
    statusRequest(FXP_RMDIR, b, std::move(fn));
}

void SftpClient::removeFile(const QString &path, Done fn)
{
    QByteArray b;
    putPath(b, path);
    statusRequest(FXP_REMOVE, b, std::move(fn));
}

void SftpClient::rename(const QString &from, const QString &to, Done fn)
{
    QByteArray b;
    putPath(b, from);
    putPath(b, to);
    statusRequest(FXP_RENAME, b, std::move(fn));
}

void SftpClient::download(const QString &remote, const QString &local, qint64 size,
                          Progress progress, Done done)
{
    if (m_xfer) {
        done(false, tr("A transfer is already running."));
        return;
    }
    auto x = std::make_unique<Transfer>();
    x->out = std::make_unique<QSaveFile>(local);
    if (!x->out->open(QIODevice::WriteOnly)) {
        done(false, x->out->errorString());
        return;
    }
    x->remote = remote;
    x->total = size;
    x->progress = std::move(progress);
    x->finished = std::move(done);
    m_xfer = std::move(x);

    QByteArray b;
    putPath(b, remote);
    putU32(b, OPEN_READ);
    putU32(b, 0);
    request(FXP_OPEN, b, [this](int t, Reader &r) {
        if (!m_xfer)
            return;
        if (t != FXP_HANDLE) {
            const quint32 code = r.u32();
            finishTransfer(false, statusText(code, r.text()));
            return;
        }
        m_xfer->handle = r.bytes();
        pumpDownload();
    });
}

void SftpClient::pumpDownload()
{
    Transfer *x = m_xfer.get();
    if (!x)
        return;
    const bool stopping = x->cancelled || x->failed;
    while (!stopping && x->inFlight < kWindow && (!x->eof || !x->gaps.isEmpty())) {
        qint64 offset;
        int want = kChunk;
        if (!x->gaps.isEmpty()) {
            offset = x->gaps.first().first;
            want = x->gaps.takeFirst().second;
        } else {
            offset = x->offset;
            x->offset += kChunk;
        }
        QByteArray b;
        putBytes(b, x->handle);
        putU64(b, quint64(offset));
        putU32(b, quint32(want));
        x->inFlight++;
        request(FXP_READ, b, [this, offset, want](int t, Reader &r) {
            Transfer *x = m_xfer.get();
            if (!x)
                return;
            x->inFlight--;
            if (t == FXP_DATA) {
                const QByteArray data = r.bytes();
                if (data.isEmpty() || data.size() > want) {
                    x->failed = true;
                    x->error = tr("The server's reply makes no sense.");
                } else if (!x->out->seek(offset) || x->out->write(data) != data.size()) {
                    x->failed = true;
                    x->error = x->out->errorString();
                } else {
                    x->done += data.size();
                    if (data.size() < want)
                        x->gaps.append({offset + data.size(), want - int(data.size())});
                    if (x->progress)
                        x->progress(x->done, qMax(x->total, x->done));
                }
            } else {
                const quint32 code = r.u32();
                const QString msg = r.text();
                if (code == FX_EOF) {
                    x->eof = true;
                } else if (!x->failed) {
                    x->failed = true;
                    x->error = statusText(code, msg);
                }
            }
            pumpDownload();
        });
    }
    if (x->inFlight == 0 && (stopping || (x->eof && x->gaps.isEmpty())) && !x->closing) {
        x->closing = true;
        closeHandle(x->handle, [this](bool, const QString &) {
            Transfer *x = m_xfer.get();
            if (x)
                finishTransfer(!x->failed && !x->cancelled, x->error);
        });
    }
}

void SftpClient::upload(const QString &local, const QString &remote, Progress progress, Done done)
{
    if (m_xfer) {
        done(false, tr("A transfer is already running."));
        return;
    }
    auto x = std::make_unique<Transfer>();
    x->upload = true;
    x->in = std::make_unique<QFile>(local);
    if (!x->in->open(QIODevice::ReadOnly)) {
        done(false, x->in->errorString());
        return;
    }
    x->remote = remote;
    x->total = x->in->size();
    x->progress = std::move(progress);
    x->finished = std::move(done);

    // The same rwx bits as the local file.
    const QFileDevice::Permissions p = x->in->permissions();
    uint mode = 0;
    if (p & QFileDevice::ReadOwner) mode |= 0400;
    if (p & QFileDevice::WriteOwner) mode |= 0200;
    if (p & QFileDevice::ExeOwner) mode |= 0100;
    if (p & QFileDevice::ReadGroup) mode |= 0040;
    if (p & QFileDevice::WriteGroup) mode |= 0020;
    if (p & QFileDevice::ExeGroup) mode |= 0010;
    if (p & QFileDevice::ReadOther) mode |= 0004;
    if (p & QFileDevice::WriteOther) mode |= 0002;
    if (p & QFileDevice::ExeOther) mode |= 0001;
    m_xfer = std::move(x);

    QByteArray b;
    putPath(b, remote);
    putU32(b, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    putU32(b, ATTR_PERMS);
    putU32(b, mode);
    request(FXP_OPEN, b, [this](int t, Reader &r) {
        if (!m_xfer)
            return;
        if (t != FXP_HANDLE) {
            const quint32 code = r.u32();
            finishTransfer(false, statusText(code, r.text()));
            return;
        }
        m_xfer->handle = r.bytes();
        pumpUpload();
    });
}

void SftpClient::pumpUpload()
{
    Transfer *x = m_xfer.get();
    if (!x)
        return;
    const bool stopping = x->cancelled || x->failed;
    while (!stopping && !x->eof && x->inFlight < kWindow) {
        const QByteArray data = x->in->read(kChunk);
        if (data.isEmpty()) {
            if (x->in->error() != QFileDevice::NoError) {
                x->failed = true;
                x->error = x->in->errorString();
            }
            x->eof = true;
            break;
        }
        const qint64 offset = x->offset;
        x->offset += data.size();
        QByteArray b;
        putBytes(b, x->handle);
        putU64(b, quint64(offset));
        putBytes(b, data);
        const int n = int(data.size());
        x->inFlight++;
        request(FXP_WRITE, b, [this, n](int t, Reader &r) {
            Transfer *x = m_xfer.get();
            if (!x)
                return;
            x->inFlight--;
            const quint32 code = t == FXP_STATUS ? r.u32() : quint32(-1);
            const QString msg = r.text();
            if (code != FX_OK) {
                if (!x->failed) {
                    x->failed = true;
                    x->error = statusText(code, msg);
                }
            } else {
                x->done += n;
                if (x->progress)
                    x->progress(x->done, x->total);
            }
            pumpUpload();
        });
    }
    if (x->inFlight == 0 && (x->cancelled || x->failed || x->eof) && !x->closing) {
        x->closing = true;
        closeHandle(x->handle, [this](bool ok, const QString &err) {
            Transfer *x = m_xfer.get();
            if (!x)
                return;
            if (!ok && !x->failed && !x->cancelled) {    // the last write can fail here
                x->failed = true;
                x->error = err;
            }
            if (x->cancelled || x->failed) {
                // Half a file is worse than none.
                const bool cancelled = x->cancelled;
                const QString error = x->error;
                removeFile(x->remote, [this, cancelled, error](bool, const QString &) {
                    finishTransfer(false, cancelled ? QString() : error);
                });
                return;
            }
            finishTransfer(true, QString());
        });
    }
}

void SftpClient::cancelTransfer()
{
    Transfer *x = m_xfer.get();
    if (!x || x->cancelled)
        return;
    x->cancelled = true;
    if (x->handle.isEmpty())
        return;              // the open reply will finish it
    if (x->upload)
        pumpUpload();
    else
        pumpDownload();
}

void SftpClient::finishTransfer(bool ok, const QString &error)
{
    std::unique_ptr<Transfer> x = std::move(m_xfer);
    if (!x)
        return;
    QString err = error;
    if (x->out) {
        if (ok && !x->out->commit()) {
            ok = false;
            err = x->out->errorString();
        } else if (!ok) {
            x->out->cancelWriting();
        }
    }
    if (x->cancelled)
        err.clear();
    x->finished(ok, err);
}
