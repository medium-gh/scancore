#include "UpdateRules.h"

#include <QDateTime>
#include <QHash>
#include <QTimeZone>

#include <algorithm>
#include <functional>
#include <utility>
#include <numeric>

namespace sc::updates {

bool finishedStatus(const QString &status) {
    const auto s = status.trimmed().toLower();
    if (s.isEmpty())
        return false;
    // Words sites use for a work that has ended, in the content languages.
    static const QStringList words{
        QStringLiteral("complete"),  QStringLiteral("finished"),    QStringLiteral("ended"),       QStringLiteral("concluído"),
        QStringLiteral("concluido"), QStringLiteral("completo"),    QStringLiteral("finalizado"),  QStringLiteral("terminé"),
        QStringLiteral("terminado"), QStringLiteral("tamat"),       QStringLiteral("selesai"),     QStringLiteral("abgeschlossen"),
        QStringLiteral("완결"),       QStringLiteral("完結"),         QStringLiteral("完结"),          QStringLiteral("завершен"),
        QStringLiteral("завершён"),  QStringLiteral("tamamlandı"),  QStringLiteral("hoàn thành"),  QStringLiteral("concluso"),
        QStringLiteral("จบแล้ว")};
    for (const auto &w : words)
        if (s.contains(w))
            return true;
    return false;
}

bool shouldCheck(const Entry &entry, const Rules &rules) {
    const auto category = entry.category.isEmpty() ? QStringLiteral("reading") : entry.category;
    if (!rules.lists.contains(category))
        return false;
    if (rules.skipFinished && finishedStatus(entry.status))
        return false;
    if (rules.skipNotStarted && !entry.started)
        return false;
    if (rules.skipUnread && entry.knownUnits > entry.readUnits)
        return false;
    return true;
}

std::optional<qint64> releaseDate(const QString &text) {
    if (text.size() < 10 || !text.at(0).isDigit())
        return std::nullopt;
    auto at = QDateTime::fromString(text, Qt::ISODate);
    if (!at.isValid())
        at = QDateTime(QDate::fromString(text.left(10), Qt::ISODate), QTime(12, 0), QTimeZone::UTC);
    if (!at.isValid())
        return std::nullopt;
    return at.toSecsSinceEpoch();
}

Rhythm rhythm(const QList<qint64> &releases, qint64 now) {
    constexpr qint64 day = 86400;
    auto sorted = releases;
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    // Chapters out the same day are one release.
    QList<qint64> distinct;
    for (const auto at : sorted)
        if (distinct.isEmpty() || distinct.last() - at >= day / 2)
            distinct.append(at);
    Rhythm r;
    if (distinct.size() < 3)
        return r;
    QList<double> gaps;
    for (int i = 1; i < distinct.size(); ++i)
        gaps.append(double(distinct[i - 1] - distinct[i]) / day);
    std::sort(gaps.begin(), gaps.end());
    const double gap = gaps.at(gaps.size() / 2);
    if (gap < 0.8 || gap > 35)
        return r;
    const qint64 step = qint64(gap * day);
    const qint64 next = distinct.first() + step;
    // More than a gap overdue: it has stopped keeping time.
    if (next < now - step)
        return r;
    r.known = true;
    r.gapDays = gap;
    r.next = next;
    r.late = next < now;
    return r;
}

bool due(const QList<qint64> &releases, qint64 lastChecked, qint64 now) {
    constexpr qint64 day = 86400;
    constexpr qint64 most = 28 * day;
    // A day either way: sites date chapters in their own time zone.
    constexpr qint64 grace = day;
    if (lastChecked <= 0 || now - lastChecked >= most)
        return true;
    // Releases, newest first, chapters out the same day as one; none dated
    // ahead of time (some sites list chapters before they are out).
    auto sorted = releases;
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    QList<qint64> days;
    for (const auto at : std::as_const(sorted))
        if (at <= now + grace && (days.isEmpty() || days.last() - at >= day / 2))
            days.append(at);
    if (days.size() > 10)
        days.resize(10);
    if (days.size() < 3)
        return true;
    QList<qint64> gaps;
    for (int i = 1; i < days.size(); ++i)
        gaps.append(days[i - 1] - days[i]);
    std::sort(gaps.begin(), gaps.end());
    const qint64 gap = std::min(gaps.at((gaps.size() - 1) / 2), most);
    if (gap < day)
        return true;
    const qint64 last = days.first();
    const qint64 sinceRelease = now - last;
    // Slack seen lately: how much later than the gap releases have come.
    const qint64 lateness = std::clamp<qint64>(gaps.last() - gap, 0, gap / 2);

    // On a break: looked at every gap, doubled while the break outlasts
    // four of them.
    if (sinceRelease > 4 * gap) {
        qint64 step = gap;
        while (step < most && sinceRelease > 4 * step)
            step *= 2;
        return now - lastChecked >= std::min(step, most) - day / 4;
    }
    // The latest release expected by now; none since the last one: not due.
    const qint64 expected = last + ((now + grace - last) / gap) * gap;
    if (expected <= last)
        return false;
    // Not looked at since it was expected, or still within how late
    // releases have come.
    return lastChecked < expected - grace || now <= expected + lateness + grace;
}

QList<QList<int>> groupsSharingKeys(const QList<QStringList> &keys) {
    QList<int> parent(keys.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int i) {
        int root = i;
        while (parent[root] != root)
            root = parent[root];
        while (parent[i] != root)
            i = std::exchange(parent[i], root);
        return root;
    };
    QHash<QString, int> owner;
    for (int i = 0; i < keys.size(); ++i)
        for (const auto &key : keys[i]) {
            if (const auto it = owner.constFind(key); it != owner.cend())
                parent[find(i)] = find(*it);
            else
                owner.insert(key, i);
        }
    QHash<int, QList<int>> groups;
    for (int i = 0; i < keys.size(); ++i)
        groups[find(i)].append(i);
    QList<QList<int>> out;
    for (const auto &group : std::as_const(groups))
        if (group.size() > 1)
            out.append(group);
    std::sort(out.begin(), out.end(), [](const QList<int> &a, const QList<int> &b) { return a.first() < b.first(); });
    return out;
}

} // namespace sc::updates
