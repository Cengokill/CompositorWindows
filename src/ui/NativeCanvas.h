#pragma once
#include "core/Document.h"
#include "editing/SelectionGesture.h"
#include "editing/Shapes.h"
#include "editing_transform/TransformGeometry.h"
#include "graphics/CanvasViewport.h"
#include "platform/DisplayProfile.h"
#include "platform/DisplayProfileWatcher.h"
#include <QInputMethodEvent>
#include <QVariant>
#include <QWidget>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <unordered_map>
#include <d3d11.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace compositor {
class NativeCanvas final:public QWidget {
public:
    struct ShapeDraftOverlay {editing::Rect rect;editing::ShapeKind kind;double cornerRadius;Pixel fill;double lineWidth{4};};
    struct TextCaretOverlay {
        std::shared_ptr<const Raster> raster;
        double originX{},originY{},width{},height{};
        struct Line { double x0{},y0{},x1{},y1{}; };
        std::vector<Line> carets;
        int caret{-1};
        struct Box { double x{},y{},width{},height{}; };
        std::vector<Box> selection;
    };
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> factory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_,image_;
    std::shared_ptr<const Raster> raster_;
    std::shared_ptr<const GrayRaster> selection_;
    std::shared_ptr<const editing::SelectionOutline> selectionInputOutline_,selectionOutline_;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> selectionGeometry_;
    std::optional<std::array<float,6>> selectionMatrix_;
    QTimer* selectionTimer_{};
    int selectionPhase_{};
    std::optional<Transform> transformOverlay_;
    std::optional<editing_transform::Corners> distortionOverlay_;
    std::optional<std::pair<Point,Point>> gradientLine_;
    std::optional<ShapeDraftOverlay> shapeDraft_;
    std::optional<TextCaretOverlay> textOverlay_;
    std::shared_ptr<const Raster> textRasterSource_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> textBitmap_;
    void drawTextOverlay();
    std::optional<editing::Rect> cropOverlay_;
    std::optional<double> snapGuideX_,snapGuideY_;
    std::optional<editing::LassoDraft> selectionDraft_;
    struct BrushCursor {Point center;double diameter;std::optional<double> hardness;std::optional<Point> marker;std::shared_ptr<const Raster> preview;double opacity{1};};
    std::optional<BrushCursor> brushCursor_;
    std::shared_ptr<const Raster> brushPreviewSource_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> brushPreviewBitmap_;
    struct SampleRing {Point position;Pixel original,sampled;};
    std::optional<SampleRing> sampleRing_;
    int documentWidth_{},documentHeight_{};
    double rasterX_{},rasterY_{},rasterUnits_{1};
    struct DisplayTile { std::shared_ptr<const Raster::Tile> source; Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap; };
    std::unordered_map<const Raster::Tile*,DisplayTile> displayTiles_;
    platform::DisplayProfile displayProfile_;
    std::unique_ptr<platform::DisplayProfileWatcher> profileWatcher_;
    std::optional<std::filesystem::path> displayProfileOverride_;
    QPointer<QWidget> profileWindow_;
    QMetaObject::Connection profileScreenConnection_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> presentationScene_;
    Microsoft::WRL::ComPtr<ID2D1Effect> presentationEffect_;
    bool profileKnown_{},presentationAttempted_{};
    QString presentationDiagnostic_;
    uint64_t presentationBytes_{},profileDiscoveryCount_{};
    uint64_t presentationByteBudget_{128ULL*1024*1024};
    bool warp_,dragging_{},rightDragging_{},panning_{},tabletActive_{};
    double backingScale_{1};
    bool followsFit_{true};
    QPointF last_;
    QString error_;
    void createDevice();
    void createTarget();
    void upload();
    void releaseDevice();
    void draw(bool present=true);
    void drawSelection();
    void updateSelectionAnimation();
    graphics::CanvasViewport viewportState()const;
    void installViewport(const graphics::CanvasViewport&);
    void synchronizeViewport();
    void watchPresentationWindow();
    void resetPresentationResources();
    void preparePresentation();
    HRESULT finishPresentation();
public:
    explicit NativeCanvas(bool warp,QWidget* parent=nullptr);
    ~NativeCanvas() override;
    double zoom{1}; // Physical display pixels per document pixel.
    QPointF pan; // Logical view pixels (DIPs).
    bool showPixelGrid{};
    bool showGuides{};
    std::vector<std::pair<bool,double>> guides;
    std::function<void(QPointF,Qt::KeyboardModifiers)> pointerDown,pointerMove,pointerUp;
    std::function<void(QPointF,Qt::KeyboardModifiers)> pointerDoubleClick;
    std::function<void()> pointerCancel;
    std::function<void()> pointerInterrupted;
    // Hover and right-drag coordinates are logical view pixels (DIPs).
    std::function<void(QPointF,Qt::KeyboardModifiers)> pointerHover;
    std::function<void()> pointerLeave;
    std::function<bool(QPointF,Qt::KeyboardModifiers)> rightPointerDown;
    std::function<void(QPointF,Qt::KeyboardModifiers,bool)> rightPointerMove;
    std::function<bool()> navigationAllowed;
    std::function<bool(QKeyEvent*)> textKey;
    std::function<void(QInputMethodEvent*)> textInput;
    std::function<CompositeViewport(double,double,double,double,double)> viewportProvider;
    void setRaster(std::shared_ptr<const Raster>);
    void setDocumentSize(int width,int height);
    void setSelection(std::shared_ptr<const GrayRaster>,std::shared_ptr<const editing::SelectionOutline> outline={});
    void setSelectionDraft(std::optional<editing::LassoDraft> value){selectionDraft_=std::move(value);update();}
    const std::optional<editing::LassoDraft>& selectionDraft()const{return selectionDraft_;}
    void setTransformOverlay(std::optional<Transform> value){transformOverlay_=value;update();}
    void setDistortionOverlay(std::optional<editing_transform::Corners> value){distortionOverlay_=value;update();}
    // Document coordinates. Source endpoints draw at radius6 DIPs; caller hit radius10 DIPs.
    void setGradientLine(std::optional<std::pair<Point,Point>> value){gradientLine_=value;update();}
    void setShapeDraft(std::optional<ShapeDraftOverlay> value){shapeDraft_=std::move(value);update();}
    void setTextOverlay(std::optional<TextCaretOverlay> value);
    const std::optional<ShapeDraftOverlay>& shapeDraft()const{return shapeDraft_;}
    void setCropOverlay(std::optional<editing::Rect> value){cropOverlay_=value;update();}
    const std::optional<editing::Rect>& cropOverlay()const{return cropOverlay_;}
    static std::optional<int> cropResizeHandle(editing::Rect,Point viewPoint,const editing_transform::ViewMapping&);
    void setSnapGuides(std::optional<double> x={},std::optional<double> y={}){snapGuideX_=x;snapGuideY_=y;update();}
    void setBrushCursor(std::optional<Point> logicalCenter,double logicalDiameter,std::optional<double> hardness={},std::optional<Point> marker={},std::shared_ptr<const Raster> preview={},double opacity=1){if(logicalCenter)brushCursor_=BrushCursor{*logicalCenter,logicalDiameter,hardness,marker,std::move(preview),opacity};else brushCursor_.reset();update();}
    void setSampleRing(std::optional<Point> position,Pixel original={},Pixel sampled={}){if(position)sampleRing_=SampleRing{*position,original,sampled};else sampleRing_.reset();update();}
    editing_transform::ViewMapping viewMapping()const;
    void fit();
    void zoomAt(double value,QPointF anchor);
    double pointsPerPixel()const{return zoom/backingScale_;}
    bool followsFit()const{return followsFit_;}
    void panBy(QPointF logicalDelta);
    QPointF documentPoint(QPointF) const;
    QString deviceError()const{return error_;}
    bool deviceReady()const{return bool(context_);}
    QString adapterName()const;
    void recreateDevice();
    QImage captureRendered();
    // Override is local to this canvas; null restores Windows monitor discovery.
    void setDisplayProfileOverride(std::optional<std::filesystem::path>);
    void setPresentationBudget(uint64_t bytes);
    void refreshDisplayProfile();
    QString presentationProfileHash()const{return QString::fromStdString(displayProfile_.sha256);}
    QString presentationDiagnostic()const{return presentationDiagnostic_;}
    bool presentationConvertsColor()const{return bool(presentationEffect_);}
    uint64_t presentationBytes()const{return presentationBytes_;}
    uint64_t profileDiscoveryCount()const{return profileDiscoveryCount_;}
    std::vector<platform::DisplayProfileWatchStatus> profileWatchStatuses()const{return profileWatcher_?profileWatcher_->statuses():std::vector<platform::DisplayProfileWatchStatus>{};}
    QPaintEngine* paintEngine() const override{return nullptr;}
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void inputMethodEvent(QInputMethodEvent*) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;
    void tabletEvent(QTabletEvent*) override;
    void leaveEvent(QEvent*) override;
    bool event(QEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
};
}
