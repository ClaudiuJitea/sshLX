// SerialPortList - live list of serial devices for the Connections tree,
// fed by the serialdetect module. The udev monitor fd is integrated with
// QSocketNotifier; without udev a QTimer polls.
#pragma once

#include <QList>
#include <QObject>
#include <functional>
#include <QString>

struct sd_monitor;
struct sd_device;
class QSocketNotifier;
class QTimer;

class SerialPortList : public QObject {
    Q_OBJECT
public:
    struct Device {
        QString name, node, byId, manufacturer, product, serial, driver;
        bool usb = false;
        quint16 vid = 0, pid = 0;
        // The identity used by device rules: by-id path if present,
        // else VID:PID+serial, else the node.
        QString ruleKey() const;
        QString vidPid() const;
        QString label() const;
        QString tooltip() const;
        // Path to put in PuTTY's SerialLine: the stable by-id link if any.
        QString stablePath() const { return byId.isEmpty() ? node : byId; }
    };

    explicit SerialPortList(QObject *parent = nullptr, bool allowUdev = true);
    ~SerialPortList() override;

    int count() const { return m_devices.size(); }
    const Device &at(int i) const { return m_devices.at(i); }
    const QList<Device> &devices() const { return m_devices; }
    QString backend() const;
    void rescan();
    // Devices for which this returns true are left out of the list and
    // report no hot-plug events. Call refilter() when its answer changes.
    void setHiddenFilter(std::function<bool(const Device &)> hidden);
    void refilter();

signals:
    void aboutToChange();
    void changed();
    void deviceAdded(const SerialPortList::Device &d);
    void deviceRemoved(const SerialPortList::Device &d);

private:
    static void cb(void *ctx, int ev, const sd_device *d);
    void dispatch();
    void reload();

    sd_monitor *m_mon = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_poll = nullptr;
    QTimer *m_settle = nullptr;
    QList<Device> m_devices;
    QList<QPair<int, Device>> m_pending;
    std::function<bool(const Device &)> m_hidden;
};
