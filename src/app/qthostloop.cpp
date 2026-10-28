#include "qthostloop.h"

#include <QMetaObject>

static const struct {
    int bit;
    QSocketNotifier::Type type;
} kKinds[3] = {
    {SSHLX_IO_READ, QSocketNotifier::Read},
    {SSHLX_IO_WRITE, QSocketNotifier::Write},
    {SSHLX_IO_EXCEPT, QSocketNotifier::Exception},
};

QtHostLoop::QtHostLoop(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [] { sshlx_engine_on_timer(); });
}

QtHostLoop::~QtHostLoop() = default;

bool QtHostLoop::initEngine(sshlx_global_cb cb, void *ctx)
{
    sshlx_host_loop loop{};
    loop.ctx = this;
    loop.watch_fd = &QtHostLoop::cbWatchFd;
    loop.unwatch_fd = &QtHostLoop::cbUnwatchFd;
    loop.set_timer = &QtHostLoop::cbSetTimer;
    loop.wake = &QtHostLoop::cbWake;
    return sshlx_engine_init(&loop, cb, ctx) == 0;
}

void *QtHostLoop::cbWatchFd(void *ctx, sshlx_fd_t fd, int mask)
{
    auto *self = static_cast<QtHostLoop *>(ctx);
    FdEntry &e = self->m_fds[fd];
    for (int i = 0; i < 3; i++) {
        if (!(mask & kKinds[i].bit))
            continue;
        if (!e.n[i]) {
            auto *n = new QSocketNotifier(static_cast<qintptr>(fd), kKinds[i].type, self);
            const int bit = kKinds[i].bit;
            QObject::connect(n, &QSocketNotifier::activated, self, [fd, bit] {
                sshlx_engine_on_fd(fd, bit);
            });
            e.n[i] = n;
        }
        e.refs[i]++;
        e.n[i]->setEnabled(true);
    }
    return new Token{fd, mask};
}

void QtHostLoop::cbUnwatchFd(void *ctx, void *token)
{
    auto *self = static_cast<QtHostLoop *>(ctx);
    auto *t = static_cast<Token *>(token);
    auto it = self->m_fds.find(t->fd);
    if (it != self->m_fds.end()) {
        for (int i = 0; i < 3; i++) {
            if (!(t->mask & kKinds[i].bit) || !it->n[i])
                continue;
            if (--it->refs[i] <= 0) {
                it->refs[i] = 0;
                // May be inside this notifier's own activation: only disable.
                it->n[i]->setEnabled(false);
            }
        }
        self->scheduleCleanup();
    }
    delete t;
}

// Delete notifier sets that nothing watches any more, from the event loop
// (never from inside a notifier's own signal).
void QtHostLoop::scheduleCleanup()
{
    if (m_cleanupPending)
        return;
    m_cleanupPending = true;
    QMetaObject::invokeMethod(this, [this] {
        m_cleanupPending = false;
        for (auto it = m_fds.begin(); it != m_fds.end();) {
            if (!it->refs[0] && !it->refs[1] && !it->refs[2]) {
                for (QSocketNotifier *n : it->n)
                    delete n;
                it = m_fds.erase(it);
            } else {
                ++it;
            }
        }
    }, Qt::QueuedConnection);
}

void QtHostLoop::cbSetTimer(void *ctx, long ms)
{
    auto *self = static_cast<QtHostLoop *>(ctx);
    if (ms < 0)
        self->m_timer.stop();
    else
        self->m_timer.start(static_cast<int>(ms));
}

void QtHostLoop::cbWake(void *ctx)
{
    auto *self = static_cast<QtHostLoop *>(ctx);
    QMetaObject::invokeMethod(self, [] { sshlx_engine_run_pending(); },
                              Qt::QueuedConnection);
}
