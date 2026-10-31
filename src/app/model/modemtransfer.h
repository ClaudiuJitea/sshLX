// ModemTransfer - Xmodem, Ymodem and Zmodem over a terminal session, for
// devices with no SFTP or SCP: ROM monitors, boot loaders, serial consoles.
//
// Qt Core only, and knows nothing of sessions: the owner connects send() to
// the session and hands every byte received to feed(). Nothing is sent from
// inside feed(), because the session's output callback must not send: input
// is queued and handled from the event loop.
#pragma once

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>

class ModemTransfer : public QObject {
    Q_OBJECT
public:
    enum class Protocol { Xmodem, Xmodem1k, Ymodem, Zmodem, Kermit };
    Q_ENUM(Protocol)

    explicit ModemTransfer(QObject *parent = nullptr);
    ~ModemTransfer() override;

    static QString protocolName(Protocol p);

    // Sends the files in order. Xmodem carries no name, so it takes one file.
    // With resume, a Zmodem receiver that holds the start of a file carries on
    // after it, as "sz -r" does.
    void startSend(Protocol p, const QStringList &files, bool resume = false);
    // Saves into directory under the sender's names, never over an existing
    // file. Xmodem carries no name: saveAs names its file, and Xmodem also
    // cannot tell the file's length, so the last block keeps its padding.
    // Zmodem keeps what arrived of an interrupted file as NAME.part. When the
    // same file comes again, the sender's checksum of that part decides
    // whether it continues from there or starts over.
    void startReceive(Protocol p, const QString &directory, const QString &saveAs = QString());
    void cancel();

    bool running() const { return m_running; }
    void feed(const char *data, size_t len);
    void connectionLost();
    // Bytes queued in the session, so a slow line sets the pace.
    void setBacklog(std::function<size_t()> f) { m_backlog = std::move(f); }
    // Stops waiting for a reply after this long; tests shorten it.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    QStringList savedFiles() const { return m_saved; }

    // A file name safe to create in a directory: the last path component,
    // with anything that could reach outside it removed. Empty if none is left.
    static QString safeFileName(const QString &remoteName);
    // directory/name, or "name (1).ext" and so on if that exists.
    static QString uniquePath(const QString &directory, const QString &name);
    // Where a Zmodem start header begins in data, or -1. *receiving is true
    // when the remote is about to send (sz), false when it waits for us (rz).
    static int findZmodemStart(const QByteArray &data, bool *receiving);

signals:
    void send(const QByteArray &data);
    void progress(const QString &file, qint64 done, qint64 total);
    void finished(bool ok, const QString &message);

private:
    enum class Phase {
        Idle,
        // Xmodem and Ymodem
        XSendWaitStart, XSendWaitAck, XSendWaitEotAck, XSendWaitHeaderAck,
        XSendWaitDataStart, XSendWaitEndAck,
        XRecvBlock,
        // Zmodem
        ZSendInit, ZSendFileAck, ZSendData, ZSendWaitAck, ZSendEofAck, ZSendFin,
        ZRecvHeader, ZRecvFileInfo, ZRecvData, ZRecvSinit, ZRecvFin, ZRecvResumeCheck,
        // Kermit
        KSendInit, KSendFile, KSendData, KSendEof, KSendBreak, KRecv
    };

    void fail(const QString &message);
    void succeed(const QString &message);
    void stopAll();
    void process();
    void onTimeout();
    void armTimeout();
    void out(const QByteArray &data);
    void reportProgress();
    bool openNextSendFile();
    bool openReceiveFile(const QString &name, qint64 size, qint64 resumeAt = 0);
    bool finishReceiveFile();
    void discardReceiveFile();

    // Xmodem and Ymodem
    void xProcess();
    void xSendByte(uchar c);
    void xSendBlock();
    void xSendHeader(bool last);
    void xSendEot();
    void xRecvPoke();
    bool xRecvHandleBlock(const QByteArray &block, int blockNo);
    bool xFillBlock();

    // Zmodem framing
    struct ZHeader {
        int type = -1;
        uchar d[4] = {0, 0, 0, 0};
        quint32 position() const;
    };
    void zStartSend();
    void zStartReceive();
    void zProcess();
    void zByte(uchar c);
    void zHandleHeader(const ZHeader &h);
    void zHandleSubpacket(const QByteArray &data, uchar end);
    void zSendHex(int type, const uchar d[4]);
    void zSendHexPos(int type, quint32 pos);
    void zSendBin(int type, const uchar d[4]);
    void zSendSubpacket(const QByteArray &data, uchar end);
    void zEscape(QByteArray &out, uchar c);
    void zSendFileHeader();
    void zPumpData();
    void zFinishSending();
    void zExpectData();

    // Kermit
    void kStart();
    void kProcess();
    void kTimeout();
    void kArm();
    void kSend(char type, int seq, const QByteArray &data);
    void kSendNak(int seq);
    void kApplyParams(const QByteArray &params);
    QByteArray kEncodeByte(uchar b) const;
    QByteArray kDecode(const QByteArray &data) const;
    void kFileHeader();
    void kNextData();
    void kAfterAck(const QByteArray &data);
    void kHandlePacket(char type, int seq, const QByteArray &data);

    Protocol m_protocol = Protocol::Xmodem;
    bool m_running = false, m_sending = false;
    Phase m_phase = Phase::Idle;
    QByteArray m_in;
    QTimer m_processTimer, m_timeout, m_pumpTimer;
    int m_timeoutMs = 20000;
    int m_retries = 0;
    std::function<size_t()> m_backlog;

    QStringList m_files;
    QString m_directory, m_saveAs;
    QStringList m_saved;
    std::unique_ptr<QFile> m_file;
    QString m_partPath, m_finalPath, m_fileName;
    qint64 m_fileSize = -1, m_position = 0, m_totalFiles = 0, m_filesDone = 0;

    // Xmodem and Ymodem
    bool m_crc = false, m_eotSeen = false;
    int m_block = 1, m_blockSize = 128, m_receivedBlocks = 0;
    QByteArray m_lastPacket;
    bool m_headerExpected = false;
    int m_cans = 0;

    // Zmodem parser
    enum class ZParse {
        Hunt, AfterPad, AfterZdle, Hex, BinHeader, BinHeaderCrc, SubData, SubCrc
    };
    ZParse m_zp = ZParse::Hunt;
    bool m_zEscaped = false;
    QByteArray m_zBuf, m_zCrcTail;
    int m_zFrameFormat = 0;       // the framing of the header that began this data
    uchar m_zSubEnd = 0;
    int m_zCans = 0;
    bool m_peerCrc32 = false, m_peerEscCtl = false;
    int m_peerBuffer = 0;
    bool m_useBin32 = false;
    uchar m_zLast = 0;
    qint64 m_zAcked = 0;
    bool m_zResume = false;
    QString m_zResumeName;
    qint64 m_zResumeSize = 0, m_zResumeAt = 0;

    static constexpr int kMaxKermitRetries = 10;
    int m_kSeq = 0, m_kMaxData = 89;
    char m_kQctl = '#', m_kQbin = 0, m_kRept = 0;
};
