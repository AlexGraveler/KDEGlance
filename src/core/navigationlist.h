// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QList>
#include <QUrl>

#include <optional>

namespace KDEGlance
{

/*
 * Ordered list of files the user can step through with the arrow keys.
 * The order is whatever the provider says (Dolphin's view order, or the
 * fallback directory listing). Stepping stops at both ends.
 *
 * When the list is refreshed and the current file is gone (deleted, renamed,
 * filtered out) the position is remembered as a gap, so "next" and
 * "previous" still lead to the former neighbours.
 */
class NavigationList
{
public:
    void reset(const QList<QUrl> &urls, const QUrl &current);
    void update(const QList<QUrl> &urls);
    void clear();

    QUrl current() const;
    int index() const; // -1 if empty
    int count() const;
    bool currentIsMissing() const;
    const QList<QUrl> &urls() const;

    // URL `delta` steps away from the current one, if any.
    std::optional<QUrl> peek(int delta) const;
    // Moves by delta; returns false (and does nothing) at the ends.
    bool step(int delta);
    // Makes url current if it is in the list.
    bool setCurrent(const QUrl &url);

private:
    QList<QUrl> m_urls;
    QUrl m_current;
    int m_index = -1; // index of m_current, or insertion point if missing
    bool m_missing = false;
};

/*
 * Fallback when no Dolphin view provides the order: regular files of the
 * directory sorted like Dolphin's default (natural, case-insensitive, by
 * name). Hidden files are included only if `current` is hidden itself.
 */
QList<QUrl> listDirectoryForNavigation(const QUrl &current);

// Natural, case-insensitive comparison: -1, 0, 1.
int naturalCompare(const QString &a, const QString &b);

} // namespace KDEGlance
