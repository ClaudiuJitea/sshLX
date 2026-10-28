// SerialManager - reacts to serial hot-plug: applies device rules
// (auto-connect / ask / ignore), shows the non-modal "device detected"
// banner (plus an optional desktop notification), opens serial tabs from
// the per-rule template and reconnects tabs when their device returns.
#pragma once

#include <QFrame>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>

#include "serialportlist.h"
#include "serialrules.h"
#include "sshlx_engine.h"

class MainWindow;
class SessionWidget;
class QLabel;
class QPushButton;
class QToolBar;

// The banner shown at the top of the main window.
class DeviceBanner : public QFrame {
    Q_OBJECT
public:
    explicit DeviceBanner(QWidget *parent = nullptr);
    void showDevice(const SerialPortList::Device &d, bool accessible);
    QString currentKey() const { return m_key; }

signals:
    void connectClicked();
    void alwaysClicked();
    void ignoreClicked();
    void neverClicked();
    void settingsClicked();

private:
    QLabel *m_text;
    QString m_key;
};

class SerialManager : public QObject {
    Q_OBJECT
public:
    SerialManager(MainWindow *main, QToolBar *bannerBar, bool allowUdev = true,
                  const QString &sysfsRoot = {}, const QString &devRoot = {});

    SerialPortList *ports() const { return m_ports; }
    SerialRules &rules() { return m_rules; }
    void saveRules();
    // Rule for a device, or a default one keyed to it.
    SerialRule ruleFor(const SerialPortList::Device &d) const;
    // PuTTY Conf for a device using its rule's line settings (for an
    // auto-speed rule, the last detected speed). Caller frees.
    sshlx_conf *confFor(const SerialPortList::Device &d) const;
    // Opens a tab for the device. With an auto-speed rule the line speed is
    // probed first (in the background), so the tab appears a moment later.
    void openDevice(const SerialPortList::Device &d, bool withOptions = false);
    void editRule(const SerialPortList::Device &d);
    DeviceBanner *banner() const { return m_banner; }
    bool deviceBoundTo(const SessionWidget *s, const SerialPortList::Device &d) const;

    static QString usbKey(const SerialPortList::Device &d);

signals:
    void deviceNotified(const QString &key);

private:
    void onAdded(const SerialPortList::Device &d);
    void onRemoved(const SerialPortList::Device &d);
    void showNext();
    void desktopNotify(const SerialPortList::Device &d);
    void answer(int choice);
    void openWithRule(const SerialPortList::Device &d, bool withOptions);

    MainWindow *m_main;
    SerialPortList *m_ports;
    SerialRules m_rules;
    QToolBar *m_bar;
    DeviceBanner *m_banner;
    QList<SerialPortList::Device> m_queue;
    QSet<QString> m_probing;   // rule keys whose speed is being detected
};
