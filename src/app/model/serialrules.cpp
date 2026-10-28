#include "serialrules.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

QString SerialRule::actionName(Action a)
{
    switch (a) {
    case AutoConnect: return QStringLiteral("auto-connect");
    case Ignore: return QStringLiteral("ignore");
    default: return QStringLiteral("ask");
    }
}

QJsonObject SerialRule::toJson() const
{
    QJsonObject o;
    o["key"] = key;
    if (!usbKey.isEmpty()) o["usb"] = usbKey;
    if (!label.isEmpty()) o["label"] = label;
    o["action"] = actionName(action);
    o["speed"] = speed;
    o["dataBits"] = dataBits;
    o["parity"] = parity;
    o["stopHalfBits"] = stopHalfBits;
    o["flow"] = flow;
    o["autoReconnect"] = autoReconnect;
    return o;
}

SerialRule SerialRule::fromJson(const QJsonObject &o)
{
    SerialRule r;
    r.key = o["key"].toString();
    r.usbKey = o["usb"].toString();
    r.label = o["label"].toString();
    const QString a = o["action"].toString();
    r.action = a == "auto-connect" ? AutoConnect : a == "ignore" ? Ignore : Ask;
    r.speed = o["speed"].toInt(0);
    r.dataBits = o["dataBits"].toInt(8);
    r.parity = o["parity"].toInt(0);
    r.stopHalfBits = o["stopHalfBits"].toInt(2);
    r.flow = o["flow"].toInt(0);
    r.autoReconnect = o["autoReconnect"].toBool(true);
    return r;
}

const SerialRule *SerialRules::match(const QString &key, const QString &usbKey) const
{
    for (const SerialRule &r : m_rules)
        if (r.key == key)
            return &r;
    if (!usbKey.isEmpty())
        for (const SerialRule &r : m_rules)
            if (r.usbKey == usbKey || r.key == usbKey)
                return &r;
    return nullptr;
}

SerialRule *SerialRules::match(const QString &key, const QString &usbKey)
{
    return const_cast<SerialRule *>(static_cast<const SerialRules *>(this)->match(key, usbKey));
}

void SerialRules::set(const SerialRule &r)
{
    for (SerialRule &x : m_rules) {
        if (x.key == r.key) {
            x = r;
            return;
        }
    }
    m_rules.append(r);
}

void SerialRules::remove(const QString &key)
{
    for (int i = 0; i < m_rules.size(); i++)
        if (m_rules[i].key == key)
            m_rules.removeAt(i--);
}

bool SerialRules::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (d.isNull()) {
        if (error) *error = pe.errorString();
        return false;
    }
    m_rules.clear();
    for (const QJsonValue &v : d.object()["rules"].toArray())
        m_rules << SerialRule::fromJson(v.toObject());
    return true;
}

bool SerialRules::save(const QString &path, QString *error) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray a;
    for (const SerialRule &r : m_rules)
        a.append(r.toJson());
    QJsonObject o;
    o["format"] = "sshLX-serial-rules";
    o["version"] = 1;
    o["rules"] = a;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

QString SerialRules::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/sshLX/serial-rules.json");
}
