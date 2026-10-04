#pragma once
#include "NativeCanvas.h"
#include "graphics/BrushSession.h"
#include "graphics/GrowingBrushSession.h"
#include "graphics/CloneCursorPreview.h"
#include "editing/Selection.h"
#include "editing/SelectionGesture.h"
#include <QChronoTimer>
#include "layers/LayerOperations.h"
#include "editing/PixelEdits.h"
#include "editing/GradientPreview.h"
#include "editing/Shapes.h"
#include "editing/DocumentGeometry.h"
#include "retouch/RetouchSession.h"
#include "editing_transform/TransformSessionState.h"
#include <QToolBar>
#include <QCheckBox>
#include "PaletteDialog.h"
#include "EditPanelSession.h"
#include "ProjectToolState.h"
#include "CommandRegistry.h"
#include "text/TextRaster.h"
#include <QStackedWidget>
#include <QPointer>
#include <QMainWindow>
#include <QTabWidget>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QSlider>
#include <QAction>
#include <QColor>
#include <memory>
#include <unordered_set>
class QMimeData;
class QInputMethodEvent;

namespace compositor {
namespace ui {class ImportQueue;class WorkspaceDropQueue;class LayerOpacityField;class PaletteSwatches;}
struct EditorProject {
    std::optional<Document> document;
    std::string active;
    std::vector<std::string> selected;
    bool maskSelected{};
    bool importing{},projectBusy{};
    ProjectToolState toolState;
    retouch::CloneAlignment cloneAlignment;
    bool cloneSampleAllLayers{};
    std::unordered_set<std::string> collapsedGroups;
    History history;
    CompositeCache composite;
    std::shared_ptr<const graphics::GrowingBrushSnapshot> brushPreview;
    std::shared_ptr<const LayerRenderPreview> textPreview;
    std::shared_ptr<const LayerRenderPreview> retouchPreview;
    std::shared_ptr<const editing::GradientPreview> gradientPreview;
    std::optional<std::pair<std::string,Blend>> blendPreview;
    std::shared_ptr<const Document> effectPreview;
    std::string brushLayerId;
    QString path;
    QString defaultTitle{"Untitled"};
    QStackedWidget* page{};
    QWidget* welcome{};
    NativeCanvas* canvas{};
};
class MainWindow final:public QMainWindow {
    bool warp_,refreshing_{};
    bool managingProjectOpen_{},selectingProjectForOpen_{};
    QTabWidget* tabs_{};
    QTreeWidget* layers_{};
    QComboBox* blend_{};
    QSlider* opacity_{};
    ui::LayerOpacityField* opacityPercent_{};
    QComboBox* target_{};
    std::array<QDoubleSpinBox*,5> geometry_{};
    QAction *undo_{},*redo_{};
    std::vector<std::unique_ptr<EditorProject>> projects_;
    EditorProject* activeProject_{};
    ui::ImportQueue* importQueue_{};
    ui::WorkspaceDropQueue* workspaceDrops_{};
    ui::CommandRegistry* commands_{};
    bool closingWindow_{};
    int nextProjectNumber_{2};
    QColor foreground_{Qt::black};
    using Tool=ProjectTool;
    Tool tool_{Tool::Move};
    ProjectBrushMode brushMode_{ProjectBrushMode::Paint};
    QPointer<PaletteDialog> colorPicker_;
    ui::PaletteSwatches* paletteSwatches_{};
    QPointer<QWidget> maskPalettePopup_;
    EditorProject* paletteTarget_{};
    bool paletteWasMaskSelected_{};
    QPointer<ui::EditPanelSession> editPanel_;
    std::optional<QPoint> palettePosition_;
    bool samplingPalette_{},showSampleRing_{true};
    QColor sampleOriginal_;
    bool zoomDragging_{},zoomMoved_{};
    bool spaceHeld_{},spaceDragging_{};
    QPointF spacePress_;
    double zoomStart_{};
    QPointF zoomAnchor_;
    retouch::Settings cloneSettings_{}, blurSettings_{retouch::Mode::Liquify};
    retouch::Mode healingMode_{retouch::Mode::HealContentAware};
    std::unique_ptr<retouch::RetouchSession> retouch_;
    EditorProject* retouchOwner_{};
    std::optional<Layer> retouchOriginal_;
    retouch::Mode retouchMode_{};
    bool retouchMask_{};
    std::optional<Point> retouchAxisAnchor_;
    std::optional<bool> retouchAxisHorizontal_;
    std::array<QDoubleSpinBox*,3> retouchTip_{};
    std::array<QSlider*,2> retouchSliders_{};
    QComboBox *healingModes_{}, *blurModes_{};
    QToolBar* retouchBar_{};
    QCheckBox *cloneAligned_{}, *cloneAllLayers_{};
    QColor background_{Qt::white};
    editing::GradientSettings gradientSettings_;
    editing::ShapeStyle shapeStyle_;
    std::optional<Layer> drawingOriginal_;
    EditorProject* gradientOwner_{};
    Point gradientStart_,gradientEnd_;
    int gradientHandle_{-1};
    bool gradientMask_{};
    TextContent textDefaults_{};
    struct TextSession {
        EditorProject* owner{};
        std::string layerId;
        double originX{},originY{};
        TextContent style;
        int caret{},anchor{};
        struct Step { TextContent style; int caret{},anchor{}; };
        std::vector<Step> undo,redo;
        std::optional<TextContent> fontPreviewOriginal;
        std::string preedit;
        text::TextLayout layout;
    };
    std::optional<TextSession> textSession_;
    bool fontChoiceKept_{};
    QPointer<QComboBox> textFontBox_;
    QPointer<QComboBox> textStyleBox_;
    bool textSelecting_{};
    bool textArming_{};
    Point textArmPoint_{};
    int textHandle_{-1};
    Transform textResizeStart_{};
    bool textResizeChanged_{};
    std::optional<TextContent> colorBeforePicker_;
    std::string shapeDraftId_;
    editing::ShapeStyle shapeDraftStyle_;
    std::optional<editing::Rect> shapeDraftRect_;
    std::optional<editing::Rect> cropDraft_;
    QString cropRatioChoice_{"Free"};
    editing::LassoKind lassoKind_{editing::LassoKind::Freehand};
    int selectionExpandAmount_{1},selectionContractAmount_{1};
    std::optional<editing::CropDrag> cropDrag_;
    std::optional<editing::CropSnap> cropSnap_;
    bool ellipse_{},selectionAntialias_{true},movingSelection_{};
    editing::SelectionMode selectionMode_{editing::SelectionMode::Replace};
    std::optional<Selection> selectionBefore_;
    editing::SelectionGesture selectionGesture_;
    EditorProject* selectionOwner_{};
    QChronoTimer* selectionScrollTimer_{};
    std::optional<Point> selectionScrollPoint_;
    Qt::KeyboardModifiers selectionModifiers_{};
    graphics::BrushSessionSettings brushSettings_;
    struct BrushTipDrag {QPointF start;double radius{},hardness{};bool hardnessShown{};};
    std::optional<BrushTipDrag> brushTipDrag_;
    std::optional<QPointF> brushPointer_;
    Qt::KeyboardModifiers brushPointerModifiers_{};
    std::array<QDoubleSpinBox*,3> brushTip_{};
    std::array<QSlider*,2> brushSliders_{};
    std::optional<std::pair<int,qint64>> opacityDigit_;
    EditorProject* opacityOwner_{};
    bool strokeMask_{},maskPaintWhite_{};
    std::optional<Point> lastBrushPoint_;
    std::string lastBrushLayer_;
    bool lastBrushMask_{};
    std::unique_ptr<graphics::GrowingBrushSession> stroke_;
    EditorProject* pointerOwner_{};
    std::shared_ptr<graphics::D3D11BrushCoverage> brushGpu_;
    graphics::CloneCursorPreview cloneCursorPreview_;
    bool cloneCrosshairShown_{};
    Point press_;
    std::optional<Transform> moving_;
    std::optional<editing_transform::Drag> transformDrag_;
    std::unique_ptr<editing_transform::TransformSessionState> transformSession_;
    EditorProject* transformOwner_{};
    bool autoSelectLayers_{},transformControls_{true};
    std::optional<Document> transformBefore_;
    std::vector<std::string> transformIds_;
    bool lockRatio_{true},snapping_{true};
    QPointF panOrigin_;
    EditorProject* current();
    Layer* active();
    bool canEditLayers();
    bool canEditAppearance();
    void captureToolState(EditorProject&)const;
    void restoreToolState(const EditorProject&);
    void initializeProject(EditorProject&);
    EditorProject* dropDestinationAt(QPoint,std::optional<Point>&);
    bool canReceiveLayerDrop(const QMimeData*,QPoint,Qt::KeyboardModifiers);
    bool receiveLayerDrop(const QMimeData*,QPoint,Qt::KeyboardModifiers);
    void switchProject();
    bool canSwitchProjects();
    ui::CommandState commandState(EditorProject* owner=nullptr);
    void initializeCommands();
    void bindCommand(QAction*,const QString& menu,const QString& label,std::function<void()>);
    ui::ImportQueue* ensureImportQueue();
    ui::WorkspaceDropQueue* ensureWorkspaceDropQueue();
    void queueImageImports(const QStringList&,EditorProject*,std::optional<Point> = {});
    bool importPhotoshop(const QString& path, bool confirm=true);
    void refresh(bool render=true,bool rebuildLayers=true);
    void edit(const char* name,const std::function<void(Document&)>& operation);
    QAction* action(QMenu*,const QString&,const QKeySequence&,std::function<void()>);
    void newDialog();
    void checkForUpdates();
    void importImage();
    void exportImage();
    void openProjectDialog();
    bool loadProjectDirectory(const QString&);
    bool saveProject(bool saveAs=false);
    bool closeProject(int);
    void pointerBegin(QPointF,Qt::KeyboardModifiers,int clickCount=1);
    void pointerUpdate(QPointF,Qt::KeyboardModifiers);
    void pointerEnd(QPointF,Qt::KeyboardModifiers);
    void pointerCancel();
    void interruptPointer();
    void selectTool(Tool);
    std::optional<double> cropRatio();
    void changeCropRatio();
    void cancelShape();
    void refreshShapeControls();
    void cancelCrop();
    void refreshCropControls();
    void resizeSelection(bool expand,double amount);
    bool handleEditingKey(QKeyEvent*);
    bool beginTemporaryHand(Point);
    bool updateTemporaryHand(Point,bool finish);
    void cancelTemporaryHand();
    void setupBrushControls();
    void openPalette(bool background=false);
    void setupPaletteControls(QToolBar*);
    void refreshPaletteControls();
    void openEditPanelColor(effects_tools::PaletteColor,const QString&,std::function<void(effects_tools::PaletteColor)>);
    void closeEditPanelColor(bool commit);
    void swapPalette();
    void resetPalette();
    bool beginPalette(Point,Qt::KeyboardModifiers);
    bool updatePalette(Point,bool finish);
    void refreshBrushControls();
    void updateBrushPointer(QPointF,Qt::KeyboardModifiers);
    void clearBrushPointer();
    void refreshBrushPointer();
    bool beginBrushTip(QPointF,Qt::KeyboardModifiers);
    bool updateBrushTip(QPointF,Qt::KeyboardModifiers,bool finish);
    void cancelBrushTip();
    bool canvasNavigationAllowed();
    void finishOpacityEdit();
    QWidget* createLayerOpacityControl();
    void refreshOpacityPercent();
    void detachOpacityPercent();
    void restoreHistorySnapshot(Snapshot);
    void previewBlendMode(std::optional<Blend>);
    void setLayerBlendMode(Blend);
    void editTransformGeometry(int,double);
    void changeTransformDraft(const std::function<void(Transform&)>&);
    Point transformScalePixelSize();
    void setupRetouchActions();
    void refreshRetouchControls();
    bool beginRetouch(Point,Qt::KeyboardModifiers);
    bool updateRetouch(Point,Qt::KeyboardModifiers);
    bool endRetouch(Point,Qt::KeyboardModifiers);
    void cancelRetouch();
    void publishRetouch(bool finish);
    Point constrainRetouch(Point,Qt::KeyboardModifiers);
    bool beginBrush(Point,Qt::KeyboardModifiers = Qt::NoModifier);
    bool updateBrush(Point,bool finish);
    void publishBrush(std::shared_ptr<const graphics::GrowingBrushSnapshot>,bool finish=false);
    CompositeViewport brushViewport(EditorProject&,double,double,double,double,double);
    void setupAdjustmentActions();
    ui::EditPanelHost makeEditPanelHost(EditorProject&,const Document&,std::string action);
    std::shared_ptr<const Document> editPanelPreview(EditorProject&)const;
    void populateEditPanelCommandState(ui::CommandState&,EditorProject*) const;
    bool beginEditPanelPointer(Point,Qt::KeyboardModifiers);
    bool updateEditPanelPointer(Point,Qt::KeyboardModifiers,bool finish);
    void cancelEditPanel();
    void adjust(const QString& kind,bool live,bool existing=false);
    void removeBackground();
    void runFilter(int);
    void setupSelectionActions();
    void runWand(Point,Qt::KeyboardModifiers,std::optional<editing::SelectionMode> modeOverride={});
    int wandTolerance_{32},wandSampleRadius_{};
    bool wandObjectMode_{},snapToGuides_{true};
    bool wandContiguous_{true},wandAllLayers_{};
    bool beginSelection(Point,Qt::KeyboardModifiers,int clickCount=1);
    bool updateSelection(Point,Qt::KeyboardModifiers,bool finish);
    void finishPolygon();
    void finishSelectionGesture();
    void cancelSelectionGesture();
    void refreshSelectionGesture();
    void hoverSelection(QPointF,Qt::KeyboardModifiers);
    void selectionModifiersChanged(Qt::KeyboardModifiers);
    void updateSelectionAutoscroll(Point);
    void stopSelectionAutoscroll();
    void stepSelectionAutoscroll();
    bool eventFilter(QObject*,QEvent*) override;
    void copySelection(bool merged,bool cut=false,bool viaLayer=false);
    void addPixelLayer(std::shared_ptr<const Raster>,Point,const char*);
    void pasteSelection();
    void pixelEdit(int operation);
    void setupLayerActions();
    void newBlankLayer();
    void finishVisibilitySwipe();
    EditorProject* visibilityOwner_{};
    void refreshLayerPanel();
    void maskCommand(int);
    void selectLayerTarget(const std::string&,bool);
    void loadLayerSelection(const std::string&,bool,Qt::KeyboardModifiers);
    layers::SelectionState layerSelection() const;
    void applyLayerEdit(layers::EditResult);
    void mergeLayers();
    void layerCommand(int);
    void editLayerEffect(int);
    void setupTransformActions();
    bool startTransformSession(bool persistent,bool distort=false);
    void applyTransformSession();
    void cancelTransformSession();
    void publishTransformSession(bool finish);
    void updateTransformActions();
    std::optional<Transform> selectedTransform();
    bool beginTransform(Point,Qt::KeyboardModifiers);
    void updateTransform(Point,Qt::KeyboardModifiers,bool finish);
    void setupDrawingActions();
    void setupTypeControls();
    void refreshTypeControls();
    void beginTextPointer(Point,Qt::KeyboardModifiers,int clickCount);
    void updateTextPointer(Point,bool finish);
    void beginText(Point,bool forceNew=false,bool extend=false,int clickCount=1);
    void beginParagraph(Point,Point);
    bool editTextAt(Point,int clickCount);
    void updateTextCaret(Point);
    int hitTextHandle(Point);
    void resizeTextBox(Point);
    void showTextPointer();
    void focusTextCanvas();
    bool finishText();
    void cancelText();
    bool handleTextKey(QKeyEvent*);
    void handleTextInput(QInputMethodEvent*);
    void publishTextEdit();
    void previewTextFont(const std::string&);
    void previewTextStyle(const std::string&);
    void keepTextFontPreview();
    void endTextFontPreview();
    void applyTextFont(const std::string&);
    void applyTextStyle(const std::string&);
    void applyTextSize(double);
    void applyTextColor(double,double,double);
    void previewTextColor(double,double,double);
    void applyTextAlignment(TextAlignment);
    void applyTextTracking(double);
    void applyTextLeading(double);
    bool undoTextTyping(bool redo);
    void beginGradient(Point);
    void updateGradient(Point,Qt::KeyboardModifiers,bool);
    void refreshGradient();
    void applyGradient();
    void cancelGradient();
    void documentSizeDialog(bool imageSize);
    bool beginDrawing(Point,Qt::KeyboardModifiers);
    bool updateDrawing(Point,Qt::KeyboardModifiers,bool finish);
    void applyCrop();
protected:
    void closeEvent(QCloseEvent*)override;
    void keyPressEvent(QKeyEvent*)override;
    void keyReleaseEvent(QKeyEvent*)override;
    void changeEvent(QEvent*)override;
    void dragEnterEvent(QDragEnterEvent*)override;
    void dropEvent(QDropEvent*)override;
public:
    explicit MainWindow(bool warp=false);
    ~MainWindow()override;
    EditorProject& addEmptyProject(bool reuseEmpty=true);
    EditorProject& addProject(Document d,QString title="Untitled",bool reuseEmpty=true);
    void addFeasibilityDocument();
    void openPath(const QString&);
    void receiveDropPaths(const QStringList&,NativeCanvas* destination=nullptr,std::optional<Point> point={});
    bool openProjectPaths(const QStringList&);
    NativeCanvas* canvas(){auto*p=current();return p?p->canvas:nullptr;}
    QString brushAdapterName()const{return brushGpu_?QString::fromStdString(brushGpu_->adapterName()):QString{};}
    void exerciseNativeUi(const QString& evidenceDirectory);
};
}
