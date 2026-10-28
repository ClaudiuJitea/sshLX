// Keyring - opt-in storage of connection passwords in the system keyring
// (QtKeychain: Secret Service / KWallet). PuTTY deliberately never stores
// passwords; sshLX only does so when the user enables it, and only in the
// keyring, never in a file. All calls are asynchronous.
#pragma once

#include <QString>
#include <functional>

namespace Keyring {

bool available();                 // built with QtKeychain
bool enabled();                   // user opted in (and available)
void setEnabled(bool on);

QString accountFor(const QString &connectionId);
void read(const QString &account, std::function<void(bool found, const QString &pw)> done);
void write(const QString &account, const QString &password,
           std::function<void(bool ok, const QString &error)> done = {});
void remove(const QString &account, std::function<void(bool ok)> done = {});

} // namespace Keyring
