// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "settings.h"
#include "views/contentview.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>

#include <array>

class KWindowShadow;

namespace KDEGlance
{
class ImageView;
class MediaView;
class InfoView;
class CornerMask;
class Toast;
class RemoteResolver;
class TextView;
class PdfView;
class OfficeView;

/*
 * The preview window: a frameless, translucent top-level with rounded
 * corners and a compositor-drawn shadow (KWindowShadow, no invisible margins
 * that would catch the mouse). It hosts one ContentView at a time.
 *
 * Window management (spec 5):
 *  - move: compositor-driven QWindow::startSystemMove() from the content;
 *  - resize: thin handles on the edges and corners start
 *    QWindow::startSystemResize(); with Alt held at the start of the drag the
 *    window keeps the photo aspect ratio (other views: the current window ratio), answering each
 *    size the compositor proposes with the ratio-locked size (KWin anchors it
 *    at the opposite edge);
 *  - placement: by KWin through the placement script (centered on the active
 *    screen's work area, remembers the user's position within a session).
 *
 * Sizes changed by the user are remembered per content category.
 */
class PreviewWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewWindow(Settings *settings, QWidget *parent = nullptr);
    ~PreviewWindow() override;

    // Shows an item. A new session starts with a hidden window: it appears
    // as soon as the content is ready (or after a short timeout).
    void showItem(const PreviewItem &item, bool newSession);
    void closePreview();
    bool isPreviewVisible() const;

    void setPhotoCompositorManaged(bool managed);
    void setWorkArea(const QRect &area); // logical pixels, panels excluded
    QRect workArea() const;
    void prefetch(const QList<PreviewItem> &items);
    void applySettings();

    // --- state for tests ------------------------------------------------------
    ContentView *currentView() const;
    ImageView *imageView() const;
    MediaView *mediaView() const;
    InfoView *infoView() const;
    TextView *textView() const { return m_textView; }
    PdfView *pdfView() const { return m_pdfView; }
    OfficeView *officeView() const { return m_officeView; }
    SizeCategory currentCategory() const;
    bool aspectLockActive() const;
    Q_INVOKABLE void beginInteractiveResize(Qt::Edges edges, bool keepAspect); // also used by tests
    Q_INVOKABLE void endInteractiveResize();
    bool userResizedThisSession() const;
    PreviewItem currentItem() const;

    static QSize defaultSizeFor(SizeCategory category);

Q_SIGNALS:
    void openExternallyRequested(const QUrl &url);
    void closed();
    void navigateRequested(int delta);
    void settingsModified();
    void settingsResetRequested();
    void currentContentReady();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void loadLocalItem(const PreviewItem &item);
    void switchTo(ContentView *view);
    void onViewReady();
    void onViewFailed(const QString &message);
    void onNaturalSizeChanged();
    void onCategoryChanged(SizeCategory category);
    void revealIfPending();
    QSize targetSizeFor(SizeCategory category, const QSize &natural) const;
    void applyTargetSize(bool force);
    // Origin is explicit: resizeEvent alone never grants persistence.
    enum class ResizeOrigin { Automatic, Manual, Wheel, Compositor };
    void resizeProgrammatically(const QSize &size, ResizeOrigin origin = ResizeOrigin::Automatic);
    void layoutOverlays();
    void handleMiddleClick();
    void resetActiveModule();
    void toggleFullscreen();
    void updateShadow();
    void updateDecodeTarget();

    RemoteResolver *m_remote;
    Settings *m_settings;
    ImageView *m_imageView;
    MediaView *m_mediaView;
    InfoView *m_infoView;
    TextView *m_textView;
    PdfView *m_pdfView;
    OfficeView *m_officeView;
    ContentView *m_current = nullptr;
    CornerMask *m_cornerMask;
    Toast *m_toast;
    std::array<QWidget *, 8> m_handles{};
    KWindowShadow *m_shadow = nullptr;

    PreviewItem m_item;
    QRect m_workArea;
    SizeCategory m_category = SizeCategory::Image;
    quint64 m_itemGeneration = 0;
    bool m_sessionActive = false;
    bool m_revealPending = false;
    QTimer m_revealTimer;
    bool m_wasActive = false;
    QElapsedTimer m_shownClock;
    QElapsedTimer m_ignoreDeactivateClock;

    // Interactive resize state.
    bool m_resizing = false;
    bool m_aspectLock = false;
    Qt::Edges m_resizeEdges;
    QSize m_resizeStartSize;
    QSize m_resizeRatio;
    QElapsedTimer m_resizeClock;
    QTimer m_resizeEndTimer;
    ResizeOrigin m_resizeOrigin = ResizeOrigin::Automatic;
    bool m_photoCompositorManaged = false;
    std::array<bool, 7> m_userSized{}; // per SizeCategory, this session

    // Middle button: single click closes, double click resets only the active module.
    QTimer m_middleClickTimer;
};

} // namespace KDEGlance
