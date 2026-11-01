// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "navigationlist.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace KDEGlance
{

void NavigationList::reset(const QList<QUrl> &urls, const QUrl &current)
{
    m_urls = urls;
    m_current = current;
    m_missing = false;
    m_index = m_urls.indexOf(current);
    if (m_index < 0 && current.isValid()) {
        // The opened file must always be reachable.
        m_urls.prepend(current);
        m_index = 0;
    }
}

void NavigationList::update(const QList<QUrl> &urls)
{
    const int found = urls.indexOf(m_current);
    if (found >= 0) {
        m_urls = urls;
        m_index = found;
        m_missing = false;
        return;
    }
    // Keep the position as a gap: the next item takes the old index.
    m_urls = urls;
    m_missing = true;
    m_index = std::clamp(m_index, 0, int(m_urls.size()));
}

void NavigationList::clear()
{
    m_urls.clear();
    m_current = QUrl();
    m_index = -1;
    m_missing = false;
}

QUrl NavigationList::current() const
{
    return m_current;
}

int NavigationList::index() const
{
    return m_index;
}

int NavigationList::count() const
{
    return int(m_urls.size());
}

bool NavigationList::currentIsMissing() const
{
    return m_missing;
}

const QList<QUrl> &NavigationList::urls() const
{
    return m_urls;
}

std::optional<QUrl> NavigationList::peek(int delta) const
{
    if (m_urls.isEmpty() || delta == 0) {
        return std::nullopt;
    }
    int target;
    if (m_missing) {
        // In a gap, index points at the element after the gap.
        target = delta > 0 ? m_index + delta - 1 : m_index + delta;
    } else {
        target = m_index + delta;
    }
    if (target < 0 || target >= m_urls.size()) {
        return std::nullopt;
    }
    return m_urls.at(target);
}

bool NavigationList::step(int delta)
{
    const auto next = peek(delta);
    if (!next) {
        return false;
    }
    m_current = *next;
    m_index = m_urls.indexOf(m_current);
    m_missing = false;
    return true;
}

bool NavigationList::setCurrent(const QUrl &url)
{
    const int i = m_urls.indexOf(url);
    if (i < 0) {
        return false;
    }
    m_current = url;
    m_index = i;
    m_missing = false;
    return true;
}

int naturalCompare(const QString &a, const QString &b)
{
    // Digit runs compare by numeric value ("img2" < "img10"), the rest
    // case-insensitively and locale-aware, like Dolphin's natural sorting.
    // Implemented here instead of QCollator::setNumericMode because that is
    // not available when Qt is built without ICU.
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = a.at(i).isDigit();
        const bool db = b.at(j).isDigit();
        if (da && db) {
            qsizetype ei = i;
            qsizetype ej = j;
            while (ei < a.size() && a.at(ei).isDigit()) {
                ++ei;
            }
            while (ej < b.size() && b.at(ej).isDigit()) {
                ++ej;
            }
            QStringView na = QStringView(a).mid(i, ei - i);
            QStringView nb = QStringView(b).mid(j, ej - j);
            while (na.size() > 1 && na.front() == u'0') {
                na = na.mid(1);
            }
            while (nb.size() > 1 && nb.front() == u'0') {
                nb = nb.mid(1);
            }
            if (na.size() != nb.size()) {
                return na.size() < nb.size() ? -1 : 1;
            }
            if (const int c = na.compare(nb); c != 0) {
                return c < 0 ? -1 : 1;
            }
            i = ei;
            j = ej;
            continue;
        }
        qsizetype ei = i;
        qsizetype ej = j;
        while (ei < a.size() && !a.at(ei).isDigit()) {
            ++ei;
        }
        while (ej < b.size() && !b.at(ej).isDigit()) {
            ++ej;
        }
        const QString ta = a.mid(i, ei - i).toCaseFolded();
        const QString tb = b.mid(j, ej - j).toCaseFolded();
        if (const int c = QString::localeAwareCompare(ta, tb); c != 0) {
            return c < 0 ? -1 : 1;
        }
        i = ei;
        j = ej;
    }
    if (i < a.size()) {
        return 1;
    }
    if (j < b.size()) {
        return -1;
    }
    return 0;
}

QList<QUrl> listDirectoryForNavigation(const QUrl &current)
{
    if (!current.isLocalFile()) {
        return {current};
    }
    const QFileInfo currentInfo(current.toLocalFile());
    QDir dir = currentInfo.absoluteDir();
    QDir::Filters filters = QDir::Files | QDir::NoDotAndDotDot | QDir::Readable;
    if (currentInfo.isHidden()) {
        filters |= QDir::Hidden;
    }
    QStringList names = dir.entryList(filters, QDir::NoSort);

    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        const int c = naturalCompare(a, b);
        return c != 0 ? c < 0 : a < b; // stable tie-break for case variants
    });

    QList<QUrl> urls;
    urls.reserve(names.size());
    for (const QString &name : std::as_const(names)) {
        urls << QUrl::fromLocalFile(dir.absoluteFilePath(name));
    }
    return urls;
}

std::optional<int> verticalNavigationTarget(const QList<QRectF> &rects, int current, int direction)
{
    if (direction == 0 || current < 0 || current >= rects.size()) {
        return std::nullopt;
    }
    const int delta = direction > 0 ? 1 : -1;
    QSet<qint64> columns;
    for (const QRectF &r : rects) {
        if (r.isValid()) {
            columns.insert(qRound64(r.x() * 10));
        }
    }
    const QRectF here = rects.at(current);
    if (columns.size() <= 1 || !here.isValid()) {
        const int target = current + delta;
        if (target < 0 || target >= rects.size()) {
            return std::nullopt;
        }
        return target;
    }
    // Nearest row in that direction (2 px tolerance for rounding).
    double rowDistance = -1;
    for (const QRectF &r : rects) {
        const double dy = (r.y() - here.y()) * delta;
        if (r.isValid() && dy > 2 && (rowDistance < 0 || dy < rowDistance)) {
            rowDistance = dy;
        }
    }
    if (rowDistance < 0) {
        return std::nullopt;
    }
    std::optional<int> best;
    double bestDx = 0;
    for (int i = 0; i < rects.size(); ++i) {
        const QRectF &r = rects.at(i);
        if (!r.isValid() || std::abs((r.y() - here.y()) * delta - rowDistance) > 2) {
            continue;
        }
        const double dx = std::abs(r.center().x() - here.center().x());
        if (!best || dx < bestDx) {
            best = i;
            bestDx = dx;
        }
    }
    return best;
}

} // namespace KDEGlance
