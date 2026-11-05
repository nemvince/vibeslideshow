#include "ui/TimelineModel.h"
#include "core/Library.h"
#include "core/Scheduler.h"
#include "ui/ClipModel.h"

#include <QFileInfo>
#include <QSet>
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

struct TlCommand {
    const char *id;
    const char *label;
    const char *glyph;
    const char *keys;
    const char *group;
    bool bar;
    const char *needs;
};

// The one list of clip operations. Adding an operation means adding a row here and
// a branch in TimelineModel::runCommand(); the toolbar, the menu and the shortcut
// all follow automatically. `keys` must not collide with the window shortcuts in
// Studio.qml (Space, Left, Right, Ctrl+G, Escape).
const QList<TlCommand> &clipCommands() {
    static const QList<TlCommand> t = {
        {"shorten",   "Shorten this shot",            "\u2212", "-",       "timing",      true, "selection"},
        {"lengthen",  "Lengthen this shot",           "+",      "+",       "timing",      true, "selection"},
        {"chop",      "Chop into two shots",          "\u2702", "C",       "timing",      true, "selection"},
        {"rerollIn",  "Another moment from this clip", "\u25CE", "I",      "clip",        true, "selection"},
        {"swap",      "Swap in a different clip",     "\u21C4", "S",       "clip",        true, "selection"},
        {"remove",    "Remove this shot",             "\u2715", "Delete",  "destructive", true, "selection"},
        {"rerollAll", "New in-point for every shot",  "\u27F3", "Shift+R", "all",         true, "timeline"},
        // The join before this shot. Index 0 has no join, so these do nothing there.
        {"cutHard",   "Hard cut into this shot",      "\u2502", "H",       "timing",      false, "selection"},
        {"cutSofter", "Soften this cut",              "\u25CB", "[",       "timing",      false, "selection"},
        {"cutHarder", "Sharpen this cut",             "\u25CF", "]",       "timing",      false, "selection"},
    };
    return t;
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
    // A new cut is a new edit, not a step inside the old one.
    clearHistory();
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

void TimelineModel::pushUndo() {
    m_undo.append({m_segs, m_fades, m_poolIds, m_sel, m_edits});
    if (m_undo.size() > m_depth) m_undo.removeFirst();
    // A new change abandons whatever had been undone: there is no branching edit.
    m_redo.clear();
    emit historyChanged();
}

void TimelineModel::clearHistory() {
    if (m_undo.isEmpty() && m_redo.isEmpty()) return;
    m_undo.clear();
    m_redo.clear();
    emit historyChanged();
}

void TimelineModel::restore(const Snapshot &snap) {
    beginResetModel();
    m_segs = snap.segs;
    m_fades = snap.fades;
    m_poolIds = snap.poolIds;
    m_sel = snap.sel;
    m_edits = snap.edits;
    endResetModel();
    emit countChanged();
    emit selectedChanged();
    emit editsChanged();
    emit statsChanged();
    emit changed();
    emit historyChanged();
}

void TimelineModel::undo() {
    if (m_undo.isEmpty()) return;
    m_redo.append({m_segs, m_fades, m_poolIds, m_sel, m_edits});
    if (m_redo.size() > m_depth) m_redo.removeFirst();
    const Snapshot snap = m_undo.takeLast();
    restore(snap);
}

void TimelineModel::redo() {
    if (m_redo.isEmpty()) return;
    m_undo.append({m_segs, m_fades, m_poolIds, m_sel, m_edits});
    if (m_undo.size() > m_depth) m_undo.removeFirst();
    const Snapshot snap = m_redo.takeLast();
    restore(snap);
}

void TimelineModel::setHistoryDepth(int steps) {
    m_depth = std::clamp(steps, 1, 1000);
    // Shrinking the history drops the oldest steps; growing it cannot bring back
    // what was already discarded.
    while (m_undo.size() > m_depth) m_undo.removeFirst();
    while (m_redo.size() > m_depth) m_redo.removeFirst();
    emit historyChanged();
}

void TimelineModel::applyMoves() {
    if (m_segs.isEmpty() || !m_sched) return;
    bool changedAny = false;
    for (int i = 0; i < m_segs.size(); i++) {
        const TlMove m = m_sched->moveFor(i, cutTime(i));
        const TlMove &old = m_segs[i].move;
        if (m.z0 == old.z0 && m.z1 == old.z1 && m.cx0 == old.cx0 && m.cy0 == old.cy0 &&
            m.cx1 == old.cx1 && m.cy1 == old.cy1)
            continue;
        m_segs[i].move = m;
        changedAny = true;
    }
    // A view setting, not an edit: no snapshot, no bump of the edit counter. Only
    // the stage is told, so the preview and the next render use the new move.
    if (changedAny) emit changed();
}

void TimelineModel::adoptEdit(const QList<TlSeg> &segs, const QList<double> &fades) {
    clearHistory();
    beginResetModel();
    m_segs = segs;
    m_fades = fades;
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

void TimelineModel::shorten() {
    const int k = m_sel;
    if (k < 0 || k >= m_segs.size()) return;
    const bool last = k == m_segs.size() - 1;
    if (last && m_segs.size() < 2) return;
    // Segment lengths always sum to the song: time removed from one segment is
    // handed to the one at the end, and the last segment hands it back to its
    // neighbour instead of shrinking the edit off the end of the song.
    const double d = std::min(TlStep, m_segs[k].len - TlMin);
    if (d <= 0) return;
    pushUndo();
    m_segs[k].len -= d;
    if (last) m_segs[k - 1].len += d;
    else m_segs.last().len += d;
    emit dataChanged(index(std::max(0, k - 1), 0), index(m_segs.size() - 1, 0),
                     {SegLenRole, SegStartRole});
    touch();
}

void TimelineModel::lengthen() {
    const int k = m_sel;
    if (k < 0 || k >= m_segs.size()) return;
    const bool last = k == m_segs.size() - 1;
    if (last && m_segs.size() < 2) return;
    // The time a shot grows by comes out of the shot that follows it — or, for the
    // last shot, out of the one before it.
    double &donor = last ? m_segs[k - 1].len : m_segs.last().len;
    const double d = std::min(TlStep, donor - TlMin);
    if (d <= 0) return;
    pushUndo();
    m_segs[k].len += d;
    donor -= d;
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
    add.move = m_segs[k].move;   // both halves carry the shot's move

    pushUndo();
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
    pushUndo();
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
    pushUndo();
    m_segs[k].id = v->id;
    m_segs[k].s = m_sched->dur2seek(*v, m_segs[k].len, m_sched->rnd());
    const QModelIndex ix = index(k, 0);
    emit dataChanged(ix, ix, {SegIdRole, SegInRole, SegThumbRole, SegColorRole, SegScoreRole, SegNameRole});
    touch();
}

void TimelineModel::remove() {
    const int k = m_sel;
    if (k < 0 || m_segs.size() <= 2) return;
    pushUndo();
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

void TimelineModel::dropClips(const QList<int> &clipIds) {
    if (m_segs.isEmpty() || clipIds.isEmpty()) return;
    const QSet<int> gone(clipIds.begin(), clipIds.end());
    QList<TlSeg> keep;
    QList<double> fades;
    double freed = 0;
    for (int i = 0; i < m_segs.size(); i++) {
        if (gone.contains(m_segs[i].id)) {
            freed += m_segs[i].len;
            continue;
        }
        keep.append(m_segs[i]);
        fades.append(i < m_fades.size() ? m_fades[i] : 0);
    }
    if (keep.size() == m_segs.size()) return;
    pushUndo();
    beginResetModel();
    m_segs = keep;
    m_fades = fades;
    m_sel = -1;
    if (!m_segs.isEmpty()) m_segs.last().len += freed;   // the song stays covered
    endResetModel();
    emit countChanged();
    emit selectedChanged();
    emit statsChanged();
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
    pushUndo();
    m_segs[row].id = clipId;
    m_segs[row].s = inPoint;
    const QModelIndex ix = index(row, 0);
    emit dataChanged(ix, ix, {SegIdRole, SegInRole, SegThumbRole, SegColorRole, SegScoreRole, SegNameRole});
    touch();
}

void TimelineModel::rerollAll() {
    if (!m_sched || !m_lib || m_segs.isEmpty()) return;
    pushUndo();
    for (TlSeg &s : m_segs) {
        const Clip *c = m_lib->clipById(s.id);
        if (c) s.s = m_sched->dur2seek(*c, s.len, m_sched->rnd());
    }
    emit dataChanged(index(0, 0), index(m_segs.size() - 1, 0), {SegInRole});
    touch();
}

QString TimelineModel::thumbForSegment(int index) const {
    // Thumbnails are keyed by clip, so the strip reuses the rail's images.
    if (index < 0 || index >= m_segs.size() || !m_clips) return {};
    return m_clips->thumbFor(m_segs[index].id);
}

QVariantList TimelineModel::commands() const {
    QVariantList out;
    for (const TlCommand &c : clipCommands()) {
        QVariantMap row;
        row["id"] = QString::fromUtf8(c.id);
        row["label"] = QString::fromUtf8(c.label);
        row["glyph"] = QString::fromUtf8(c.glyph);
        row["keys"] = QString::fromUtf8(c.keys);
        row["group"] = QString::fromUtf8(c.group);
        row["bar"] = c.bar;
        row["needs"] = QString::fromUtf8(c.needs);
        out.append(row);
    }
    return out;
}

QString TimelineModel::commandKey(const QString &id) const {
    for (const TlCommand &c : clipCommands())
        if (id == QLatin1String(c.id)) return QString::fromUtf8(c.keys);
    return {};
}

void TimelineModel::runCommand(const QString &id) {
    if (id == QLatin1String("shorten")) shorten();
    else if (id == QLatin1String("lengthen")) lengthen();
    else if (id == QLatin1String("chop")) chop();
    else if (id == QLatin1String("rerollIn")) rerollIn();
    else if (id == QLatin1String("swap")) swap();
    else if (id == QLatin1String("remove")) remove();
    else if (id == QLatin1String("rerollAll")) rerollAll();
    else if (id == QLatin1String("cutHard") || id == QLatin1String("cutSofter")
             || id == QLatin1String("cutHarder")) {
        // The fade into the selected shot. Row 0 starts the edit: there is no join
        // there to soften, so it is left alone.
        const int k = m_sel;
        if (k <= 0 || k >= m_segs.size()) return;
        const double cur = m_fades.value(k, 0);
        // The step and the ceiling are settings: `[` and `]` move a cut by
        // `cutStep` ms, and no single cut dissolves for longer than `cutMax`.
        const int step = m_sched ? std::max(1, m_sched->params().cutStep) : 40;
        const double ceiling = double(m_sched ? std::max(step, m_sched->params().cutMax) : 500);
        const double next = (id == QLatin1String("cutHard")) ? 0.0
                          : (id == QLatin1String("cutSofter"))
                                ? (cur < ceiling ? std::min(ceiling, cur + step) : cur)
                                : (cur > 0 ? std::max(0.0, cur - step) : cur);
        // Nothing left to do: a cut already at the ceiling cannot get softer, and
        // one already hard cannot get sharper. No snapshot for a no-op.
        if (next == cur) return;
        pushUndo();
        m_fades[k] = next;
        emit dataChanged(index(k, 0), index(k, 0));
        touch();
    }
    // Unknown id: nothing. Keys are always live (see Studio.qml), and every
    // operation above already no-ops when nothing is selected.
}

