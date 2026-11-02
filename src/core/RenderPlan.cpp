#include "core/RenderPlan.h"
#include "core/Library.h"
#include "core/Tools.h"

#include <QProcess>
#include <QStringList>

QList<RenderSeg> buildRenderPlan(const QList<TlSeg> &segs, const QList<double> &fades,
                                 const Library &lib) {
    QList<RenderSeg> out;
    out.reserve(segs.size());
    for (int i = 0; i < segs.size(); i++) {
        const Clip *c = lib.clipById(segs[i].id);
        if (!c) continue;
        RenderSeg r;
        r.path = c->path;
        r.in = segs[i].s;
        r.len = segs[i].len;
        r.fadeMs = fades.value(i, 0);
        r.move = segs[i].move;
        r.w = c->w;
        r.h = c->h;
        out.append(r);
    }
    return out;
}

QList<RenderSeg> renderSegsFromDocument(const Document &doc) {
    QList<RenderSeg> out;
    out.reserve(doc.segments.size());
    for (const DocSeg &d : doc.segments) {
        RenderSeg r;
        r.path = d.path;
        r.in = d.in;
        r.len = d.len;
        r.fadeMs = d.fadeMs;
        r.move = d.move;
        out.append(r);
    }
    return out;
}

bool probeVideoSize(const QString &path, int *w, int *h) {
    QProcess p;
    p.start(tools::path(QStringLiteral("ffprobe")),
            {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"),
             QStringLiteral("v:0"), QStringLiteral("-show_entries"), QStringLiteral("stream=width,height"),
             QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
    if (!p.waitForFinished(20000)) return false;
    const QStringList parts = QString::fromLocal8Bit(p.readAllStandardOutput())
                                  .trimmed().split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 2) return false;
    const int pw = parts[0].trimmed().toInt();
    const int ph = parts[1].trimmed().toInt();
    if (pw <= 0 || ph <= 0) return false;
    if (w) *w = pw;
    if (h) *h = ph;
    return true;
}
