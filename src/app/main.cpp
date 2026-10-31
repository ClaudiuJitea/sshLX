#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <cstring>

#include "cli.h"
#include "dialogkit.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "theme.h"
#include "icons.h"
#include "qthostloop.h"
#include "shortcuts.h"

static MainWindow *g_main = nullptr;


static char g_crashFile[1024];

static void onGlobalEvent(void *, sshlx_global_event ev, const char *text)
{
    if (g_main)
        g_main->globalEvent(ev, QString::fromUtf8(text));
}

// Called by the engine just before the process exits on an unrecoverable
// PuTTY error. Only async-safe-ish work: append the message to a file.
static void onFatal(const char *msg)
{
    if (!g_crashFile[0])
        return;
    int fd = ::open(g_crashFile, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;
    const char *prefix = "fatal\t";
    if (::write(fd, prefix, strlen(prefix)) < 0 ||
        ::write(fd, msg, strlen(msg)) < 0 || ::write(fd, "\n", 1) < 0) {
        /* nothing more we can do */
    }
    ::close(fd);
}

int main(int argc, char **argv)
{
    // Many sessions share one process: allow as many fds as the hard limit
    // permits (PuTTY treats some fd-allocation failures as fatal).
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < rl.rlim_max) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }

    // On GNOME-family desktops, Qt without a platform theme falls back to its
    // own small default font and no desktop icon/palette integration. Use
    // Qt's GTK3 platform theme there unless the user chose something else.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
        const QByteArray de = qgetenv("XDG_CURRENT_DESKTOP").toLower();
        for (const char *d : {"gnome", "unity", "xfce", "cinnamon", "mate", "pantheon", "budgie"})
            if (de.contains(d)) {
                qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
                break;
            }
    }

    // "sshLX list", "sshLX run ..." and so on need no window.
    if (argc > 1 && isCliCommand(argv[1]))
        return runCli(argc, argv);

    // A link or host: the running window takes it, if there is one.
    QString target;
    if (argc > 1) {
        QString first = QString::fromLocal8Bit(argv[1]);
        if (first == "connect" && argc > 2)
            first = QString::fromLocal8Bit(argv[2]);
        if (!first.startsWith('-'))
            target = first;
    }

    QApplication app(argc, argv);
    if (!target.isEmpty() && MainWindow::sendToRunningInstance(MainWindow::instanceSocket(), target))
        return 0;
    QApplication::setOrganizationName("sshLX");
    QApplication::setApplicationName("sshLX");
    QApplication::setApplicationVersion(SSHLX_VERSION);
    QApplication::setDesktopFileName("sshlx");
    QApplication::setWindowIcon(Icons::app());
    applyUiFontSize();
    Theme::apply();

    QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(cfgDir);
    QByteArray crash = (cfgDir + "/open-tabs.crash").toLocal8Bit();
    strncpy(g_crashFile, crash.constData(), sizeof(g_crashFile) - 1);

    QtHostLoop loop;
    if (!loop.initEngine(&onGlobalEvent, nullptr))
        return 1;
    sshlx_engine_set_fatal_handler(&onFatal);

    int rc;
    {
        MainWindow w;
        g_main = &w;
        w.show();
        w.restoreStartupState();
        w.listenForInstances(MainWindow::instanceSocket());
        if (!target.isEmpty()) {
            QString error;
            if (!w.connectTo(target, true, &error))
                DialogKit::warning(&w, QStringLiteral("sshLX"), error);
        }
        rc = app.exec();
        g_main = nullptr;
    }
    // All SessionWidgets are gone; let the engine finish their teardown.
    sshlx_engine_run_pending();
    sshlx_engine_shutdown();
    return rc;
}
