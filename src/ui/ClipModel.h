#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QPointer>
#include "core/Params.h"
#include "core/Types.h"

class Library;
class ThumbJobs;

// The library rail: one row per scored clip, with the filtered subset exposed as
// a separate role so the rail can grey out what the filters exclude.
class ClipModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY visibleCountChanged)
    // Name search over the bin: a substring, case-insensitive.
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole, PathRole, DayRole, TodRole, ScoreRole, MvRole,
        AspectRole, DurationRole, ProxyRole, ThumbRole, IndexRole, FilteredRole
    };
    explicit ClipModel(QObject *parent = nullptr);
    ~ClipModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setLibrary(Library *lib);
    int visibleCount() const { return m_visible; }
    QString search() const { return m_search; }
    void setSearch(const QString &s);

    // Clip ids currently passing the filters, in rail order.
    Q_INVOKABLE QVariantList visibleIds() const;
    Q_INVOKABLE QString thumbFor(int id) const;
    Q_INVOKABLE void ensureThumbs(int first, int last);

    // Filters decide which rows are "in the pool"; Studio sets them.
    void setFilters(const Filters &f);
    const Filters &filters() const { return m_filters; }
    // Clip ids that pass the filters, in rail order (the pool for an edit).
    QList<int> passingIds() const;

    // Params/filters changed: recompute the filtered set and thumbnails.
    Q_INVOKABLE void refresh();

signals:
    void countChanged();
    void visibleCountChanged();
    void searchChanged();

private:
    void requestThumb(int row);
    // Rebuild the row list from the library and the current search.
    void rebuild();

    Library *m_lib = nullptr;
    QList<int> m_ids;          // clip ids in rail order
    QSet<int> m_passing;
    int m_visible = 0;
    Filters m_filters;
    QString m_search;
    QHash<int, QString> m_thumbs;   // clip id -> cached thumbnail path
    QSet<QString> m_pendingThumbs;  // cache keys with an extraction already running
    ThumbJobs *m_jobs = nullptr;
};
