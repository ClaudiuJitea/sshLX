#include "serialportlist.h"

#include <QSocketNotifier>
#include <QTimer>

#include "serialdetect.h"

static SerialPortList::Device fromC(const sd_device *d)
{
    SerialPortList::Device r;
    r.name = QString::fromUtf8(d->name);
    r.node = QString::fromUtf8(d->devnode);
    r.byId = QString::fromUtf8(d->by_id);
    r.manufacturer = QString::fromUtf8(d->manufacturer).replace('_', ' ');
    r.product = QString::fromUtf8(d->product).replace('_', ' ');
    r.serial = QString::fromUtf8(d->serial);
    r.driver = QString::fromUtf8(d->driver);
    r.usb = d->usb;
    r.vid = d->vid;
    r.pid = d->pid;
    return r;
}

QString SerialPortList::Device::vidPid() const
{
    if (!usb)
        return {};
    return QStringLiteral("%1:%2").arg(vid, 4, 16, QLatin1Char('0'))
                                  .arg(pid, 4, 16, QLatin1Char('0'));
}

QString SerialPortList::Device::ruleKey() const
{
    if (!byId.isEmpty())
        return "by-id:" + byId;
    if (usb)
        return "usb:" + vidPid() + ":" + serial;
    return "node:" + node;
}

QString SerialPortList::Device::label() const
{
    QString what = product.isEmpty() ? driver : product;
    return what.isEmpty() ? node : QStringLiteral("%1 (%2)").arg(node, what);
}

QString SerialPortList::Device::tooltip() const
{
    QStringList l;
    l << QStringLiteral("<b>%1</b>").arg(node.toHtmlEscaped());
    if (!byId.isEmpty()) l << "by-id: " + byId.toHtmlEscaped();
    if (usb) l << "USB " + vidPid();
    if (!manufacturer.isEmpty()) l << "Manufacturer: " + manufacturer.toHtmlEscaped();
    if (!product.isEmpty()) l << "Product: " + product.toHtmlEscaped();
    if (!serial.isEmpty()) l << "Serial number: " + serial.toHtmlEscaped();
    if (!driver.isEmpty()) l << "Driver: " + driver.toHtmlEscaped();
    return l.join("<br>");
}

SerialPortList::SerialPortList(QObject *parent, bool allowUdev)
    : QObject(parent)
{
    m_mon = sd_monitor_new(
        [](void *ctx, sd_event ev, const sd_device *dev) {
            cb(ctx, int(ev), dev);
        },
        this, allowUdev ? 0 : SD_FLAG_NO_UDEV, nullptr, nullptr);
    int fd = sd_monitor_fd(m_mon);
    if (fd >= 0) {
        m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &SerialPortList::dispatch);
        // by-id links and sysfs attributes can settle slightly after the
        // udev event; rescan once more shortly afterwards.
        m_settle = new QTimer(this);
        m_settle->setSingleShot(true);
        m_settle->setInterval(700);
        connect(m_settle, &QTimer::timeout, this, &SerialPortList::rescan);
    }
    int interval = sd_monitor_poll_interval_ms(m_mon);
    if (interval > 0) {
        m_poll = new QTimer(this);
        m_poll->setInterval(interval);
        connect(m_poll, &QTimer::timeout, this, &SerialPortList::rescan);
        m_poll->start();
    }
    reload();
}

SerialPortList::~SerialPortList()
{
    sd_monitor_free(m_mon);
}

QString SerialPortList::backend() const
{
    return QString::fromUtf8(sd_monitor_backend(m_mon));
}

void SerialPortList::cb(void *ctx, int ev, const sd_device *d)
{
    static_cast<SerialPortList *>(ctx)->m_pending.append({ev, fromC(d)});
}

void SerialPortList::reload()
{
    const sd_device *list;
    size_t n = sd_monitor_list(m_mon, &list);
    m_devices.clear();
    for (size_t i = 0; i < n; i++) {
        Device d = fromC(&list[i]);
        if (!m_hidden || !m_hidden(d))
            m_devices.append(d);
    }
}

void SerialPortList::setHiddenFilter(std::function<bool(const Device &)> hidden)
{
    m_hidden = std::move(hidden);
    refilter();
}

void SerialPortList::refilter()
{
    emit aboutToChange();
    reload();
    emit changed();
}

void SerialPortList::dispatch()
{
    sd_monitor_dispatch(m_mon);
    QList<QPair<int, Device>> events;
    events.swap(m_pending);
    if (!events.isEmpty()) {
        emit aboutToChange();
        reload();
        emit changed();
        for (const auto &e : events) {
            if (m_hidden && m_hidden(e.second))
                continue;
            if (e.first == SD_ADDED)
                emit deviceAdded(e.second);
            else
                emit deviceRemoved(e.second);
        }
    }
    if (m_settle)
        m_settle->start();
}

void SerialPortList::rescan()
{
    sd_monitor_rescan(m_mon);
    QList<QPair<int, Device>> events;
    events.swap(m_pending);
    if (events.isEmpty())
        return;   // nothing changed: no signals, so views keep their state
    emit aboutToChange();
    reload();
    emit changed();
    for (const auto &e : events) {
        if (m_hidden && m_hidden(e.second))
            continue;
        if (e.first == SD_ADDED)
            emit deviceAdded(e.second);
        else
            emit deviceRemoved(e.second);
    }
}
