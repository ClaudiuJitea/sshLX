// SerialRules - per-device behaviour when a serial device is plugged in,
// persisted as ~/.config/sshLX/serial-rules.json. Qt Core only.
//
// A rule is matched by the device's stable identity: its
// /dev/serial/by-id path when it has one (survives renumbering), otherwise
// USB VID:PID + serial number, otherwise the device node.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

struct SerialRule {
    enum Action { Ask, AutoConnect, Ignore };
    QString key;            // "by-id:/dev/serial/by-id/...", "usb:0403:6001:SERIAL", "node:/dev/ttyS0"
    QString usbKey;         // "usb:VID:PID:SERIAL" alternative identity (may be empty)
    QString label;          // product name, for the rules list
    Action action = Ask;
    int speed = 0;          // baud; 0 = detect when the device is opened
    int dataBits = 8;
    int parity = 0;         // PuTTY SER_PAR_*: 0 none, 1 odd, 2 even, 3 mark, 4 space
    int stopHalfBits = 2;   // PuTTY stores stop bits in halves: 2 = 1 stop bit
    int flow = 0;           // PuTTY SER_FLOW_*: 0 none, 1 XON/XOFF, 2 RTS/CTS, 3 DSR/DTR
                            // (template default 8N1, no flow control; PuTTY's own default is XON/XOFF)
    bool autoReconnect = true;
    bool hidden = false;    // left out of the Serial Ports list and ignored when plugged in

    QJsonObject toJson() const;
    static SerialRule fromJson(const QJsonObject &o);
    static QString actionName(Action a);
};

class SerialRules {
public:
    // Find the rule for a device: exact key first, then the USB identity.
    const SerialRule *match(const QString &key, const QString &usbKey) const;
    SerialRule *match(const QString &key, const QString &usbKey);
    // Insert or replace (by key).
    void set(const SerialRule &r);
    void remove(const QString &key);
    QList<SerialRule> rules() const { return m_rules; }

    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;
    static QString defaultPath();

private:
    QList<SerialRule> m_rules;
};
