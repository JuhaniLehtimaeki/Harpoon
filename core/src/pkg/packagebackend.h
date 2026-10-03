#pragma once

#include "model/error.h"

#include <QStringList>

#include <functional>

namespace Harpoon {

struct InstallOptions
{
    bool allowReinstall = false;
    bool allowDowngrade = false;
};

// Installs and removes RPMs through the system package manager. Calls are
// asynchronous and the backend serializes them: PackageKit runs one
// transaction at a time.
class PackageBackend
{
public:
    using Done = std::function<void(const Error &error)>;

    virtual ~PackageBackend() = default;

    virtual QString name() const = 0;
    // Whether this backend can run without asking the user each time.
    virtual bool isSilent() const = 0;

    // Installs every file in one transaction. Paths must be absolute.
    virtual void installFiles(const QStringList &paths, const InstallOptions &options, Done done) = 0;
    // Removes installed packages by name, in one transaction (an app's
    // packages may depend on each other).
    virtual void removePackages(const QStringList &names, Done done) = 0;
};

} // namespace Harpoon
