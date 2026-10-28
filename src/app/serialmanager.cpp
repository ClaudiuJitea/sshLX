#include "serialmanager.h"
#include "icons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>

#ifdef SSHLX_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#endif

#include <cstring>
#include <iterator>
#include <memory>

#include "dialogkit.h"
#include "documentarea.h"
#include "mainwindow.h"
#include "serialdetect.h"
#include "sessionwidget.h"
#include "settingsdialog.h"

static int serialProtocol()
{
    for (size_t i = 0; i < sshlx_protocol_count(); i++)
        if (!strcmp(sshlx_protocol_get(i)->name, "serial"))
            return sshlx_protocol_get(i)->id;
    return -1;
}

// Tried in this order when a rule's speed is "auto": 9600 is the console
// default of most network gear (Cisco, Juniper, HP, APC), 115200 that of
// most boards and modern devices.
static const int kProbeSpeeds[] = {9600, 115200, 19200, 38400, 57600, 230400};
// Used when nothing answers the probe.
static const int kFallbackSpeed = 9600;

static QString detectedKey(const QString &ruleKey)
{
    QString k = ruleKey;
    return "serial/detectedSpeed/" + k.replace('/', '|');
}

// ---- banner -------------------------------------------------------------------
DeviceBanner::DeviceBanner(QWidget *parent) : QFrame(parent)
{
    setObjectName("DeviceBanner");
    setFrameShape(QFrame::StyledPanel);
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(6, 2, 6, 2);
    auto *icon = new QLabel;
    icon->setPixmap(Icons::get("usb").pixmap(20, 20));
    h->addWidget(icon);
    m_text = new QLabel;
    m_text->setTextFormat(Qt::RichText);
    h->addWidget(m_text, 1);
    auto add = [&](const QString &t, void (DeviceBanner::*sig)()) {
        auto *b = new QPushButton(t);
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, sig);
        h->addWidget(b);
        return b;
    };
    QPushButton *connectBtn = add(tr("&Connect"), &DeviceBanner::connectClicked);
    connectBtn->setObjectName("connect");
    connectBtn->setProperty("role", "primary");
    add(tr("&Always connect"), &DeviceBanner::alwaysClicked)->setObjectName("always");
    add(tr("&Ignore"), &DeviceBanner::ignoreClicked)->setObjectName("ignore");
    add(tr("&Never ask"), &DeviceBanner::neverClicked)->setObjectName("never");
    add(tr("&Settings..."), &DeviceBanner::settingsClicked)->setObjectName("settings");
}

void DeviceBanner::showDevice(const SerialPortList::Device &d, bool accessible)
{
    m_key = d.ruleKey();
    QString what = d.product.isEmpty() ? (d.usb ? d.vidPid() : tr("serial port")) : d.product;
    QString text = (d.usb ? tr("USB serial device detected: <b>%1</b> on <b>%2</b> – Connect?")
                          : tr("Serial device detected: <b>%1</b> on <b>%2</b> – Connect?"))
                       .arg(what.toHtmlEscaped(), d.node.toHtmlEscaped());
    if (!accessible)
        text += tr("<br><small>You don't have permission to open it: add your user to "
                   "the <b>dialout</b> group (<b>uucp</b> on some distributions) and log in "
                   "again.</small>");
    m_text->setText(text);
    m_text->setToolTip(d.tooltip());
}

// ---- manager --------------------------------------------------------------------
SerialManager::SerialManager(MainWindow *main, QToolBar *bannerBar, bool allowUdev,
                             const QString &sysfsRoot, const QString &devRoot)
    : QObject(main), m_main(main), m_bar(bannerBar)
{
    m_rules.load(SerialRules::defaultPath());
    m_ports = new SerialPortList(this, allowUdev, sysfsRoot, devRoot);
    m_banner = new DeviceBanner;
    m_bar->addWidget(m_banner);
    m_bar->setVisible(false);
    connect(m_ports, &SerialPortList::deviceAdded, this, &SerialManager::onAdded);
    connect(m_ports, &SerialPortList::deviceRemoved, this, &SerialManager::onRemoved);
    connect(m_banner, &DeviceBanner::connectClicked, this, [this] { answer(0); });
    connect(m_banner, &DeviceBanner::alwaysClicked, this, [this] { answer(1); });
    connect(m_banner, &DeviceBanner::ignoreClicked, this, [this] { answer(2); });
    connect(m_banner, &DeviceBanner::neverClicked, this, [this] { answer(3); });
    connect(m_banner, &DeviceBanner::settingsClicked, this, [this] { answer(4); });
}

void SerialManager::saveRules()
{
    m_rules.save(SerialRules::defaultPath());
}

QString SerialManager::usbKey(const SerialPortList::Device &d)
{
    return d.usb ? "usb:" + d.vidPid() + ":" + d.serial : QString();
}

SerialRule SerialManager::ruleFor(const SerialPortList::Device &d) const
{
    if (const SerialRule *r = m_rules.match(d.ruleKey(), usbKey(d)))
        return *r;
    SerialRule r;
    r.key = d.ruleKey();
    r.usbKey = usbKey(d);
    r.label = d.product.isEmpty() ? d.node : d.product;
    return r;
}

sshlx_conf *SerialManager::confFor(const SerialPortList::Device &d) const
{
    SerialRule r = ruleFor(d);
    sshlx_conf *c = sshlx_conf_new_default();
    sshlx_conf_set_int(c, sshlx_conf_key_protocol(), serialProtocol());
    // The by-id path survives renumbering (ttyUSB0 -> ttyUSB1).
    sshlx_conf_set_str(c, sshlx_conf_key_find("serline"), d.stablePath().toUtf8().constData());
    int speed = r.speed;
    if (speed <= 0)
        speed = QSettings().value(detectedKey(d.ruleKey()), kFallbackSpeed).toInt();
    sshlx_conf_set_int(c, sshlx_conf_key_find("serspeed"), speed);
    sshlx_conf_set_int(c, sshlx_conf_key_find("serdatabits"), r.dataBits);
    sshlx_conf_set_int(c, sshlx_conf_key_find("serparity"), r.parity);
    sshlx_conf_set_int(c, sshlx_conf_key_find("serstopbits"), r.stopHalfBits);
    sshlx_conf_set_int(c, sshlx_conf_key_find("serflow"), r.flow);
    QString title = d.product.isEmpty() ? d.node : QStringLiteral("%1 (%2)").arg(d.product, d.name);
    sshlx_conf_set_str(c, sshlx_conf_key_find("wintitle"), title.toUtf8().constData());
    return c;
}

void SerialManager::openDevice(const SerialPortList::Device &d, bool withOptions)
{
    const QString key = d.ruleKey();
    if (m_probing.contains(key))
        return;   // already detecting; the tab opens when that finishes
    // A port serves one session at a time: bring its tab forward (and
    // reconnect it if it has ended) rather than open a second one.
    if (!withOptions) {
        for (SessionWidget *s : m_main->sessions()) {
            if (deviceBoundTo(s, d)) {
                m_main->documents()->activate(s);
                if (s->state() == SSHLX_STATE_DISCONNECTED)
                    s->reconnect();
                return;
            }
        }
    }
    // Probing would steal the bytes of a tab already talking to the device.
    bool inUse = false;
    for (SessionWidget *s : m_main->sessions())
        if (s->state() != SSHLX_STATE_DISCONNECTED && deviceBoundTo(s, d))
            inUse = true;
    if (ruleFor(d).speed > 0 || inUse) {
        openWithRule(d, withOptions);
        return;
    }
    m_probing.insert(key);
    m_main->statusBar()->showMessage(tr("Detecting the line speed of %1...").arg(d.node));
    auto speed = std::make_shared<int>(0);
    const QByteArray node = d.node.toUtf8();
    QThread *t = QThread::create([speed, node] {
        *speed = sd_probe_speed(node.constData(), kProbeSpeeds, std::size(kProbeSpeeds),
                                300, nullptr);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    connect(t, &QThread::finished, this, [this, d, withOptions, speed, key] {
        m_probing.remove(key);
        QSettings().setValue(detectedKey(key), *speed > 0 ? *speed : kFallbackSpeed);
        m_main->statusBar()->showMessage(
            *speed > 0 ? tr("%1: detected %2 baud").arg(d.node).arg(*speed)
                       : tr("%1: no reply to the speed probe, using %2 baud")
                             .arg(d.node).arg(kFallbackSpeed),
            6000);
        openWithRule(d, withOptions);
    });
    t->start();
}

void SerialManager::openWithRule(const SerialPortList::Device &d, bool withOptions)
{
    sshlx_conf *c = confFor(d);
    QString name = d.product.isEmpty() ? d.node : d.product;
    if (withOptions) {
        auto *dlg = new SettingsDialog(c, false, 0, tr("sshLX Configuration - %1").arg(name),
                                       m_main, true);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->showPanel("Connection/Serial");
        QString path = d.stablePath();
        connect(dlg, &SettingsDialog::accepted, m_main, [this, dlg, name, path] {
            SessionWidget *s = m_main->openSession(dlg->conf(), name);
            s->setSerialByIdPath(path);
        });
        dlg->open();
        return;
    }
    SessionWidget *s = m_main->openSession(c, name);
    s->setSerialByIdPath(d.stablePath());
    sshlx_conf_free(c);
}

bool SerialManager::deviceBoundTo(const SessionWidget *s, const SerialPortList::Device &d) const
{
    if (!s->serialByIdPath().isEmpty())
        return s->serialByIdPath() == d.stablePath() ||
               (!d.byId.isEmpty() && s->serialByIdPath() == d.byId);
    if (s->protocol() != serialProtocol())
        return false;
    QString line = QString::fromUtf8(sshlx_conf_get_str(s->conf(), sshlx_conf_key_find("serline")));
    return line == d.node || (!d.byId.isEmpty() && line == d.byId);
}

void SerialManager::onAdded(const SerialPortList::Device &d)
{
    SerialRule r = ruleFor(d);

    // A device coming back reconnects the tabs bound to it.
    bool reconnected = false;
    if (r.autoReconnect) {
        for (SessionWidget *s : m_main->sessions()) {
            if (s->state() == SSHLX_STATE_DISCONNECTED && deviceBoundTo(s, d)) {
                s->reconnect();
                reconnected = true;
            }
        }
    }
    if (reconnected || r.action == SerialRule::Ignore)
        return;
    if (r.action == SerialRule::AutoConnect) {
        openDevice(d);
        return;
    }
    // Ask: queue for the banner.
    for (const auto &q : m_queue)
        if (q.ruleKey() == d.ruleKey())
            return;
    m_queue.append(d);
    if (m_queue.size() == 1)
        showNext();
    QSettings st;
    if (st.value("notifications/desktop", false).toBool())
        desktopNotify(d);
    emit deviceNotified(d.ruleKey());
}

void SerialManager::onRemoved(const SerialPortList::Device &d)
{
    // Drop a pending question about it.
    for (int i = 0; i < m_queue.size(); i++) {
        if (m_queue[i].ruleKey() == d.ruleKey()) {
            m_queue.removeAt(i);
            if (i == 0)
                showNext();
            break;
        }
    }
    // Bound tabs see EOF/EIO from PuTTY's serial back end and disconnect on
    // their own; nothing else to do. They reconnect in onAdded().
}

void SerialManager::showNext()
{
    if (m_queue.isEmpty()) {
        m_bar->setVisible(false);
        return;
    }
    const SerialPortList::Device &d = m_queue.first();
    m_banner->showDevice(d, sd_device_accessible(d.node.toUtf8().constData(), nullptr));
    m_bar->setVisible(true);
}

void SerialManager::answer(int choice)
{
    if (m_queue.isEmpty())
        return;
    SerialPortList::Device d = m_queue.takeFirst();
    SerialRule r = ruleFor(d);
    switch (choice) {
    case 0:   // connect once
        openDevice(d);
        break;
    case 1:   // always connect
        r.action = SerialRule::AutoConnect;
        m_rules.set(r);
        saveRules();
        openDevice(d);
        break;
    case 2:   // ignore this time
        break;
    case 3:   // never ask
        r.action = SerialRule::Ignore;
        m_rules.set(r);
        saveRules();
        break;
    case 4:
        editRule(d);
        break;
    }
    showNext();
}

void SerialManager::editRule(const SerialPortList::Device &d)
{
    SerialRule r = ruleFor(d);
    QDialog dlg(m_main);
    dlg.setWindowTitle(tr("Serial device rule"));
    dlg.setMinimumWidth(480);
    auto *outer = new QVBoxLayout(&dlg);
    DialogKit::frame(outer);
    QStringList facts;
    facts << d.node;
    if (d.usb)
        facts << "USB " + d.vidPid();
    if (!d.serial.isEmpty())
        facts << tr("serial %1").arg(d.serial);
    outer->addWidget(DialogKit::header(r.label, facts.join(QStringLiteral("  ·  "))));
    auto *whenBox = new QFormLayout;
    whenBox->setHorizontalSpacing(16);
    whenBox->setVerticalSpacing(10);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    auto *action = new QComboBox;
    action->addItem(tr("Ask"), SerialRule::Ask);
    action->addItem(tr("Always connect automatically"), SerialRule::AutoConnect);
    action->addItem(tr("Ignore"), SerialRule::Ignore);
    action->setCurrentIndex(action->findData(r.action));
    whenBox->addRow(tr("When plugged in"), action);
    auto *speed = new QComboBox;
    speed->setEditable(true);
    const QString autoSpeed = tr("Auto-detect");
    speed->addItem(autoSpeed);
    for (int s : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000})
        speed->addItem(QString::number(s));
    speed->setCurrentText(r.speed > 0 ? QString::number(r.speed) : autoSpeed);
    form->addRow(tr("Speed (baud)"), speed);
    auto *bits = new QSpinBox;
    bits->setRange(5, 8);
    bits->setValue(r.dataBits);
    form->addRow(tr("Data bits"), bits);
    auto *parity = new QComboBox;
    for (const char *p : {"None", "Odd", "Even", "Mark", "Space"})
        parity->addItem(tr(p));
    parity->setCurrentIndex(r.parity);
    form->addRow(tr("Parity"), parity);
    auto *stop = new QComboBox;
    stop->addItem("1", 2);
    stop->addItem("1.5", 3);
    stop->addItem("2", 4);
    stop->setCurrentIndex(stop->findData(r.stopHalfBits));
    form->addRow(tr("Stop bits"), stop);
    auto *flow = new QComboBox;
    for (const char *f : {"None", "XON/XOFF", "RTS/CTS", "DSR/DTR"})
        flow->addItem(tr(f));
    flow->setCurrentIndex(r.flow);
    form->addRow(tr("Flow control"), flow);
    auto *reconnect = new QCheckBox(tr("Reconnect open tabs when this device returns"));
    reconnect->setChecked(r.autoReconnect);
    auto *whenCol = new QVBoxLayout;
    whenCol->setSpacing(10);
    whenCol->addLayout(whenBox);
    whenCol->addWidget(reconnect);
    outer->addWidget(DialogKit::card(tr("Behaviour"), whenCol));
    outer->addWidget(DialogKit::card(tr("Line settings"), form));
    QPushButton *cancel = DialogKit::secondaryButton(tr("Cancel"));
    QPushButton *ok = DialogKit::primaryButton(tr("Save rule"));
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    outer->addWidget(DialogKit::footer({cancel, ok}));
    if (dlg.exec() != QDialog::Accepted)
        return;
    r.action = SerialRule::Action(action->currentData().toInt());
    r.speed = qMax(0, speed->currentText().toInt());   // "Auto-detect" -> 0
    r.dataBits = bits->value();
    r.parity = parity->currentIndex();
    r.stopHalfBits = stop->currentData().toInt();
    r.flow = flow->currentIndex();
    r.autoReconnect = reconnect->isChecked();
    m_rules.set(r);
    saveRules();
}

void SerialManager::desktopNotify(const SerialPortList::Device &d)
{
#ifdef SSHLX_HAVE_DBUS
    QDBusMessage m = QDBusMessage::createMethodCall(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify");
    QString what = d.product.isEmpty() ? d.node : d.product;
    m << QStringLiteral("sshLX") << uint(0) << QStringLiteral("sshlx")
      << tr("Serial device detected")
      << tr("%1 on %2").arg(what, d.node)
      << QStringList() << QVariantMap() << int(8000);
    QDBusConnection::sessionBus().call(m, QDBus::NoBlock);
#else
    Q_UNUSED(d);
#endif
}
