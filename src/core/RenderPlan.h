#pragma once
#include <QList>
#include <QString>
#include "core/Types.h"
#include "core/Document.h"

class Library;

// One shot, as the exporter needs it: a file, an in-point, a length, the move and
// the source size. Everything an ffmpeg command needs and nothing from the index.
struct RenderSeg {
    QString path;
    double in = 0, len = 0;
    double fadeMs = 0;   // crossfade into this segment
    TlMove move;
    int w = 0, h = 0;    // source size; 0 = probe it
};

// The edit as render segments. Segments whose clip has left the index are dropped.
QList<RenderSeg> buildRenderPlan(const QList<TlSeg> &segs, const QList<double> &fades,
                                 const Library &lib);
// The same, straight from a document: path-based, so the size is probed later.
QList<RenderSeg> renderSegsFromDocument(const Document &doc);
// Source size through ffprobe. False when the file cannot be read.
bool probeVideoSize(const QString &path, int *w, int *h);
