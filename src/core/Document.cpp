#include "core/Document.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

const char *kAppTag = "vibeslideshow";

void fail(QString *error, const QString &why) {
    if (error) *error = why;
}

// The motion block of a segment, with the identity move for anything absent: a
// hand-written or older project simply has a still shot.
TlMove moveFrom(const QJsonObject &o) {
    TlMove m;
    m.z0 = o.value(QStringLiteral("z0")).toDouble(1.0);
    m.z1 = o.value(QStringLiteral("z1")).toDouble(1.0);
    m.cx0 = o.value(QStringLiteral("cx0")).toDouble(0.5);
    m.cy0 = o.value(QStringLiteral("cy0")).toDouble(0.5);
    m.cx1 = o.value(QStringLiteral("cx1")).toDouble(0.5);
    m.cy1 = o.value(QStringLiteral("cy1")).toDouble(0.5);
    return m;
}

QJsonObject moveTo(const TlMove &m) {
    QJsonObject o;
    o["z0"] = m.z0;
    o["z1"] = m.z1;
    o["cx0"] = m.cx0;
    o["cy0"] = m.cy0;
    o["cx1"] = m.cx1;
    o["cy1"] = m.cy1;
    return o;
}

} // namespace

bool loadDocument(const QString &path, Document *out, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("Could not open %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    QJsonParseError parse{};
    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !jd.isObject()) {
        fail(error, QStringLiteral("Not a vibeslideshow project."));
        return false;
    }
    const QJsonObject o = jd.object();
    if (o.value(QStringLiteral("app")).toString() != QLatin1String(kAppTag)) {
        fail(error, QStringLiteral("Not a vibeslideshow project."));
        return false;
    }
    const int version = o.value(QStringLiteral("version")).toInt(kDocumentVersion);
    if (version != kDocumentVersion) {
        fail(error, QStringLiteral("This project was made by a newer version of vibeslideshow."));
        return false;
    }

    Document d;
    d.song = o.value(QStringLiteral("song")).toString();
    for (const auto &v : o.value(QStringLiteral("sources")).toArray()) {
        const QJsonObject so = v.toObject();
        const QString spath = so.value(QStringLiteral("path")).toString();
        if (spath.isEmpty()) continue;
        d.sources.append({spath, so.value(QStringLiteral("dir")).toBool()});
    }
    for (const auto &v : o.value(QStringLiteral("hidden")).toArray()) {
        const QString h = v.toString();
        if (!h.isEmpty()) d.hidden.insert(h);
    }
    d.previewHeight = o.value(QStringLiteral("previewHeight")).toInt(0);
    const QJsonObject look = o.value(QStringLiteral("look")).toObject();
    d.preset = look.value(QStringLiteral("preset")).toString();
    d.params = look.value(QStringLiteral("params")).toObject().toVariantMap();
    d.filters = o.value(QStringLiteral("filters")).toObject().toVariantMap();

    // An unedited project has no `edit` at all; that is not an error.
    const QJsonObject edit = o.value(QStringLiteral("edit")).toObject();
    for (const auto &v : edit.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject so = v.toObject();
        DocSeg seg;
        seg.path = so.value(QStringLiteral("path")).toString();
        if (seg.path.isEmpty()) continue;
        seg.in = so.value(QStringLiteral("in")).toDouble(0);
        seg.len = so.value(QStringLiteral("len")).toDouble(0);
        seg.fadeMs = so.value(QStringLiteral("fadeMs")).toDouble(0);
        seg.move = moveFrom(so);
        d.segments.append(seg);
    }

    if (out) *out = d;
    return true;
}

bool saveDocument(const QString &path, const Document &doc, QString *error) {
    QJsonObject o;
    o["app"] = QLatin1String(kAppTag);
    o["version"] = kDocumentVersion;
    o["song"] = doc.song;

    QJsonArray sources;
    for (const MediaSource &s : doc.sources) {
        QJsonObject so;
        so["path"] = s.path;
        so["dir"] = s.isDir;
        sources.append(so);
    }
    o["sources"] = sources;

    // Sorted, so two saves of the same project produce the same bytes.
    QStringList hiddenSorted;
    hiddenSorted.reserve(doc.hidden.size());
    for (const QString &h : doc.hidden) hiddenSorted.append(h);
    hiddenSorted.sort();
    QJsonArray hidden;
    for (const QString &h : hiddenSorted) hidden.append(h);
    o["hidden"] = hidden;

    o["previewHeight"] = doc.previewHeight;
    QJsonObject look;
    look["preset"] = doc.preset;
    look["params"] = QJsonObject::fromVariantMap(doc.params);
    o["look"] = look;
    o["filters"] = QJsonObject::fromVariantMap(doc.filters);

    QJsonArray segments;
    for (const DocSeg &s : doc.segments) {
        QJsonObject so;
        so["path"] = s.path;
        so["in"] = s.in;
        so["len"] = s.len;
        so["fadeMs"] = s.fadeMs;
        const QJsonObject mv = moveTo(s.move);
        for (auto it = mv.begin(); it != mv.end(); ++it) so.insert(it.key(), it.value());
        segments.append(so);
    }
    QJsonObject edit;
    edit["segments"] = segments;
    o["edit"] = edit;

    // QSaveFile: a crash or a full disk mid-write must not leave a half project
    // where the user's work used to be.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        fail(error, QStringLiteral("Could not write %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        fail(error, QStringLiteral("Could not write %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    return true;
}
