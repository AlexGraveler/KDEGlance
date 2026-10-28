// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

class QObject;
class QWidget;

namespace KDEGlance
{

/*
 * Access to Dolphin's view internals from inside the Dolphin process.
 *
 * Dolphin has no plugin API for "the files of this view in display order" or
 * "select this file". Its view classes (DolphinView, KFileItemModel,
 * KItemListSelectionManager, ...) live in libdolphinprivate, which exports
 * them but installs no headers. KDEGlance therefore
 *  - finds the objects through Qt's object tree and meta-object class names
 *    (QObject::inherits), never by casting blindly, and
 *  - calls a handful of exported member functions resolved by name with
 *    dlsym() at runtime (Itanium C++ ABI).
 * Every symbol is checked when the hook loads. If Dolphin changes and a
 * symbol disappears, the dependent feature is switched off (fallbacks are
 * used) instead of risking a crash: a renamed or re-typed function has a
 * different mangled name and simply is not found.
 */
class DolphinAccess
{
public:
    static const DolphinAccess &instance();

    // All symbols needed for display order and selection sync were found.
    bool hasModelAccess() const;
    bool hasSelectionAccess() const;
    QStringList missingSymbols() const;

    // The DolphinView containing `widget`, or nullptr.
    static QWidget *viewFor(QWidget *widget);
    // True if `focus` is the file view itself (not the inline rename editor,
    // the location bar, a panel, the terminal, ...).
    static bool isFileViewFocus(QWidget *focus, QWidget **viewOut = nullptr);

    static QObject *modelOf(QWidget *view);
    static QWidget *containerOf(QWidget *view);
    static QObject *controllerOf(QWidget *view);
    static QObject *selectionManagerOf(QWidget *view);
    static QObject *itemListViewOf(QWidget *view);

    struct Snapshot {
        QList<QUrl> urls; // files (no folders) in display order, as local URLs
        QList<QUrl> modelUrls; // the same items as the model knows them
        int current = -1; // index into urls of the file to preview
    };

    /*
     * Files of the view and the one to preview: `preferred` (context menu)
     * if given, else the current item if it is selected (or nothing is
     * selected), else the first selected item. nullopt if there is nothing
     * to preview or the target is a folder.
     */
    std::optional<Snapshot> snapshot(QWidget *view, const std::optional<QUrl> &preferred = std::nullopt) const;
    // Cheap check used before taking the key away from Dolphin.
    bool hasFileTarget(QWidget *view) const;
    // All files of the view in display order (current = -1).
    std::optional<Snapshot> files(QWidget *view) const;

    bool isTypeAheadActive(QWidget *view, bool *known) const;
    bool isSelectionModeActive(QWidget *view) const;

    // Makes `modelUrl` the current and only selected item and scrolls to it.
    bool selectUrl(QWidget *view, const QUrl &modelUrl) const;

private:
    DolphinAccess();
    void resolve();
    int targetIndex(const QObject *model, const QObject *selection, const std::optional<QUrl> &preferred) const;
    Snapshot collect(const QObject *model, int target) const;

    struct Api;
    Api *m_api;
    QStringList m_missing;
};

} // namespace KDEGlance
