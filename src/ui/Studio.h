#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QUrl>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QSet>
#include <memory>
#include "core/Params.h"
#include "core/Types.h"

class Library;
class Scheduler;
class ClipModel;
class TimelineModel;
class Show;

// The one object the QML studio talks to. It owns the document (sources, song,
// params, filters, profiles), mediates the worker passes, and exposes
// plain-language state instead of the 33 raw knobs.
class Studio : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(QString songPath READ songPath NOTIFY songChanged)
    Q_PROPERTY(QString songName READ songName NOTIFY songChanged)
    Q_PROPERTY(bool songAnalysed READ songAnalysed NOTIFY analysisChanged)
    Q_PROPERTY(QString songSummary READ songSummary NOTIFY analysisChanged)
    // Beats per minute, so beat-based settings can be shown as seconds.
    Q_PROPERTY(double bpm READ bpm NOTIFY analysisChanged)
    // The track as features: what the song is like, measured from its audio.
    Q_PROPERTY(double energy READ energy NOTIFY analysisChanged)
    Q_PROPERTY(double danceability READ danceability NOTIFY analysisChanged)
    Q_PROPERTY(double valence READ valence NOTIFY analysisChanged)
    Q_PROPERTY(double brightness READ brightness NOTIFY analysisChanged)
    Q_PROPERTY(double loudnessDb READ loudnessDb NOTIFY analysisChanged)
    Q_PROPERTY(QString trackKey READ trackKey NOTIFY analysisChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int scoreDone READ scoreDone NOTIFY progressChanged)
    Q_PROPERTY(int scoreTotal READ scoreTotal NOTIFY progressChanged)
    // 0..1 over whatever pass is running: scanning first, then scoring.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    // "Scored 40 of 126 · ~3 min left" while a pass runs, empty otherwise.
    Q_PROPERTY(QString progressLabel READ progressLabel NOTIFY progressChanged)
    // What the app is doing right now, in two words: "Scanning footage",
    // "Scoring clips", "Listening to the song". Empty when it is idle.
    Q_PROPERTY(QString phase READ phase NOTIFY progressChanged)
    // False while a pass makes the workspace unsafe to touch: the library is
    // being renumbered under the open edit, or there is no analysed song yet.
    Q_PROPERTY(bool workspaceEnabled READ workspaceEnabled NOTIFY progressChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    // Every look the picker offers: built-in presets first, then saved ones, each
    // marked if it is the active look.
    Q_PROPERTY(QVariantList looks READ looks NOTIFY looksChanged)
    Q_PROPERTY(QString activePreset READ activePreset NOTIFY activePresetChanged)
    Q_PROPERTY(QVariantMap params READ params NOTIFY paramsChanged)
    Q_PROPERTY(QStringList days READ days NOTIFY filtersChanged)
    Q_PROPERTY(QVariantMap filters READ filters NOTIFY filtersChanged)
    Q_PROPERTY(int poolCount READ poolCount NOTIFY filtersChanged)
    Q_PROPERTY(int libraryCount READ libraryCount NOTIFY libraryChanged)
    Q_PROPERTY(QString lookSummary READ lookSummary NOTIFY paramsChanged)
    Q_PROPERTY(bool hasEdit READ hasEdit NOTIFY editChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
public:
    explicit Studio(QObject *parent = nullptr);
    ~Studio() override;

    QVariantList sources() const;
    QString songPath() const { return m_song; }
    QString songName() const;
    bool songAnalysed() const { return m_analysis.valid(); }
    QString songSummary() const;
    double bpm() const { return m_analysis.valid() ? m_analysis.bpm : 0.0; }
    double energy() const { return m_analysis.valid() ? m_analysis.energy : 0.0; }
    double danceability() const { return m_analysis.valid() ? m_analysis.danceability : 0.0; }
    double valence() const { return m_analysis.valid() ? m_analysis.valence : 0.0; }
    double brightness() const { return m_analysis.valid() ? m_analysis.brightness : 0.0; }
    double loudnessDb() const { return m_analysis.valid() ? m_analysis.loudnessDb : -60.0; }
    QString trackKey() const { return m_analysis.valid() ? m_analysis.keyName : QString(); }
    // Derived: the library is scanning/scoring, or a song is being analysed.
    // One flag shared by both is what used to hide the scan progress behind a
    // finished analysis.
    bool busy() const;
    QString status() const { return m_status; }
    int scoreDone() const;
    int scoreTotal() const;
    double progress() const;
    QString progressLabel() const;
    QString phase() const;
    bool workspaceEnabled() const;
    QVariantList profiles() const;
    QVariantList looks() const;
    QString activePreset() const { return m_activePreset; }
    QVariantMap params() const { return m_params.toMap(); }
    QStringList days() const;
    QVariantMap filters() const;
    int poolCount() const;
    int libraryCount() const;
    QString lookSummary() const;
    bool hasEdit() const;
    double volume() const;

    Library *library() const { return m_lib; }
    ClipModel *clipModel() const { return m_clips; }
    TimelineModel *timeline() const { return m_timeline; }
    Show *show() const { return m_show; }

    void setVolume(double v);

    Q_INVOKABLE void startIndexing();
    // QML pickers and drops hand over QUrl: it must be converted with
    // toLocalFile(), or a path with a space arrives percent-encoded and every
    // open fails.
    Q_INVOKABLE void importUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void removeSource(const QString &path);
    Q_INVOKABLE void removeClip(int clipId);
    Q_INVOKABLE void loadSong(const QUrl &url);
    Q_INVOKABLE void applyPreset(const QString &name);
    Q_INVOKABLE void setParam(const QString &key, const QVariant &value);
    Q_INVOKABLE void setFilter(const QString &key, const QVariant &value);
    Q_INVOKABLE void generate();
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void stopShow();
    Q_INVOKABLE void seek(double t);
    Q_INVOKABLE void cueSegment(int index);
    Q_INVOKABLE void newInPointAt(int clipId, double songTime);
    Q_INVOKABLE void saveProfile(const QString &name);
    Q_INVOKABLE void loadProfile(const QString &name);
    Q_INVOKABLE void deleteProfile(const QString &name);
    Q_INVOKABLE void saveNow();
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE QString formatTime(double t) const;
    Q_INVOKABLE void openFolderDialog();
    Q_INVOKABLE void openFilesDialog();
    Q_INVOKABLE void openSongDialog();


signals:
    void sourcesChanged();
    void songChanged();
    void analysisChanged();
    void busyChanged();
    void statusChanged();
    void progressChanged();
    void profilesChanged();
    void looksChanged();
    void activePresetChanged();
    void paramsChanged();
    void filtersChanged();
    void libraryChanged();
    void editChanged();
    void volumeChanged();
    void folderDialogRequested();
    void filesDialogRequested();
    void songDialogRequested();


private:
    void reloadLibraryModel();
    void setStatus(const QString &s);
    void setSongBusy(bool b);
    void syncScheduler();
    void persist();
    // Set the named look in use. It feeds two things the QML sees — the picker's
    // rows (their `active` flag lives in looks()) and the activePreset property —
    // so both signals come from here. Setting one without the other is exactly
    // how the picker's label fell out of step with the look that was applied.
    void setActiveLook(const QString &preset, const QString &profile);
    void refreshProfiles();
    void restoreSession();
    void writeProfiles();
    void applyFilters(const QVariantMap &m);
    void loadSongFile(const QString &path);
    void noteProgress(int done, int total);
    QString etaSuffix(int done, int total) const;
    QString etaFraction(double frac) const;
    QString configDir() const;

    Library *m_lib = nullptr;
    std::unique_ptr<Scheduler> m_sched;
    ClipModel *m_clips = nullptr;
    TimelineModel *m_timeline = nullptr;
    Show *m_show = nullptr;

    QList<MediaSource> m_sources;
    QSet<QString> m_hidden;
    QString m_song;
    Params m_params;
    Analysis m_analysis;
    Filters m_filters;
    QString m_activePreset;
    // Name of a saved look when one is in use; empty while a preset is active.
    QString m_activeProfile;
    QString m_status;
    bool m_songBusy = false;
    bool m_pendingRegenerate = false;
    // Set while a library change must not recut the edit: a clip removed from the
    // bin leaves the ids alone, so only its shots need dropping.
    bool m_keepEditOnIndex = false;
    // True when the current edit was cut before scoring finished; it is re-cut
    // once real scores land, unless the user already edited it by hand.
    bool m_editUnscored = false;
    // Bumped per song load so a slow decode of a song the user already replaced
    // cannot install its analysis over the newer one.
    int m_songGen = 0;
    // Progress timing for the ETA: reset whenever a new pass (or a new total)
    // starts, so the rate is measured on the phase actually running.
    QElapsedTimer m_phaseClock;
    int m_phaseTotal = 0;
    QVariantMap m_profiles;

};
