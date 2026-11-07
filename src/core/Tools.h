#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QString>
#include <QStringList>

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

// True when the file lives behind a network filesystem. An SD card or USB disk
// mounted under /media or /mnt is local and must not be treated like a share;
// the mount's filesystem type is the only way to tell the two apart.
inline bool isNetworkPath(const QString &path) {
    if (path.contains(QLatin1String("/gvfs/"))) return true;
    if (path.startsWith(QLatin1String("//")) || path.startsWith(QLatin1String("\\\\"))) return true;

    const QStorageInfo info(path);
    const QString fs = QString::fromLocal8Bit(info.fileSystemType()).toLower();
    if (fs.isEmpty()) {
        // Unmounted/missing: keep the old prefix guess so a network share that
        // happens to be down is still not hammered with 20 s of readahead.
        return path.startsWith(QLatin1String("/mnt/")) || path.startsWith(QLatin1String("/media/"));
    }
    static const QStringList networkFs = {
        QStringLiteral("cifs"), QStringLiteral("smb"), QStringLiteral("smb2"), QStringLiteral("smb3"),
        QStringLiteral("nfs"), QStringLiteral("nfs4"), QStringLiteral("afp"), QStringLiteral("afpfs"),
        QStringLiteral("davfs"), QStringLiteral("fuse.sshfs"), QStringLiteral("fuse.rclone"),
        QStringLiteral("fuse.smbnetfs"), QStringLiteral("fuse.curlftpfs"), QStringLiteral("fuse.davfs"),
        QStringLiteral("fuse.webdavfs")};
    for (const QString &net : networkFs)
        if (fs == net || fs.startsWith(net + QLatin1Char('.'))) return true;
    return false;
}

} // namespace tools
