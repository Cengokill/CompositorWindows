#pragma once
#include "NativeCanvas.h"
#include "graphics/BrushSession.h"
#include "graphics/GrowingBrushSession.h"
#include "editing/Selection.h"
#include "editing/SelectionGesture.h"
#include <QChronoTimer>
#include "layers/LayerOperations.h"
#include "editing/PixelEdits.h"
#include "editing/Shapes.h"
#include "editing/DocumentGeometry.h"
#include "retouch/RetouchSession.h"
#include "editing_transform/TransformSessionState.h"
#include <QToolBar>
#include <QCheckBox>
#include "PaletteDialog.h"
#include "ProjectToolState.h"
#include "CommandRegistry.h"
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

namespace compositor {
namespace ui {class ImportQueue;}
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
    std::optional<Layer> gradientPreview;
    std::string brushLayerId;
    QString path;
    QString defaultTitle{"Untitled"};
    QStackedWidget* page{};
    QWidget* welcome{};
    NativeCanvas* canvas{};
};
class MainWindow final:public QMainWindow {
    bool warp_,refreshing_{};
    QTabWidget* tabs_{};
    QTreeWidget* layers_{};
    QComboBox* blend_{};
    QSlider* opacity_{};
    QComboBox* target_{};
    std::array<QDoubleSpinBox*,5> geometry_{};
    QAction *undo_{},*redo_{};
    std::vector<std::unique_ptr<EditorProject>> projects_;
    EditorProject* activeProject_{};
    ui::ImportQueue* importQueue_{};
    ui::CommandRegistry* commands_{};
    bool closingWindow_{};
    int nextProjectNumber_{2};
    QColor foreground_{Qt::black};
    using Tool=ProjectTool;
    Tool tool_{Tool::Move};
    QPointer<PaletteDialog> colorPicker_;
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
    std::string shapeDraftId_;
    std::optional<editing::Rect> cropDraft_;
    QString cropRatioChoice_{"Free"};
    editing::LassoKind lassoKind_{editing::LassoKind::Freehand};
    int selectionExpandAmount_{1},selectionContractAmount_{1};
    std::optional<editing::CropDrag> cropDrag_;
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
    std::optional<std::pair<int,qint64>> opacityDigit_;
    EditorProject* opacityOwner_{};
    bool strokeMask_{},maskPaintWhite_{};
    std::optional<Point> lastBrushPoint_;
    std::string lastBrushLayer_;
    bool lastBrushMask_{};
    std::unique_ptr<graphics::GrowingBrushSession> stroke_;
    EditorProject* pointerOwner_{};
    std::shared_ptr<graphics::D3D11BrushCoverage> brushGpu_;
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
    void switchProject();
    bool canSwitchProjects();
    ui::CommandState commandState(EditorProject* owner=nullptr);
    void initializeCommands();
    void bindCommand(QAction*,const QString& menu,const QString& label,std::function<void()>);
    ui::ImportQueue* ensureImportQueue();
    void queueImageImports(const QStringList&,EditorProject*,std::optional<Point> = {});
    void refresh(bool render=true,bool rebuildLayers=true);
    void edit(const char* name,const std::function<void(Document&)>& operation);
    QAction* action(QMenu*,const QString&,const QKeySequence&,std::function<void()>);
    void newDialog();
    void importImage();
    void exportImage();
    void openProjectDialog();
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
    void resizeSelection(bool expand,double amount);
    bool handleEditingKey(QKeyEvent*);
    bool beginTemporaryHand(Point);
    bool updateTemporaryHand(Point,bool finish);
    void cancelTemporaryHand();
    void setupBrushControls();
    void openPalette(bool background=false);
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
    void restoreHistorySnapshot(const Snapshot&);
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
    void adjust(const QString& kind,bool live,bool existing=false);
    void removeBackground();
    void runFilter(int);
    void setupSelectionActions();
    void runWand(Point,Qt::KeyboardModifiers,std::optional<editing::SelectionMode> modeOverride={});
    int wandTolerance_{32},wandSampleRadius_{};
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
    void layerCommand(int);
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
    EditorProject& addProject(Document d,QString title="Untitled");
    void addFeasibilityDocument();
    void openPath(const QString&);
    NativeCanvas* canvas(){auto*p=current();return p?p->canvas:nullptr;}
    void exerciseNativeUi(const QString& evidenceDirectory);
};
}
