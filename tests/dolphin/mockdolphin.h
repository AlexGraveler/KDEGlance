// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

// A stand-in for the parts of Dolphin's libdolphinprivate that KDEGlance's
// hook talks to. Class names, object ownership and the signatures of the
// functions the hook resolves at runtime are the same as in Dolphin
// (src/kitemviews, src/views), so the hook runs against this mock exactly as
// it runs inside Dolphin. Behaviour is reduced to what the tests observe.

#include <KFileItem>

#include <QAbstractScrollArea>
#include <QGraphicsWidget>
#include <QList>
#include <QMainWindow>
#include <QSet>
#include <QUrl>

struct KItemRange {
    int index = 0;
    int count = 0;
};
class KItemRangeList : public QList<KItemRange>
{
};

class KFileItemModel : public QObject
{
    Q_OBJECT
public:
    explicit KFileItemModel(QObject *parent = nullptr);
    int count() const;
    KFileItem fileItem(int index) const;
    int index(const QUrl &url) const;

    // test API
    void setItems(const KFileItemList &items);
    void insertItem(int index, const KFileItem &item);
    void removeItem(int index);

Q_SIGNALS:
    void itemsInserted(const KItemRangeList &itemRanges);
    void itemsRemoved(const KItemRangeList &itemRanges);
    void itemsMoved(const KItemRange &itemRange, const QList<int> &movedToIndexes);
    void directoryLoadingCompleted();
    void fileItemsChanged(const KFileItemList &changedFileItems);

private:
    KFileItemList m_items;
};

class KItemListSelectionManager : public QObject
{
    Q_OBJECT
public:
    enum SelectionMode {
        Select,
        Deselect,
        Toggle
    };
    explicit KItemListSelectionManager(QObject *parent = nullptr);
    int currentItem() const;
    bool isSelected(int index) const;
    bool hasSelection() const;
    void setCurrentItem(int current);
    void clearSelection();
    void setSelected(int index, int count = 1, SelectionMode mode = Select);
    void beginAnchoredSelection(int anchor);
    void endAnchoredSelection();

    // test API
    QSet<int> selection() const;
    int anchor() const;
    int changes = 0;

private:
    int m_current = -1;
    QSet<int> m_selection;
    int m_anchor = -1;
};

class KItemListController : public QObject
{
    Q_OBJECT
public:
    explicit KItemListController(QObject *parent = nullptr);
    bool isSearchAsYouTypeActive() const;
    bool selectionMode() const;

    bool typeAheadActive = false;
    bool selectionModeActive = false;
};

class KItemListView : public QGraphicsWidget
{
    Q_OBJECT
public:
    enum ViewItemPosition {
        Beginning,
        Middle,
        End,
        Nearest
    };
    void scrollToItem(int index, ViewItemPosition viewItemPosition = Nearest);
    int lastScrolled = -1;
    int lastPosition = -1;
};

class KItemListContainer : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit KItemListContainer(QWidget *parent = nullptr);
    KItemListController *controller() const;
    KItemListSelectionManager *selectionManager() const;
    KItemListView *listView() const;
    QList<int> keysReceived;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    KItemListController *m_controller;
    KItemListSelectionManager *m_selection;
    KItemListView *m_view;
};

class DolphinView : public QWidget
{
    Q_OBJECT
public:
    explicit DolphinView(QWidget *parent = nullptr);
    KFileItemModel *model() const;
    KItemListContainer *container() const;
    // Selects one item like a click would.
    void selectOnly(int index);

Q_SIGNALS:
    void selectionChanged(const KFileItemList &selection);

private:
    KFileItemModel *m_model;
    KItemListContainer *m_container;
};

class DolphinMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit DolphinMainWindow(int views = 1);
    DolphinView *view(int i) const;

private:
    QList<DolphinView *> m_views;
};
