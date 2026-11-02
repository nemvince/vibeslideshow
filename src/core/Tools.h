#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

// External commands the app shells out to: ffmpeg and ffprobe.
//
// A packaged build ships them next to the executable, and an app launched from the
// Finder, the Start menu or a .desktop file does not inherit the user's PATH — so
// look beside the binary first, then fall back to the name for a plain PATH lookup.
namespace tools {

inline QString path(const QString &name) {
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates{name};
#ifdef Q_OS_WIN
    candidates.prepend(name + QStringLiteral(".exe"));
#endif

    QStringList dirs;
    const QString override = qEnvironmentVariable("VIBESLIDESHOW_TOOLS_DIR");
    if (!override.isEmpty()) dirs << override;
    dirs << appDir << appDir + QStringLiteral("/tools");

    for (const QString &dir : dirs) {
        for (const QString &candidate : candidates) {
            const QString full = QDir(dir).absoluteFilePath(candidate);
            const QFileInfo fi(full);
            if (fi.isFile() && (fi.isExecutable() || !fi.suffix().isEmpty())) return full;
        }
    }
    return name;   // not bundled: let the OS resolve it from PATH
}

} // namespace tools
