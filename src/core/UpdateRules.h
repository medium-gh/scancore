#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace sc::updates {

// Which saved series the new-chapter check looks at (Settings > Library).
struct Rules {
    // Library lists checked: reading, planned, paused, completed, dropped.
    QStringList lists{QStringLiteral("reading"), QStringLiteral("planned"), QStringLiteral("paused"), QStringLiteral("completed")};
    // Series the site marks as finished.
    bool skipFinished = true;
    // Series with chapters already there to read.
    bool skipUnread = false;
    // Series never opened.
    bool skipNotStarted = false;
};

struct Entry {
    QString category;
    QString status;
    int knownUnits = 0;
    int readUnits = 0;
    bool started = false;
};

bool shouldCheck(const Entry &entry, const Rules &rules);

// A site's publication status saying the series is over ("Completed",
// "Finalizado", "완결", ...).
bool finishedStatus(const QString &status);

// A chapter's release date as sources give it (ISO date and time, or just
// the date), in seconds; nothing for text such as "3 days ago".
std::optional<qint64> releaseDate(const QString &text);

// When a series' next chapter is due, from the gaps between its last
// releases (the median, so one late chapter does not move it).
struct Rhythm {
    bool known = false;
    double gapDays = 0;
    qint64 next = 0;
    // Due already, but less than a gap ago.
    bool late = false;
};
Rhythm rhythm(const QList<qint64> &releases, qint64 now);

// Whether a check by itself should look at a series now, or it is not due
// a chapter yet (as Mihon skips series "outside the release period").
// `releases` are its recent chapters' dates, `lastChecked` when it was last
// looked at (0: never), all in seconds. From its rhythm (the median gap):
// looked at once a release is due, then at every check while it is late; on
// a break (four gaps without one) less and less often. Never more than four
// weeks between looks, and without a rhythm to go by (under three dated
// releases, or one a day) at every check.
bool due(const QList<qint64> &releases, qint64 lastChecked, qint64 now);

// Items sharing any key end up in one group (transitively); only groups of
// two or more are returned, as indexes into `keys`.
QList<QList<int>> groupsSharingKeys(const QList<QStringList> &keys);

} // namespace sc::updates
