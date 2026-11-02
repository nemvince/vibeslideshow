#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QUrl>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <memory>
#include "core/Params.h"
#include "core/Types.h"

class Library;
class Scheduler;
class ClipModel;
class TimelineModel;
class Show;

// The one object the QML studio talks to. It owns the document (dirs, song,
// params, filters, profiles), mediates the worker passes, and exposes
// plain-language state instead of the 33 raw knobs.
class Studio : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList videoDirs READ videoDirs NOTIFY videoDirsChanged)
    Q_PROPERTY(QString songPath READ songPath NOTIFY songChanged)
    Q_PROPERTY(QString songName READ songName NOTIFY songChanged)
    Q_PROPERTY(bool songAnalysed READ songAnalysed NOTIFY analysisChanged)
    Q_PROPERTY(QString songSummary READ songSummary NOTIFY analysisChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int scoreDone READ scoreDone NOTIFY progressChanged)
    Q_PROPERTY(int scoreTotal READ scoreTotal NOTIFY progressChanged)
    // 0..1 over whatever pass is running: scanning first, then scoring.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    // "Scored 40 of 126 · ~3 min left" while a pass runs, empty otherwise.
    Q_PROPERTY(QString progressLabel READ progressLabel NOTIFY progressChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList advanced READ advanced NOTIFY advancedChanged)
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

    QStringList videoDirs() const { return m_dirs; }
    QString songPath() const { return m_song; }
    QString songName() const;
    bool songAnalysed() const { return m_analysis.valid(); }
    QString songSummary() const;
    // Derived: the library is scanning/scoring, or a song is being analysed.
    // One flag shared by both is what used to hide the scan progress behind a
    // finished analysis.
    bool busy() const;
    QString status() const { return m_status; }
    int scoreDone() const;
    int scoreTotal() const;
    double progress() const;
    QString progressLabel() const;
    QVariantList presets() const;
    QVariantList advanced() const;
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

    Q_INVOKABLE void startIndexing(const QStringList &dirs);
    // QML pickers hand over QUrl: it must be converted with toLocalFile(), or a
    // path with a space arrives percent-encoded and every open fails.
    Q_INVOKABLE void addFolder(const QUrl &url);
    Q_INVOKABLE void removeFolder(const QString &dir);
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
    Q_INVOKABLE void openSongDialog();


signals:
    void videoDirsChanged();
    void songChanged();
    void analysisChanged();
    void busyChanged();
    void statusChanged();
    void progressChanged();
    void presetsChanged();
    void advancedChanged();
    void profilesChanged();
    void looksChanged();
    void activePresetChanged();
    void paramsChanged();
    void filtersChanged();
    void libraryChanged();
    void editChanged();
    void volumeChanged();
    void folderDialogRequested();
    void songDialogRequested();


private:
    void reloadLibraryModel();
    void setStatus(const QString &s);
    void setSongBusy(bool b);
    void syncScheduler();
    void persist();
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

    QStringList m_dirs;
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
