// MapWidget: paints the latest finished frame (opaque RGBX8888, produced off the GUI thread) at an integer zoom,
// point-sampled and centred on a black letterbox. It never composites.
#pragma once
#include <string>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMetaType>
#include <QWidget>
#include <memory>
#include "render/hittest.h"
#include "render/mousechain.h"

namespace fugue::shell {

using HitPtr = std::shared_ptr<const fugue::render::HitScene>;

// One pointer event on the map image, carrying the scene that was on screen when it happened, so the hit test
// answers for the picture the player saw. It crosses to the session thread through a mutex-guarded queue
// (app/sessionworker.h).
struct MapPointer {
    enum Kind { Press, Move, Release } kind = Press;
    fugue::render::PointerEvent ev;
    HitPtr scene;
};

class MapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MapWidget(QWidget* parent = nullptr);
    // `hit` is the scene the frame was drawn from (render::assembleMapControl withHit); pointer events are hit-tested
    // against it. Null (no scene yet): pointer events are dropped.
    void setFrame(const QImage& img, HitPtr hit = {});
    HitPtr hitScene() const { return hit_; }
    // A newer scene for the same picture (the render thread skipped an unchanged list): no repaint.
    void setHit(HitPtr hit) { hit_ = std::move(hit); }
    bool hasFrame() const { return !img_.isNull(); }
    // The `right-click` skin parameter: a right click reaches the wire only if true; otherwise the widget answers it
    // locally (localMenuRequested), as stock DreamSeeker's verb popup does.
    void setRightClickToWire(bool v) { rightClick_ = v; }
    bool rightClickToWire() const { return rightClick_; }
    // Widget position to image pixel (top-left origin); false outside the letterboxed image.
    bool toImage(const QPoint& pos, int& ix, int& iy, bool clampToImage) const;
    void setIconSize(int px) { iconSize_ = px; zoom_ = 0; update(); }   // skin icon-size; 0 means fit
    // Scaling. zoom (relative to world.icon_size, fractional) and icon-size (absolute px) are one property: at most
    // one is nonzero, and 0 means auto-fit. The letterbox picks fit (min) or cover (max, centre-cropped). zoom-mode
    // only selects the resampler.
    void setMapScale(double zoom, int iconSize, bool letterbox, const std::string& mode);
    const std::string& zoomMode() const { return mode_; }    // resampler in use: normal, distort or blur
    int iconSizePx() const { return iconSize_; }              // icon-size in use; 0 means auto-fit or zoom
    double scaleF() const;                                    // the factor in use (fractional when auto-fitting)
    int scale() const { return int(scaleF() + 0.5); }         // scaleF() rounded
    QRect imageRect() const;                                  // where the frame lands inside the widget
    uint64_t framesShown() const { return shown_; }
    // The latest frame at its native size, as `.screenshot` saves it (shell/screenshot.h).
    const QImage& frame() const { return img_; }
    // Name of the atom under the pointer (render::hitAt on the frame on screen, honouring mouse_opacity), or "" over
    // nothing, a nameless atom, the letterbox or off the control.
    QString hoverName() const { return hoverName_; }
signals:
    void pointer(const fugue::shell::MapPointer& p);               // press, drag motion or release on the image
    void localMenuRequested(const QPoint& globalPos, const QString& atomName);   // right click kept off the wire
    // The same right click with the scene it hit-tested against, so the session thread can build the atom's verb
    // menu (model/verbmenu.h); only that thread may read the model. Emitted only when the click landed on the image.
    void localMenuPointer(const QPoint& globalPos, const fugue::shell::MapPointer& at);
    // hoverName() changed: the main window's status bar shows it (SkinWindow).
    void hoverChanged(const QString& name);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent* e) override { e->ignore(); }   // the app-level focus filter routes keys
private:
    QImage img_;
    int iconSize_ = 0, tile_ = 32;
    double zoom_ = 0;
    bool letterbox_ = true;
    std::string mode_ = "normal";
    uint64_t shown_ = 0;
    HitPtr hit_;
    bool rightClick_ = false;
    int heldButton_ = 0;
    bool hovering_ = false;   // the pointer is over this control; hoverPos_ is where it is
    QPoint hoverPos_;
    QString hoverName_;
    void updateHover();
    void emitPointer(MapPointer::Kind k, QMouseEvent* e, int button);
};

}  // namespace fugue::shell

Q_DECLARE_METATYPE(fugue::shell::MapPointer)
Q_DECLARE_METATYPE(fugue::shell::HitPtr)
