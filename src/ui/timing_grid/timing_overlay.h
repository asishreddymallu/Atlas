#pragma once
/**
 * @file timing_overlay.h
 * @brief T6: the Pipeline timing panel, laid over the bottom of the central
 *        area instead of docked beside it.
 *
 * A dock takes its height from the central area, so opening it shrank the
 * editor, the circuit and the register panel. This widget is placed by hand
 * over the bottom of a "host" (the window's central widget) and sits outside
 * every layout, so showing or hiding it moves nothing else.
 *
 * It is a direct child of the main window, not of the host, so the window's
 * QObject tree keeps the shape the dock gave it (harnesses enumerate the
 * window's actions and spin boxes in tree order). It follows the host's
 * geometry through an event filter.
 *
 * The top edge is a drag strip; the height is held between kMinHeight and the
 * space under `topLimit` (the ribbon), and the one the user chose is saved
 * through AppSettings when the drag ends.
 */
#include <QPointer>
#include <QWidget>

class QLabel;
class QToolButton;

namespace Kites
{
class TimingOverlay : public QWidget
{
    Q_OBJECT
  public:
    static constexpr int    kMinHeight      = 150;
    static constexpr double kDefaultFraction = 0.4; ///< of the window, before the user picks a height

    /// `content` is reparented into the overlay below its title row.
    TimingOverlay(const QString &title, QWidget *content, QWidget *parent);

    /// The widget to lie over (its bottom edge, full width) and the one whose
    /// bottom edge it may not rise above. Call once both are laid out in the
    /// window; until then the overlay stays hidden where it is.
    void setHost(QWidget *host, QWidget *topLimit);

    /// The height the user chose (or the default), before clamping to the host.
    int  preferredHeight() const { return m_height; }
    /// The tallest it can be now: the host's height under topLimit.
    int  maximumOverlayHeight() const;

  signals:
    /// Shown or hidden explicitly (not when the whole window hides).
    void visibilityChanged(bool visible);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool event(QEvent *event) override;

  private:
    void reposition();
    int  clampHeight(int height) const;

    QPointer<QWidget> m_host;
    QPointer<QWidget> m_topLimit;
    QWidget          *m_grip  = nullptr;
    QToolButton      *m_close = nullptr;
    int               m_height = 0; ///< 0: not chosen yet, the default applies on first show
    int               m_dragStartY = 0;
    int               m_dragStartHeight = 0;
    bool              m_dragging = false;
};
} // namespace Kites
