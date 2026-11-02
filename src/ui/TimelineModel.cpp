#include "ui/TimelineModel.h"
#include "core/Library.h"
#include "core/Scheduler.h"
#include "ui/ClipModel.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace {
// Stable per-clip swatch for the strip: clips keep their colour across edits.
QString colorFor(int id) {
    static const QStringList cols = {QStringLiteral("#2a4d69"), QStringLiteral("#4b5d54"),
                                     QStringLiteral("#5d4037"), QStringLiteral("#455a64"),
                                     QStringLiteral("#5e4b6b"), QStringLiteral("#37524f"),
                                     QStringLiteral("#6b4b3f"), QStringLiteral("#544a6b")};
    return cols[((id % cols.size()) + cols.size()) % cols.size()];
}
} // namespace

TimelineModel::TimelineModel(QObject *parent) : QAbstractListModel(parent) {}

int TimelineModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_segs.size();
}

QHash<int, QByteArray> TimelineModel::roleNames() const {
    return {{SegIdRole, "segId"}, {SegStartRole, "segStart"}, {SegLenRole, "segLen"},
            {SegInRole, "segIn"}, {SegThumbRole, "segThumb"}, {SegScoreRole, "segScore"},
            {SegNameRole, "segName"}, {SegColorRole, "segColor"}, {SegIndexRole, "segIndex"},
            {SegSelectedRole, "segSelected"}};
}

QVariant TimelineModel::data(const QModelIndex &index, int role) const {
    const int r = index.row();
    if (r < 0 || r >= m_segs.size()) return {};
    const TlSeg &s = m_segs[r];
    switch (role) {
    case SegIdRole: return s.id;
    case SegInRole: return s.s;
    case SegLenRole: return s.len;
    case SegStartRole: {
        double at = 0;
        for (int i = 0; i < r; i++) at += m_segs[i].len;
        return at;
    }
    case SegThumbRole: return thumbForSegment(r);
    case SegScoreRole: {
        const Clip *c = m_lib ? m_lib->clipById(s.id) : nullptr;
        return c ? c->score : 0;
    }
    case SegNameRole: {
        const Clip *c = m_lib ? m_lib->clipById(s.id) : nullptr;
        return c ? QFileInfo(c->path).baseName() : QString();
    }
    case SegColorRole: return colorFor(s.id);
    case SegIndexRole: return r;
    case SegSelectedRole: return r == m_sel;
    default: return {};
    }
}

void TimelineModel::setSelected(int i) {
    if (i < -1 || i >= m_segs.size()) return;
    if (i == m_sel) return;
    const int old = m_sel;
    m_sel = i;
    if (old >= 0 && old < m_segs.size()) {
        const QModelIndex ix = index(old, 0);
        emit dataChanged(ix, ix, {SegSelectedRole});
    }
    if (m_sel >= 0 && m_sel < m_segs.size()) {
        const QModelIndex ix = index(m_sel, 0);
        emit dataChanged(ix, ix, {SegSelectedRole});
    }
    emit selectedChanged();
}

QString TimelineModel::selectionInfo() const {
    if (m_sel < 0 || m_sel >= m_segs.size()) return {};
    const TlSeg &s = m_segs[m_sel];
    const Clip *c = m_lib ? m_lib->clipById(s.id) : nullptr;
    QString src = QStringLiteral("missing clip");
    if (c) {
        if (c->day.isEmpty()) src = QStringLiteral("no stamp");
        else if (c->hasTod) {
            const int h = int(c->tod), m = int(std::round((c->tod - h) * 60));
            src = QStringLiteral("%1 %2:%3 UTC").arg(c->day, QString::number(h).rightJustified(2, QLatin1Char('0')),
                                                     QString::number(m).rightJustified(2, QLatin1Char('0')));
        } else src = c->day;
    }
    return QStringLiteral("#%1 of %2 · %3 · %4 s · in-point %5 s")
        .arg(m_sel + 1).arg(m_segs.size()).arg(src)
        .arg(s.len, 0, 'f', 2).arg(s.s, 0, 'f', 2);
}

QString TimelineModel::stats() const {
    return QStringLiteral("%1 segments · %2 edits").arg(m_segs.size()).arg(m_edits);
}

QList<double> TimelineModel::cuts() const {
    QList<double> c;
    double at = 0;
    for (const TlSeg &s : m_segs) {
        c.append(std::round(at * 1000.0) / 1000.0);
        at += s.len;
    }
    return c;
}

void TimelineModel::applyPool(const QList<int> &poolIds) {
    m_poolIds = poolIds;
    if (!m_sched) return;
    // The filter defines the pool; the scheduler owns the walk order (chronology,
    // optionally shuffled on its seeded stream) so a fixed seed reproduces it.
    QList<Clip> base;
    base.reserve(poolIds.size());
    if (m_lib) {
        for (int id : poolIds) {
            const Clip *c = m_lib->clipById(id);
            if (c) base.append(*c);
        }
    }
    m_sched->setPool(base);
    m_sched->setPool(m_sched->orderedPool(m_sched->params().shuffle));
}

void TimelineModel::regenerate(const QList<int> &poolIds) {
    applyPool(poolIds);
    if (!m_sched || m_songDur <= 0) return;
    Cuts cc;
    QList<TlSeg> segs = m_sched->materialize(m_songDur, m_sched->pool(), &cc);
    beginResetModel();
    m_segs = segs;
    // Fades run parallel to the cuts; an edited segment keeps the fade its cut had.
    m_fades = cc.fades;
    while (m_fades.size() < m_segs.size()) m_fades.append(0);
    m_fades.resize(m_segs.size());
    m_sel = -1;
    m_edits = 0;
    endResetModel();
    emit countChanged();
    emit selectedChanged();
    emit editsChanged();
    emit statsChanged();
    emit changed();
}

void TimelineModel::touch() {
    m_edits++;
    emit editsChanged();
    emit statsChanged();
    emit changed();
}

void TimelineModel::shorten() {
    const int k = m_sel;
    if (k < 0 || k >= m_segs.size()) return;
    // Segment lengths always sum to the song: time removed from one segment is
    // handed to the one at the end, and the last segment hands it back to its
    // neighbour instead of shrinking the edit off the end of the song.
    if (k == m_segs.size() - 1) {
        if (m_segs.size() < 2) return;
        const double d = std::min(TlStep, m_segs[k].len - TlMin);
        if (d <= 0) return;
        m_segs[k].len -= d;
        m_segs[k - 1].len += d;
    } else {
        const double d = std::min(TlStep, m_segs[k].len - TlMin);
        if (d <= 0) return;
        m_segs[k].len -= d;
        m_segs.last().len += d;
    }
    emit dataChanged(index(std::max(0, k - 1), 0), index(m_segs.size() - 1, 0),
                     {SegLenRole, SegStartRole});
    touch();
}

void TimelineModel::lengthen() {
    const int k = m_sel;
    if (k < 0 || k >= m_segs.size()) return;
    if (k == m_segs.size() - 1) {
        if (m_segs.size() < 2) return;
        const double d = std::min(TlStep, m_segs[k - 1].len - TlMin);
        if (d <= 0) return;
        m_segs[k].len += d;
        m_segs[k - 1].len -= d;
    } else {
        const double d = std::min(TlStep, m_segs.last().len - TlMin);
        if (d <= 0) return;
        m_segs[k].len += d;
        m_segs.last().len -= d;
    }
    emit dataChanged(index(std::max(0, k - 1), 0), index(m_segs.size() - 1, 0),
                     {SegLenRole, SegStartRole});
    touch();
}

void TimelineModel::chop() {
    const int k = m_sel;
    if (k < 0 || k >= m_segs.size()) return;
    const double half = m_segs[k].len / 2;
    if (half < TlMin || m_segs[k].len - half < TlMin) return;

    const int prevId = k > 0 ? m_segs[k - 1].id : -1;
    const int nextId = k + 1 < m_segs.size() ? m_segs[k + 1].id : -1;
    const QList<Clip> &pool = m_sched->pool();
    if (pool.isEmpty()) return;
    const Clip *v = m_sched->pickNeighbour(pool, m_segs[k].id, prevId, nextId);
    if (!v) return;

    TlSeg add;
    add.id = v->id;
    add.len = m_segs[k].len - half;
    add.s = m_sched->dur2seek(*v, add.len, m_sched->rnd());

    beginInsertRows(QModelIndex(), k + 1, k + 1);
    m_segs.insert(k + 1, add);
    m_fades.insert(k + 1, 0);   // a chop is always a hard cut
    m_segs[k].len = half;
    endInsertRows();
    emit dataChanged(index(k, 0), index(m_segs.size() - 1, 0), {SegLenRole, SegStartRole});
    emit countChanged();
    emit statsChanged();
    touch();
}

void TimelineModel::rerollIn() {
    const int k = m_sel;
    if (k < 0 || k >= m_segs.size() || !m_lib) return;
    const Clip *c = m_lib->clipById(m_segs[k].id);
    if (!c) return;
    m_segs[k].s = m_sched->dur2seek(*c, m_segs[k].len, m_sched->rnd());
    const QModelIndex ix = index(k, 0);
    emit dataChanged(ix, ix, {SegInRole});
    touch();
}

void TimelineModel::swap() {
    const int k = m_sel;
    if (k < 0 || k >= m_segs.size() || !m_sched) return;
    const int prevId = k > 0 ? m_segs[k - 1].id : -1;
    const int nextId = k + 1 < m_segs.size() ? m_segs[k + 1].id : -1;
    const Clip *v = m_sched->pickNeighbour(m_sched->pool(), m_segs[k].id, prevId, nextId);
    if (!v) return;
    m_segs[k].id = v->id;
    m_segs[k].s = m_sched->dur2seek(*v, m_segs[k].len, m_sched->rnd());
    const QModelIndex ix = index(k, 0);
    emit dataChanged(ix, ix, {SegIdRole, SegInRole, SegThumbRole, SegColorRole, SegScoreRole, SegNameRole});
    touch();
}

void TimelineModel::remove() {
    const int k = m_sel;
    if (k < 0 || m_segs.size() <= 2) return;
    const double ln = m_segs[k].len;
    beginRemoveRows(QModelIndex(), k, k);
    m_segs.removeAt(k);
    m_fades.removeAt(k);
    endRemoveRows();
    m_segs.last().len += ln;
    setSelected(std::min(k, int(m_segs.size()) - 1));
    emit dataChanged(index(0, 0), index(m_segs.size() - 1, 0), {SegLenRole, SegStartRole});
    emit countChanged();
    touch();
}

void TimelineModel::setSongDuration(double d) {
    if (m_songDur == d) return;
    m_songDur = d;
    emit songDurationChanged();
}

int TimelineModel::indexAt(double songTime) const {
    double at = 0;
    for (int i = 0; i < m_segs.size(); i++) {
        at += m_segs[i].len;
        if (songTime < at) return i;
    }
    return m_segs.isEmpty() ? -1 : m_segs.size() - 1;
}

double TimelineModel::cutTime(int row) const {
    if (row < 0 || row > m_segs.size()) return 0;
    double t = 0;
    for (int i = 0; i < row && i < m_segs.size(); i++) t += m_segs[i].len;
    return t;
}

void TimelineModel::replaceSegment(int row, int clipId, double inPoint) {
    if (row < 0 || row >= m_segs.size()) return;
    m_segs[row].id = clipId;
    m_segs[row].s = inPoint;
    const QModelIndex ix = index(row, 0);
    emit dataChanged(ix, ix, {SegIdRole, SegInRole, SegThumbRole, SegColorRole, SegScoreRole, SegNameRole});
    touch();
}

void TimelineModel::rerollAll() {
    if (!m_sched || !m_lib) return;
    for (TlSeg &s : m_segs) {
        const Clip *c = m_lib->clipById(s.id);
        if (c) s.s = m_sched->dur2seek(*c, s.len, m_sched->rnd());
    }
    if (!m_segs.isEmpty())
        emit dataChanged(index(0, 0), index(m_segs.size() - 1, 0), {SegInRole});
    touch();
}

QString TimelineModel::thumbForSegment(int index) const {
    // Thumbnails are keyed by clip, so the strip reuses the rail's images.
    if (index < 0 || index >= m_segs.size() || !m_clips) return {};
    return m_clips->thumbFor(m_segs[index].id);
}
