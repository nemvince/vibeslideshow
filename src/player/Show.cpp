#include "player/Show.h"
#include "player/MpvItem.h"
#include "core/Scheduler.h"
#include "core/Library.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace {
QString fmtTime(double t) {
    if (!std::isfinite(t) || t < 0) t = 0;
    const int total = int(t);
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

Show::Show(QObject *parent) : QObject(parent) {
    m_audio = mpv_create();
    if (!m_audio) {
        qCritical("vibeslideshow: could not create the audio instance");
    } else {
        mpv_set_option_string(m_audio, "vo", "null");
        mpv_set_option_string(m_audio, "video", "no");
        mpv_set_option_string(m_audio, "config", "no");
        mpv_set_option_string(m_audio, "idle", "yes");
        mpv_set_option_string(m_audio, "keep-open", "no");
        mpv_set_option_string(m_audio, "audio-client-name", "vibeslideshow");
        if (mpv_initialize(m_audio) < 0) {
            qCritical("vibeslideshow: could not initialise the audio instance");
            mpv_terminate_destroy(m_audio);
            m_audio = nullptr;
        } else {
            mpv_observe_property(m_audio, 0, "time-pos", MPV_FORMAT_DOUBLE);
            mpv_observe_property(m_audio, 0, "duration", MPV_FORMAT_DOUBLE);
        }
    }

    m_tick.setInterval(25);
    m_tick.setTimerType(Qt::PreciseTimer);
    connect(&m_tick, &QTimer::timeout, this, &Show::tick);
    m_fillLater.setSingleShot(true);
    connect(&m_fillLater, &QTimer::timeout, this, &Show::fillLookahead);
}

Show::~Show() {
    if (m_audio) mpv_terminate_destroy(m_audio);
}

void Show::setPreviewHeight(int lines) {
    m_previewHeight = std::max(0, lines);
    for (MpvItem *s : m_surfs) s->setPreviewHeight(m_previewHeight);
}

void Show::attachSurfaces(const QVariantList &surfaces) {
    m_surfs.clear();
    m_holdSeg.clear();
    m_holdAt.clear();
    for (const QVariant &v : surfaces) {
        auto *item = qobject_cast<MpvItem *>(v.value<QObject *>());
        if (!item) continue;
        m_surfs << item;
        m_holdSeg << -1;
        m_holdClip << -1;
        m_holdAt << -1.0;
    }
    m_frontIdx = m_surfs.isEmpty() ? -1 : 0;
    m_outgoingIdx = -1;
    // The session restores before the stage exists, so the cap arrives here too.
    for (MpvItem *s : m_surfs) s->setPreviewHeight(m_previewHeight);
    if (auto *f = frontSurface()) f->setPaused(true);
}

MpvItem *Show::frontSurface() const {
    return (m_frontIdx >= 0 && m_frontIdx < m_surfs.size()) ? m_surfs[m_frontIdx] : nullptr;
}

int Show::segmentAt(double songTime) const {
    if (m_segs.isEmpty()) return -1;
    int idx = 0;
    for (int i = 0; i < m_cuts.size(); i++)
        if (m_cuts[i] <= songTime + 1e-6) idx = i;
    return std::min(idx, int(m_segs.size()) - 1);
}

double Show::targetFor(int idx, double songTime) const {
    if (idx < 0 || idx >= m_segs.size()) return 0;
    double clipDur = 0;
    pathFor(m_segs[idx], &clipDur);
    // Offset into the clip: zero at a cut, more when landing mid-shot.
    const double into = (idx < m_cuts.size()) ? std::max(0.0, songTime - m_cuts[idx]) : 0.0;
    return std::min(m_segs[idx].s + into, std::max(0.0, clipDur - 0.05));
}

double Show::warmReadahead(int idx) const {
    if (idx < 0 || idx >= m_segs.size()) return 2.0;
    const double len = (idx + 1 < m_cuts.size()) ? m_cuts[idx + 1] - m_cuts[idx] : m_segs[idx].len;
    // The shot plus a margin, nothing more: four surfaces each asking mpv for a
    // full readahead would hold several minutes of footage in memory at once.
    return std::clamp(len + 1.5, 2.0, 20.0);
}

void Show::loadInto(int surf, int segIdx, double clipTime, double readaheadSecs) {
    if (surf < 0 || surf >= m_surfs.size() || segIdx < 0 || segIdx >= m_segs.size()) return;
    const QString path = pathFor(m_segs[segIdx], nullptr);
    if (path.isEmpty()) return;
    m_surfs[surf]->loadAndSeek(path, clipTime, readaheadSecs);
    m_surfs[surf]->setPaused(true);
    m_holdSeg[surf] = segIdx;
    m_holdClip[surf] = m_segs[segIdx].id;
    m_holdAt[surf] = clipTime;
}

int Show::surfaceHolding(int idx, double clipTime, double tolerance) const {
    if (idx < 0 || idx >= m_segs.size()) return -1;
    for (int s = 0; s < m_surfs.size(); s++) {
        if (m_holdSeg[s] != idx) continue;
        if (m_holdClip[s] != m_segs[idx].id) continue;
        if (std::fabs(m_holdAt[s] - clipTime) > tolerance) continue;
        return s;
    }
    return -1;
}

int Show::spareSurface() const {
    if (m_surfs.isEmpty()) return -1;
    // A surface still fading out is on screen: loading into it would show the
    // next shot through the fade.
    const bool fading = m_outgoingIdx >= 0 && m_sinceCut.isValid() &&
                        m_sinceCut.elapsed() < m_lastFadeMs + 60;
    int fallback = -1;
    for (int s = 0; s < m_surfs.size(); s++) {
        if (s == m_frontIdx) continue;
        if (s == m_outgoingIdx && fading) continue;
        if (m_holdSeg[s] < 0) return s;   // already empty: use it and touch nothing
        if (fallback < 0) fallback = s;
    }
    return fallback;
}

void Show::emitReadyChanged() { emit readyChanged(); }

void Show::setEdit(const QList<TlSeg> &segs, const QList<double> &cuts, const QList<double> &fades) {
    // The timeline under playback just changed: stop cutting against a schedule
    // that no longer exists, and let the user press play on the new one.
    if (m_playing) pause();
    m_segs = segs;
    m_cuts = cuts;
    m_fades = fades;
    emit segmentsChanged();
    emitReadyChanged();
    // The picture follows the edit at once. Without this the stage keeps the frame
    // it was holding until something else seeks, so a swap or a chop looks like it
    // did nothing until you touch the strip.
    refreshPreview();
}

void Show::applyView(double songTime) {
    MpvItem *f = frontSurface();
    if (!f || m_cutIdx < 0 || m_cutIdx >= m_segs.size()) return;
    const TlMove &mv = m_segs[m_cutIdx].move;
    const double len = std::max(0.001, m_segs[m_cutIdx].len);
    const double p = std::clamp((songTime - m_cuts.value(m_cutIdx, 0.0)) / len, 0.0, 1.0);
    const double z = mv.z0 + (mv.z1 - mv.z0) * p;
    const double cx = mv.cx0 + (mv.cx1 - mv.cx0) * p;
    const double cy = mv.cy0 + (mv.cy1 - mv.cy0) * p;
    // mpv's pan is positive leftwards, hence the negation: a centre right of the
    // middle of the frame pans the picture left.
    f->setView(std::log2(std::max(1.0, z)), -(cx - 0.5), -(cy - 0.5));
}

void Show::refreshPreview() {
    if (m_surfs.isEmpty()) return;
    // No edit left: let go of the picture rather than leaving the last clip of a
    // deleted folder on the stage.
    if (m_segs.isEmpty() || m_cuts.isEmpty()) {
        releaseSurfaces();
        m_currentClip = -1;
        m_cutIdx = 0;
        emit currentClipChanged();
        emit cutIndexChanged();
        return;
    }

    const int idx = segmentAt(m_pos);
    const double target = targetFor(idx, m_pos);
    // The shot on screen is the one the playhead is over, at the same frame: an
    // edit somewhere else in the strip must not re-open and re-seek this picture
    // (on a network share that is a visible stall for no change).
    if (surfaceHolding(idx, target, 1e-6) == m_frontIdx && m_currentClip == m_segs[idx].id) {
        fillLookahead();
        applyView(m_pos);   // a paused seek shows the shot's opening framing
        return;
    }

    // Replace the picture where it is, on the surface that is already on screen:
    // there is nothing to fade from, because the old shot is gone from the edit.
    if (m_frontIdx < 0) m_frontIdx = 0;
    if (m_frontIdx >= m_surfs.size()) m_frontIdx = m_surfs.size() - 1;
    loadInto(m_frontIdx, idx, target, -1);
    m_outgoingIdx = -1;   // replaced in place: not a cut, so nothing is fading out
    for (int s = 0; s < m_surfs.size(); s++)
        if (s != m_frontIdx) m_surfs[s]->setPaused(true);
    m_cutIdx = idx;
    m_currentClip = m_segs[idx].id;
    m_lastFadeMs = (idx < m_fades.size()) ? m_fades[idx] : 120;
    emit currentClipChanged();
    emit cutIndexChanged();
    m_sinceCut.restart();
    m_lastFrameTs = -1;
    m_seekStreak = 0;
    m_sinceFrameAdvance.restart();
    fillLookahead();
    applyView(m_pos);   // a paused seek shows the shot's opening framing
}

void Show::fillLookahead() {
    if (m_surfs.isEmpty() || m_segs.isEmpty() || m_cuts.isEmpty() || m_frontIdx < 0) return;
    const int n = m_surfs.size();
    if (n < 2) return;
    // A link that cannot feed the shot on screen must not be asked for three more.
    if (m_buffering) return;
    const bool fading = m_outgoingIdx >= 0 && m_sinceCut.isValid() &&
                        m_sinceCut.elapsed() < m_lastFadeMs + 60;

    // The shots the next cuts will want, nearest first: one per spare surface.
    QList<int> want;
    for (int k = 1; k < n && m_cutIdx + k < m_segs.size(); k++) want << m_cutIdx + k;

    // A surface is spoken for while it holds one of those shots, at its cut frame.
    auto reserved = [&](int s) {
        for (int w : want)
            if (surfaceHolding(w, targetFor(w, m_cuts[w]), 1e-6) == s) return true;
        return false;
    };
    bool blocked = false;
    for (int idx : want) {
        const double target = targetFor(idx, m_cuts[idx]);
        if (surfaceHolding(idx, target, 1e-6) >= 0) continue;   // already warm
        // Any surface that is not on screen, not fading out, and not holding a
        // shot still needed; an empty one first, so a warm shot is never reused.
        int victim = -1;
        for (int pass = 0; pass < 2 && victim < 0; pass++) {
            for (int s = 0; s < n; s++) {
                if (s == m_frontIdx) continue;
                if (s == m_outgoingIdx && fading) continue;
                if (reserved(s)) continue;
                if (pass == 0 && m_holdSeg[s] >= 0) continue;   // first pass: empty ones only
                victim = s;
                break;
            }
        }
        if (victim < 0) { blocked = true; break; }
        loadInto(victim, idx, target, warmReadahead(idx));
    }
    // The surface still fading out can hold one of these in a moment: come back
    // for it rather than leaving a slot cold until the next cut.
    if (blocked && fading && !m_fillLater.isActive()) m_fillLater.start(int(m_lastFadeMs) + 80);
}

void Show::releaseSurfaces() {
    for (MpvItem *s : m_surfs) s->clear();
    for (int s = 0; s < m_surfs.size(); s++) {
        m_holdSeg[s] = -1;
        m_holdClip[s] = -1;
        m_holdAt[s] = -1.0;
    }
    m_outgoingIdx = -1;
}

QString Show::pathFor(const TlSeg &s, double *dur) const {
    if (!m_lib) return {};
    const Clip *c = m_lib->clipById(s.id);
    if (!c) return {};
    const bool proxy = m_sched && m_sched->params().useLrf && !c->lrf.isEmpty();
    if (dur) *dur = proxy ? c->lrfDur : c->duration;
    return proxy ? c->lrf : c->path;
}

void Show::setSong(const QString &path) {
    // A new song invalidates the clock the edit was cut against.
    pause();
    m_song = path;
    m_songTitle = QFileInfo(path).completeBaseName();
    m_dur = 0;
    setPos(0);
    if (m_audio) {
        const QByteArray p = path.toUtf8();
        const char *cmd[] = {"loadfile", p.constData(), "replace", nullptr};
        mpv_command(m_audio, cmd);
        int pause = 1;
        mpv_set_property(m_audio, "pause", MPV_FORMAT_FLAG, &pause);
        double vol = m_volume;
        mpv_set_property(m_audio, "volume", MPV_FORMAT_DOUBLE, &vol);
    }
    emit songChanged();
    emit durationChanged();
}

void Show::setVolume(double v) {
    m_volume = std::max(0.0, std::min(100.0, v));
    if (m_audio) {
        double vol = m_volume;
        mpv_set_property(m_audio, "volume", MPV_FORMAT_DOUBLE, &vol);
    }
    emit volumeChanged();
}

void Show::pollAudio() {
    if (!m_audio) return;
    for (;;) {
        mpv_event *e = mpv_wait_event(m_audio, 0);
        if (!e || e->event_id == MPV_EVENT_NONE) break;
        if (e->event_id != MPV_EVENT_PROPERTY_CHANGE) continue;
        auto *p = static_cast<mpv_event_property *>(e->data);
        if (!p || p->format != MPV_FORMAT_DOUBLE || !p->data || !p->name) continue;
        const QString name = QString::fromUtf8(p->name);
        const double v = *static_cast<double *>(p->data);
        if (name == QLatin1String("time-pos")) setPos(v);
        else if (name == QLatin1String("duration") && std::isfinite(v) && v > 0) {
            m_dur = v;
            emit durationChanged();
        }
    }
}

void Show::setPos(double t) {
    m_pos = t;
    emit positionChanged();
}

QString Show::positionText() const { return fmtTime(m_pos); }
QString Show::durationText() const { return fmtTime(m_dur); }

double Show::expectedVideoTime(int idx, double songTime) const {
    // Song time → clip time: the segment's in-point plus however far into it we are.
    if (idx < 0 || idx >= m_segs.size() || idx >= m_cuts.size()) return 0;
    const double start = m_cuts[idx];
    return m_segs[idx].s + std::max(0.0, songTime - start);
}

void Show::play() {
    if (!m_audio || !ready() || m_surfs.isEmpty()) return;
    int pause = 0;
    mpv_set_property(m_audio, "pause", MPV_FORMAT_FLAG, &pause);
    m_playing = true;
    m_waiting = false;
    m_resync = false;
    m_lastFrameTs = -1;
    m_sinceFrameAdvance.restart();
    emit playingChanged();
    m_tick.start();
    const int idx = segmentAt(m_pos);
    // Already open on the visible surface, and holding the clip the edit has in
    // that shot (an edit that swapped a clip re-cues through refreshPreview).
    const bool open = m_frontIdx >= 0 && idx >= 0 && m_holdSeg[m_frontIdx] == idx &&
                      m_holdClip[m_frontIdx] == m_segs[idx].id;
    if (auto *f = frontSurface(); f && open) {
        // The shot under the playhead is already open (or about to be, if an edit
        // just re-cued it): unpause and let the lookahead keep the rest warm.
        const double want = targetFor(idx, m_pos);
        if (std::fabs(m_holdAt[m_frontIdx] - want) > 0.10) {
            f->seekExact(want);
            m_holdAt[m_frontIdx] = want;
        }
        f->setPaused(false);
        fillLookahead();
    } else {
        // Nothing usable on screen: cue the covering segment.
        startSegment(idx, m_pos);
    }
}

void Show::setBuffering(bool b) {
    if (m_buffering == b) return;
    m_buffering = b;
    emit bufferingChanged();
}

void Show::beginWait() {
    if (!m_playing || m_waiting) return;
    m_waiting = true;
    m_resync = false;
    m_sinceWaitStart.restart();
    setBuffering(true);
    // Hold the song at the position the picture is stuck on. Running the music
    // on would only widen the gap and make the resume a jump.
    if (m_audio) {
        int one = 1;
        mpv_set_property(m_audio, "pause", MPV_FORMAT_FLAG, &one);
    }
    if (MpvItem *f = frontSurface()) f->setPaused(true);
}

void Show::resumeFromWait() {
    if (!m_waiting) return;
    m_waiting = false;
    m_resync = false;
    setBuffering(false);
    if (m_audio) {
        int zero = 0;
        mpv_set_property(m_audio, "pause", MPV_FORMAT_FLAG, &zero);
    }
    if (MpvItem *f = frontSurface()) f->setPaused(false);
    // Give the freshly resumed clip a moment before the drift guard touches it.
    m_sinceCut.restart();
    m_sinceCorrection.invalidate();
    m_seekStreak = 0;
    m_lastFrameTs = -1;
    m_sinceFrameAdvance.restart();
}

void Show::pause() {
    if (m_audio) {
        int pause = 1;
        mpv_set_property(m_audio, "pause", MPV_FORMAT_FLAG, &pause);
    }
    m_playing = false;
    m_tick.stop();
    m_waiting = false;
    m_resync = false;
    setBuffering(false);
    for (MpvItem *s : m_surfs) s->setPaused(true);
    emit playingChanged();
}

void Show::togglePlay() { playing() ? pause() : play(); }

void Show::stop() {
    pause();
    if (m_audio) {
        const char *cmd[] = {"stop", nullptr};
        mpv_command(m_audio, cmd);
    }
    m_currentClip = -1;
    m_cutIdx = 0;
    setPos(0);
    releaseSurfaces();
    emit currentClipChanged();
    emit cutIndexChanged();
}

void Show::seek(double t) {
    if (!m_audio) return;
    const QByteArray v = QByteArray::number(t, 'f', 3);
    const char *cmd[] = {"seek", v.constData(), "absolute+exact", nullptr};
    mpv_command(m_audio, cmd);
    setPos(t);
    if (!ready()) return;
    // Re-cue the segment covering the new time so the picture follows the song.
    const int idx = segmentAt(t);
    const bool wasPlaying = m_playing;
    m_waiting = false;
    m_resync = false;
    if (wasPlaying) pause();
    startSegment(idx, t);
    // Resume through the same gate a stall uses: the song only starts once the
    // picture at the new position has something to show, so a seek cannot leave
    // sound ahead of image.
    if (wasPlaying) { play(); beginWait(); }
}

void Show::cueSegment(int index) {
    if (index < 0 || index >= m_segs.size() || index >= m_cuts.size()) return;
    const bool wasPlaying = m_playing;
    if (wasPlaying) pause();
    setPos(m_cuts[index]);
    if (m_audio) {
        const QByteArray v = QByteArray::number(m_cuts[index], 'f', 3);
        const char *cmd[] = {"seek", v.constData(), "absolute+exact", nullptr};
        mpv_command(m_audio, cmd);
    }
    startSegment(index, m_cuts[index]);
    if (wasPlaying) { play(); beginWait(); }
}

void Show::startSegment(int idx, double songTime) {
    if (m_surfs.isEmpty() || idx < 0 || idx >= m_segs.size()) return;
    if (pathFor(m_segs[idx], nullptr).isEmpty()) return;

    int surf = spareSurface();
    // One surface only, or all of them busy: replace the picture where it stands.
    const bool inPlace = surf < 0;
    if (inPlace) surf = m_frontIdx;
    if (surf < 0) return;

    // This one is about to be on screen and playing: full readahead, not a warm one.
    loadInto(surf, idx, targetFor(idx, songTime), -1);
    m_surfs[surf]->setPaused(!m_playing);
    for (int s = 0; s < m_surfs.size(); s++)
        if (s != surf) m_surfs[s]->setPaused(true);

    m_outgoingIdx = inPlace ? -1 : m_frontIdx;
    m_frontIdx = surf;
    m_cutIdx = idx;
    m_currentClip = m_segs[idx].id;
    m_lastFadeMs = (idx < m_fades.size()) ? m_fades[idx] : 120;

    emit currentClipChanged();
    emit cutIndexChanged();
    // In place, the picture is already on the surface being written to: there is
    // nothing to fade from, so no crossfade is asked for.
    if (!inPlace) emit crossfade(surf, int(std::lround(m_lastFadeMs)));
    m_sinceCut.restart();
    // A new clip starts on its own buffer: reset stall tracking for it.
    m_lastFrameTs = -1;
    m_seekStreak = 0;
    m_sinceFrameAdvance.restart();
    fillLookahead();
    // A seek lands on a shot's own framing even while paused: tick() only runs
    // during playback, so this is what a scrubbed, stopped picture shows.
    applyView(songTime);
}

void Show::revealSegment(int idx, double songTime) {
    if (m_surfs.isEmpty() || idx < 0 || idx >= m_segs.size()) return;
    // Within a quarter second counts as ready: the tick can be a frame late, and
    // the picture is corrected in place below if it is further off than that.
    const double want = targetFor(idx, songTime);
    const int held = surfaceHolding(idx, want, 0.25);
    if (held < 0) {
        // The load did not make it in time: open it now and take the hitch.
        startSegment(idx, songTime);
        return;
    }
    if (held == m_frontIdx) {
        // Already the shot on screen (a cut so short the tick skipped it).
        m_cutIdx = idx;
        m_currentClip = m_segs[idx].id;
        m_surfs[held]->setPaused(!m_playing);
        emit currentClipChanged();
        emit cutIndexChanged();
        fillLookahead();
        return;
    }

    // Warm and ready: the cut is a swap, not a file open. This is what makes a
    // quarter-second shot land on time.
    m_surfs[held]->setReadahead(-1);   // now it is the one playing
    m_surfs[held]->setPaused(!m_playing);
    if (std::fabs(m_holdAt[held] - want) > 0.10) {
        // Open, but parked on another frame (the song moved on while it sat warm):
        // a seek inside a file that is already open is cheap, a reload is not.
        m_surfs[held]->seekExact(want);
        m_holdAt[held] = want;
    }
    for (int s = 0; s < m_surfs.size(); s++)
        if (s != held) m_surfs[s]->setPaused(true);

    m_outgoingIdx = m_frontIdx;
    m_frontIdx = held;
    m_cutIdx = idx;
    m_currentClip = m_segs[idx].id;
    m_lastFadeMs = (idx < m_fades.size()) ? m_fades[idx] : 120;

    emit currentClipChanged();
    emit cutIndexChanged();
    emit crossfade(held, int(std::lround(m_lastFadeMs)));
    m_sinceCut.restart();
    m_lastFrameTs = -1;
    m_seekStreak = 0;
    m_sinceFrameAdvance.restart();
    fillLookahead();
}

void Show::tick() {
    pollAudio();
    // Pump the visible surface at the song clock's rate: under advanced control
    // mpv decodes only when render() is called, and wakeup callbacks alone are
    // not frequent enough to keep video flowing.
    if (m_playing)
        if (MpvItem *f = frontSurface()) f->requestFrame();
    if (!m_playing || m_segs.isEmpty()) return;

    MpvItem *front = frontSurface();
    if (!front) return;

    // ---- holding for the file -------------------------------------------
    // The song is paused at the position the picture stalled on. Wait until the
    // surface has enough buffered to play *through the rest of this segment*,
    // re-syncing the picture with one seek first, then let both continue
    // together. Resuming early would only stall again a few frames later.
    if (m_waiting) {
        const double want = expectedVideoTime(m_cutIdx, m_pos);
        if (!m_resync && std::fabs(front->frameTime() - want) > 0.35) {
            front->seekExact(want);
            m_resync = true;
            m_sinceWaitStart.restart();
            return;
        }
        const double segEnd = (m_cutIdx + 1 < m_cuts.size()) ? m_cuts[m_cutIdx + 1] : m_dur;
        const double leftInSegment = std::max(0.0, segEnd - m_pos);
        // Enough for a smooth minute of playback, or everything the segment has
        // left, whichever is smaller.
        // Keep the bar below what the demuxer is allowed to read ahead: asking
        // for more buffered media than the cache can hold waits forever.
        const double need = std::min(1.0, leftInSegment + 0.1);
        const bool ready = front->bufferedSecs() >= need;
        // If mpv never reports a cache size, fall back to "it has had a moment".
        const double waited = m_sinceWaitStart.isValid() ? m_sinceWaitStart.elapsed() : 0;
        const bool blind = !front->hasBufferInfo() && waited > 1500;
        // Safety net: a dead share must not freeze the app with no way back. Play
        // what there is; if it starves again the gate simply re-engages.
        const bool giveUp = waited > 15000;
        if (ready || blind || giveUp || leftInSegment < 0.25) {
            resumeFromWait();
            return;
        }
        return;   // stay held: nothing else may advance while the song is stopped
    }

    // Advance on the audio clock: the song is the only authority on time.
    if (m_cutIdx + 1 < m_cuts.size() && m_pos >= m_cuts[m_cutIdx + 1]) {
        int next = m_cutIdx + 1;
        while (next + 1 < m_cuts.size() && m_pos >= m_cuts[next + 1]) next++;
        // The shot the song has reached, from whichever surface is already holding
        // it. Only a load that did not finish in time falls back to opening it now.
        revealSegment(next, m_pos);
        front = frontSurface();
    } else {
        // While a shot plays, keep every spare surface warm for the cuts ahead.
        fillLookahead();
    }

    // The move follows the song clock, so it is updated whether or not a cut just
    // happened: a shot that is about to end is at the far end of its push.
    applyView(m_pos);

    // Stall detection before drift correction: if the visible surface stops
    // producing frames while the song runs, it is waiting on the file. Hold the
    // song there rather than letting it run ahead; correcting the clock of a
    // starved demuxer throws its buffer away and restarts the fetch.
    if (front) {
        const double ft = front->frameTime();
        if (ft != m_lastFrameTs) {
            m_lastFrameTs = ft;
            m_sinceFrameAdvance.restart();
            m_seekStreak = 0;
        } else if (m_sinceFrameAdvance.isValid() && m_sinceFrameAdvance.elapsed() > 1200) {
            beginWait();
            return;
        }
    }

    // Drift guard: if the on-screen clip wanders off the song by more than a
    // frame or two, pull it back. Small drift is left alone so playback stays
    // smooth, and repeated corrections that do not stick back off exponentially.
    if (front && !m_buffering) {
        const double want = expectedVideoTime(m_cutIdx, m_pos);
        // Just after a cut the surface is still opening the new file and its
        // clock reports the OLD clip's time; correcting then would stack seeks on
        // top of the one the cut already issued.
        const bool settled = m_sinceCut.isValid() ? m_sinceCut.elapsed() > 700 : true;
        const double cooldownMs = std::min(8000.0, 500.0 * std::pow(2.0, m_seekStreak));
        const bool cooled = !m_sinceCorrection.isValid() || m_sinceCorrection.elapsed() >= cooldownMs;
        if (settled && cooled && !front->paused() && std::fabs(front->frameTime() - want) > 0.15) {
            front->seekExact(want);
            m_sinceCorrection.restart();
            m_seekStreak = std::min(m_seekStreak + 1, 4);
        }
    }

    // The song ran out: land on the play button instead of a stale picture.
    if (m_dur > 0 && m_pos >= m_dur - 0.12) {
        pause();
        setPos(m_dur);
    }
}

