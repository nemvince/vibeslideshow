#pragma once
#include <QList>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include "core/Types.h"

// One shot as a document stores it. The clip is named by its path, never by the
// index id: ids are rebuilt by every scan, paths survive a reopen.
struct DocSeg {
    QString path;
    double in = 0;
    double len = 0;
    double fadeMs = 0;   // crossfade into this shot
    TlMove move;
};

// A .vibes project: the footage, the song, the look, the filters and the edit.
struct Document {
    QString song;
    QList<MediaSource> sources;
    QSet<QString> hidden;
    QVariantMap params;       // Params::toMap()
    QString preset;
    QVariantMap filters;      // Studio's filtersToMap()
    int previewHeight = 0;
    QList<DocSeg> segments;   // empty = no edit yet
};

inline constexpr int kDocumentVersion = 1;

// Reads a .vibes file. On failure returns false, leaves `out` untouched and puts a
// one-line, user-facing reason in `*error`.
bool loadDocument(const QString &path, Document *out, QString *error);
// Writes atomically (QSaveFile). Rejects an unsupported `version`.
bool saveDocument(const QString &path, const Document &doc, QString *error);
