// QtHostLoop - implements the engine's sshlx_host_loop with Qt primitives:
// QSocketNotifier for fd readiness, a single-shot QTimer for PuTTY's timer
// and a queued invocation for "run pending callbacks". It does not depend on
// which event dispatcher Qt happens to use.
#pragma once

#include <QHash>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

#include "sshlx_engine.h"

class QtHostLoop : public QObject {
    Q_OBJECT
public:
    explicit QtHostLoop(QObject *parent = nullptr);
    ~QtHostLoop() override;

    // Initialise the engine with this loop. Returns false on failure.
    bool initEngine(sshlx_global_cb cb, void *ctx);

private:
    // PuTTY's uxsel re-registers an fd whenever its interest set changes,
    // and Qt allows only one notifier per (fd, type). So each fd gets one
    // persistent set of notifiers, enabled according to the live watches.
    struct FdEntry {
        QSocketNotifier *n[3] = {nullptr, nullptr, nullptr};
        int refs[3] = {0, 0, 0};
    };
    struct Token {
        sshlx_fd_t fd;
        int mask;
    };

    static void *cbWatchFd(void *ctx, sshlx_fd_t fd, int mask);
    static void cbUnwatchFd(void *ctx, void *token);
    static void cbSetTimer(void *ctx, long ms);
    static void cbWake(void *ctx);
    void scheduleCleanup();

    QTimer m_timer;
    QHash<sshlx_fd_t, FdEntry> m_fds;
    bool m_cleanupPending = false;
};
