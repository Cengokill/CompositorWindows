#pragma once
#include "core/Document.h"
#include "editing/SelectionGesture.h"
#include "editing/Shapes.h"
#include "editing/Snapping.h"
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
    // `plate` is the moving layer's full painted appearance, shifted with the draft.
    // `front` is the frozen content that must stay above the moving layer.
    struct MovePreview {
        std::string layerId;
        uint64_t gesture{};
        std::shared_ptr<const Raster> base;
        double baseX{},baseY{},baseUnits{1};
        std::shared_ptr<const Raster> layer;
        Transform placed{};
        double opacity{1};
        std::shared_ptr<const Raster> plate,front;
        double plateX{},plateY{},anchorX{},anchorY{};
    };
    struct PreviewPlate { double x{},y{}; int width{},height{}; };
    struct TextCaretOverlay {
        std::shared_ptr<const Raster> raster;
        double originX{},originY{},width{},height{};
        struct Line { double x0{},y0{},x1{},y1{}; };
        std::vector<Line> carets;
        int caret{-1};
        // Document-space corners, in order around the highlight: top-left, top-right, bottom-right, bottom-left.
        struct Quad { double x0{},y0{},x1{},y1{},x2{},y2{},x3{},y3{}; };
        std::vector<Quad> selection;
        bool showFrame{};
        std::array<Point,4> frame{};
        std::array<Point,8> handles{};
    };
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> factory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_,image_;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush1> checkerBrush_;
    float checkerScale_{};
    std::shared_ptr<const Raster> raster_;
    std::optional<MovePreview> movePreview_;
    void drawRasterTiles(const Raster& image,double docX,double docY,double units,float left,float top,double scale);
    void drawMoveSprite(double scale);
    std::shared_ptr<const GrayRaster> selection_;
    std::shared_ptr<const editing::SelectionOutline> selectionInputOutline_,selectionOutline_;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> selectionGeometry_;
    std::optional<std::array<float,6>> selectionMatrix_;
    QTimer* selectionTimer_{};
    int selectionPhase_{};
    std::optional<Transform> transformOverlay_;
    std::optional<editing_transform::Corners> distortionOverlay_;
    std::optional<std::pair<Point,Point>> gradientLine_;
    std::vector<std::pair<Point,Point>> cameraRawGuides_;
    std::optional<ShapeDraftOverlay> shapeDraft_;
    std::optional<TextCaretOverlay> textOverlay_;
    std::shared_ptr<const Raster> textRasterSource_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> textBitmap_;
    void drawTextOverlay();
    std::optional<editing::Rect> cropOverlay_;
    std::optional<double> snapGuideX_,snapGuideY_;
    editing::LayoutGrid layoutGrid_;
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
    QTimer* navigatorTimer_{};
    QTimer* viewportTimer_{};
    bool navigatorDeferred_{},navigatorDirty_{},navigating_{},viewportDeferred_{},viewportGesture_{},compositeHeld_{},viewportPresented_{};
    std::shared_ptr<const Raster> navigatorRaster_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> navigatorBitmap_;
    double navigatorOriginX_{},navigatorOriginY_{},navigatorUnits_{1};
    void refreshNavigator();
    void drawNavigator();
    void drawDocumentStandIn(D2D1_RECT_F bounds);
    bool sharpPatchCoversView() const;
    void ensureNavigatorBitmap();
    bool navigatorContains(QPointF) const;
    void navigateFrom(QPointF);
    void centerDocument(Point);
    void noteViewportInteraction();
    void settleViewport();
    bool profileKnown_{},presentationAttempted_{};
    QString presentationDiagnostic_;
    uint64_t presentationBytes_{},profileDiscoveryCount_{};
    uint64_t presentationByteBudget_{128ULL*1024*1024};
    bool warp_,dragging_{},rightDragging_{},panning_{},tabletActive_{};
    double backingScale_{1};
    bool followsFit_{true};
    bool blackSurround_{};
    QPointF last_;
    QString error_;
    void createDevice();
    void createTarget();
    void upload();
    void ensureChecker();
    void releaseDevice();
    struct GuideDrag { bool horizontal{}; double position{}; int index{-1}; QPointF view; };
    std::optional<GuideDrag> guideDrag_;
    enum class RulerBand { None, Horizontal, Vertical };
    RulerBand rulerBandAt(QPointF) const;
    std::optional<int> guideIndexAt(QPointF) const;
    bool beginGuideDrag(QPointF);
    void updateGuideDrag(QPointF);
    void finishGuideDrag(QPointF);
    void cancelGuideDrag();
    void drawRulers();
    void drawGuideOverlay();
    void drawLayoutGrid();
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
    // Layout grid lines are overlay-only; they never reach the composite raster or exports.
    const editing::LayoutGrid& layoutGrid() const{return layoutGrid_;}
    void setLayoutGrid(const editing::LayoutGrid& value){layoutGrid_=value;update();}
    std::vector<std::pair<bool,double>> guides;
    // Screen-space bands around the canvas. Tick marks use the same mapping as
    // documentPoint, so a labeled pixel stays under its tick while zooming or panning.
    static constexpr double rulerThickness=24;
    bool showRulers{};
    void setRulersVisible(bool visible);
    // Canvas-only full screen paints the area outside the document black.
    void setBlackSurround(bool black);
    // Mouse or tablet ownership of a canvas gesture. Window-level mode changes wait for it to end.
    bool pointerHeld() const{return dragging_||rightDragging_||tabletActive_||panning_||navigating_;}
    QRectF contentRect() const;
    struct GuidePreview { bool horizontal{}; double position{}; int index{-1}; };
    std::optional<GuidePreview> guidePreview() const;
    double rulerMajorInterval() const;
    std::function<void(bool horizontal,double position)> guideDragUpdated;
    std::function<void(bool horizontal,double position,int index)> guideDragFinished;
    std::function<void()> guideDragCanceled;
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
    // Reduced full-document composite. maxEdge is the longest side in pixels.
    // The canvas calls it after editing pauses, not on every repaint or brush step.
    std::function<CompositeViewport(int maxEdge)> navigatorProvider;
    struct NavigatorPlacement { bool visible{}; QRectF map; QRectF viewport; };
    NavigatorPlacement navigatorPlacement() const;
    void scheduleNavigator();
    void setNavigatorDeferred(bool deferred);
    bool navigatorDeferred() const { return navigatorDeferred_; }
    void setRaster(std::shared_ptr<const Raster>);
    // A plain layer move draws a frozen background plus the layer. The full
    // viewport composite waits until the drag ends.
    void setMovePreview(MovePreview preview);
    void setMovePreviewPlacement(Transform placed);
    void clearMovePreview();
    bool hasMovePreview() const { return movePreview_.has_value(); }
    bool previewMatches(const std::string& layerId,uint64_t gesture) const;
    std::string previewLayerId() const { return movePreview_?movePreview_->layerId:std::string{}; }
    uint64_t previewGesture() const { return movePreview_?movePreview_->gesture:0; }
    std::optional<PreviewPlate> previewPlate() const;
    // While held, paint keeps the current frame instead of composing on the UI thread.
    void setCompositeHeld(bool held);
    bool compositeHeld() const { return compositeHeld_; }
    struct ViewportBounds { double x{},y{},width{},height{},units{1}; bool valid{}; };
    ViewportBounds viewportBounds() const;
    // Install a finished composite and, when requested, drop the move sprite in the same frame.
    void presentComposite(std::shared_ptr<const Raster> raster,double x,double y,double units,bool clearPreview);
    void setDocumentSize(int width,int height);
    void setSelection(std::shared_ptr<const GrayRaster>,std::shared_ptr<const editing::SelectionOutline> outline={});
    void setSelectionDraft(std::optional<editing::LassoDraft> value){selectionDraft_=std::move(value);update();}
    const std::optional<editing::LassoDraft>& selectionDraft()const{return selectionDraft_;}
    void setTransformOverlay(std::optional<Transform> value){transformOverlay_=value;update();}
    void setDistortionOverlay(std::optional<editing_transform::Corners> value){distortionOverlay_=value;update();}
    // Document coordinates. Source endpoints draw at radius6 DIPs; caller hit radius10 DIPs.
    void setGradientLine(std::optional<std::pair<Point,Point>> value){gradientLine_=value;update();}
    // Document coordinates for Camera Raw guided-upright segments, including the in-progress stroke.
    void setCameraRawGuides(std::vector<std::pair<Point,Point>> value){cameraRawGuides_=std::move(value);update();}
    void setShapeDraft(std::optional<ShapeDraftOverlay> value){shapeDraft_=std::move(value);update();}
    void setTextOverlay(std::optional<TextCaretOverlay> value);
    const std::optional<ShapeDraftOverlay>& shapeDraft()const{return shapeDraft_;}
    void setCropOverlay(std::optional<editing::Rect> value){cropOverlay_=value;update();}
    const std::optional<editing::Rect>& cropOverlay()const{return cropOverlay_;}
    static std::optional<int> cropResizeHandle(editing::Rect,Point viewPoint,const editing_transform::ViewMapping&);
    void setSnapGuides(std::optional<double> x={},std::optional<double> y={}){snapGuideX_=x;snapGuideY_=y;update();}
    bool hasSnapGuides() const{return snapGuideX_||snapGuideY_;}
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
