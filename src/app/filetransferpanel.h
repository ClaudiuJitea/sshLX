// FileTransferPanel - the file manager shown in a transfer tab, in place of
// the terminal: this computer on the left, the server on the right, and a
// queue below. Browses over SFTP. A device with no SFTP gets SCP instead:
// no listing, a typed remote path and a new connection for each file.
#pragma once

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

#include "sftpclient.h"
#include "sshlx_engine.h"

class QAction;
class QElapsedTimer;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class ScpClient;
class SessionWidget;

class FileTransferPanel : public QWidget {
    Q_OBJECT
public:
    using Configure = std::function<void(SessionWidget *)>;
    // host is the tab, already set up for the sftp subsystem. configure
    // gives the extra connections SCP needs the tab's saved logins.
    FileTransferPanel(SessionWidget *host, const sshlx_conf *conf, Configure configure,
                      QWidget *parent = nullptr);
    ~FileTransferPanel() override;

    bool busy() const;
    void uploadPaths(const QStringList &paths);
    // As soon as the connection is ready, or now if it is.
    void uploadWhenReady(const QStringList &paths);
    void setLocalDir(const QString &dir);
    QString remoteDir() const { return m_remoteDir; }
    bool scpOnly() const { return m_scpOnly; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class State { Queued, Running, Done, Failed, Skipped, Cancelled };
    struct Job {
        bool upload = false, makeDir = false;
        QString local, remote;
        qint64 size = 0;
        State state = State::Queued;
        QTreeWidgetItem *item = nullptr;
    };

    QStringList m_pendingUploads;
    void onHostState(sshlx_state st);
    // Runs fn once s can really send, or never if its connection ends first.
    void whenSendReady(SessionWidget *s, std::function<void()> fn);
    void onReady();
    void enterScpMode();
    void loadRemote(const QString &dir);
    void buildToolBar(QLayout *into);
    void updateActions();
    void setRemoteActive(bool remote);
    void applyFilter();
    void newFolder();
    void renameSelected();
    void deleteSelected();
    void deleteLocal();
    void remoteActivated(QTreeWidgetItem *item);
    void localActivated();
    void downloadSelected();
    void uploadSelected();
    void downloadPath();
    void queueDownload(const SftpClient::Entry &e, const QString &remote, const QString &localDir,
                       int depth);
    void addJob(Job job);
    void startNext();
    void runSftp(Job &job);
    void runScp(Job &job);
    void finishJob(bool ok, const QString &error);
    void setJobText(const Job &job, const QString &status);
    bool mayOverwrite(const QString &name);
    void showRemoteMenu(const QPoint &pos);
    void newRemoteFolder();
    void renameRemote(QTreeWidgetItem *item);
    void deleteRemote();
    void cancelTransfers();
    void clearFinished();
    void updateSummary();
    void note(const QString &text);
    QStringList selectedRemoteNames() const;
    QString localDir() const { return m_localDir; }

    QPointer<SessionWidget> m_host;
    sshlx_conf *m_conf;
    Configure m_configure;
    SftpClient *m_sftp;
    ScpClient *m_scp;
    bool m_sftpReady = false, m_scpOnly = false;
    QFileSystemModel *m_fsModel;
    QTreeView *m_localView;
    QLineEdit *m_localPath, *m_remotePath;
    QString m_localDir, m_remoteDir;
    QTreeWidget *m_remote;
    QStackedWidget *m_remoteStack;
    QLineEdit *m_scpPath;
    QPushButton *m_cancel;
    QLabel *m_localLabel, *m_remoteLabel;
    QAction *m_upload, *m_download, *m_refresh, *m_newFolder, *m_rename, *m_delete, *m_hidden;
    QLineEdit *m_filter;
    bool m_remoteActive = true;      // the side New Folder, Rename and Delete act on
    QTreeWidget *m_queue;
    QLabel *m_summary;
    QVBoxLayout *m_scpHost;
    QPointer<SessionWidget> m_scpSession;
    QTimer *m_scpPump;
    QElapsedTimer *m_clock;
    QList<Job> m_jobs;
    int m_current = -1;
    int m_walking = 0;       // folders still being listed for a download
    bool m_overwriteAll = false;
};
