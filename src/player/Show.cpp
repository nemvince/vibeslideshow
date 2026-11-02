#include "player/Show.h"
#include "player/MpvItem.h"
#include "core/Scheduler.h"
#include "core/Library.h"

#include <QFileInfo>
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
}

Show::~Show() {
    if (m_audio) mpv_terminate_destroy(m_audio);
}

void Show::attachSurfaces(QObject *a, QObject *b) {
    m_a = qobject_cast<MpvItem *>(a);
    m_b = qobject_cast<MpvItem *>(b);
}

void Show::emitReadyChanged() { emit readyChanged(); }

void Show::setEdit(const QList<TlSeg> &segs, const QList<double> &cuts, const QList<double> &fades) {
    // The timeline under playback just changed: stop cutting against a schedule
    // that no longer exists, and let the user press play on the new one.
    if (m_playing) pause();
    m_segs = segs;
    m_cuts = cuts;
    m_fades = fades;
    m_cutIdx = 0;
    m_preloaded = -1;
    m_preloadPending = -1;
    m_currentClip = -1;
    emit segmentsChanged();
    emit currentClipChanged();
    emitReadyChanged();
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
    if (!m_audio || !ready() || !m_a || !m_b) return;
    int pause = 0;
    mpv_set_property(m_audio, "pause", MPV_FORMAT_FLAG, &pause);
    m_playing = true;
    m_waiting = false;
    m_resync = false;
    m_lastFrameTs = -1;
    m_sinceFrameAdvance.restart();
    emit playingChanged();
    m_tick.start();
    if (m_currentClip < 0) {
        // Resume from wherever the song sits: cue the covering segment.
        int idx = 0;
        for (int i = 0; i < m_cuts.size(); i++)
            if (m_cuts[i] <= m_pos + 1e-6) idx = i;
        startSegment(idx, m_pos);
    } else if (auto *front = m_aIsFront ? m_a : m_b) {
        front->setPaused(false);
        maybePreload();
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
    if (MpvItem *front = m_aIsFront ? m_a : m_b) front->setPaused(true);
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
    if (MpvItem *front = m_aIsFront ? m_a : m_b) front->setPaused(false);
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
    if (m_a) m_a->setPaused(true);
    if (m_b) m_b->setPaused(true);
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
    m_preloaded = -1;
    m_preloadPending = -1;
    setPos(0);
    if (m_a) m_a->clear();
    if (m_b) m_b->clear();
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
    int idx = 0;
    for (int i = 0; i < m_cuts.size(); i++)
        if (m_cuts[i] <= t + 1e-6) idx = i;
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
    if (!m_a || !m_b || idx < 0 || idx >= m_segs.size()) return;
    const TlSeg &seg = m_segs[idx];
    double clipDur = 0;
    const QString path = pathFor(seg, &clipDur);
    if (path.isEmpty()) return;

    MpvItem *back = m_aIsFront ? m_b : m_a;
    MpvItem *front = m_aIsFront ? m_a : m_b;

    // Offset into the clip: normally zero, non-zero when resuming mid-segment.
    const double into = (idx < m_cuts.size()) ? std::max(0.0, songTime - m_cuts[idx]) : 0.0;
    const double target = std::min(seg.s + into, std::max(0.0, clipDur - 0.05));
    back->loadAndSeek(path, target);
    back->setPaused(!m_playing);

    m_aIsFront = !m_aIsFront;
    m_cutIdx = idx;
    m_currentClip = seg.id;
    m_preloaded = -1;
    m_preloadPending = idx + 1;
    m_lastFadeMs = (idx < m_fades.size()) ? m_fades[idx] : 120;
    front->setPaused(true);

    emit currentClipChanged();
    emit cutIndexChanged();
    emit crossfade(m_aIsFront, int(std::lround(m_lastFadeMs)));
    m_sinceCut.restart();
    // A new clip starts on its own buffer: reset stall tracking for it.
    m_lastFrameTs = -1;
    m_seekStreak = 0;
    m_sinceFrameAdvance.restart();

    // The outgoing surface is still on screen for m_lastFadeMs. Loading into it
    // now would show the next clip through the fade, so the preload waits.
    maybePreload();
}

void Show::maybePreload() {
    // Preloading the next clip over a link that cannot even feed the current one
    // only makes the stall worse.
    if (!m_playing || m_buffering) return;
    if (m_preloadPending < 0 || m_preloaded == m_preloadPending) return;
    if (m_sinceCut.isValid() && m_sinceCut.elapsed() < m_lastFadeMs + 60) return;
    preloadNext(m_preloadPending);
}

void Show::preloadNext(int idx) {
    if (idx < 0 || idx >= m_segs.size() || !m_a || !m_b) return;
    const TlSeg &seg = m_segs[idx];
    double clipDur = 0;
    const QString path = pathFor(seg, &clipDur);
    if (path.isEmpty()) return;
    MpvItem *back = m_aIsFront ? m_b : m_a;
    const double target = std::min(seg.s, std::max(0.0, clipDur - 0.05));
    back->loadAndSeek(path, target);
    back->setPaused(true);
    m_preloaded = idx;
}

void Show::revealCut(int next) {
    MpvItem *back = m_aIsFront ? m_b : m_a;
    MpvItem *front = m_aIsFront ? m_a : m_b;
    back->setPaused(false);
    m_aIsFront = !m_aIsFront;
    m_cutIdx = next;
    m_currentClip = m_segs[next].id;
    m_preloaded = -1;
    m_preloadPending = next + 1;
    m_lastFadeMs = (next < m_fades.size()) ? m_fades[next] : 120;
    front->setPaused(true);
    emit currentClipChanged();
    emit cutIndexChanged();
    emit crossfade(m_aIsFront, int(std::lround(m_lastFadeMs)));
    m_sinceCut.restart();
    m_lastFrameTs = -1;
    m_seekStreak = 0;
    m_sinceFrameAdvance.restart();
}

void Show::tick() {
    pollAudio();
    // Pump the visible surface at the song clock's rate: under advanced control
    // mpv decodes only when render() is called, and wakeup callbacks alone are
    // not frequent enough to keep video flowing.
    if (m_playing && m_a && m_b) (m_aIsFront ? m_a : m_b)->requestFrame();
    if (!m_playing || m_segs.isEmpty()) return;

    MpvItem *front = m_aIsFront ? m_a : m_b;
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
        if (m_preloaded == next) {
            // The next clip is already resident on the back surface: just reveal it.
            revealCut(next);
        } else {
            startSegment(next, m_pos);
        }
        front = m_aIsFront ? m_a : m_b;
    } else {
        maybePreload();
    }

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

