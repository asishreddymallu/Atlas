#pragma once
/**
 * @file circuit_view.h
 * @brief The processor diagram's view: Fit by default, zoom 25 % to 400 %.
 *
 * Fit (the default) shows the whole diagram and refits when the view is
 * resized. Zooming leaves Fit: Ctrl + mouse wheel (anchored under the
 * cursor), a touchpad pinch, the buttons at the bottom right (-, +, Fit,
 * 100%), or Ctrl+plus / Ctrl+minus / Ctrl+0 (Fit) while the diagram has
 * focus. When zoomed, the left button drags to pan and scroll bars appear
 * as needed. Everything is drawn as vectors with antialiasing, so text and
 * lines stay sharp at every zoom and on high-DPI screens.
 *
 * The zoom belongs to the view, not the scene: stepping, running, undoing
 * and redrawing leave it alone. Only Fit, or a new scene (a processor
 * change, showScene()), returns to Fit.
 */
#include <QGraphicsView>

class QToolButton;

namespace Atlas
{
class CircuitView : public QGraphicsView
{
    Q_OBJECT
  public:
    static constexpr double kMinZoom  = 0.25;
    static constexpr double kMaxZoom  = 4.0;
    static constexpr double kZoomStep = 1.25; ///< one button press or key press

    explicit CircuitView(QWidget *parent = nullptr);
    explicit CircuitView(QGraphicsScene *scene, QWidget *parent = nullptr);

    /// Shows `scene` (a new processor's diagram) at Fit.
    void showScene(QGraphicsScene *scene);

    /// The current scale (1.0 = 100 %).
    double zoom() const;
    bool   isFit() const { return m_fit; }
    /// The scale Fit uses for the current viewport, clamped to the range.
    double fitZoom() const;
    /// The scene area Fit shows: every item, with a small margin.
    QRectF fitRect() const;

    static double clampZoom(double z);

  public slots:
    /// Zooms to `z` (clamped), keeping the scene point under `anchor`
    /// (viewport coordinates) where it is; the view centre by default.
    void setZoom(double z);
    void setZoomAt(double z, const QPoint &anchor);
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoom100();

  signals:
    void zoomChanged(double zoom, bool fit);

  protected:
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool viewportEvent(QEvent *event) override;

  private:
    void init();
    void applyFit();
    void applyMode();
    void placeButtons();

    bool         m_fit = true;
    QWidget     *m_buttons = nullptr;
    QToolButton *m_zoomOut = nullptr, *m_zoomIn = nullptr, *m_fitButton = nullptr, *m_100 = nullptr;
};
} // namespace Atlas
