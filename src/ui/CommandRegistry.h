#pragma once
#include <QKeySequence>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <optional>
#include <vector>
class QAction;
class QWidget;
class QKeyEvent;

namespace compositor::ui {
// State values describe the current session before a command resolves any edit.
// No document/raster/history ownership belongs to the registry.
struct CommandState {
    bool document{},projectBusy{},showsBusy{},importing{},brushStroke{},warpStroke{};
    bool levels{},hueSaturation{},filterEdit{},showsNewDocument{},showsImporter{};
    bool renamingLayer{},importError{},adjustmentEditing{},transformEdit{},persistentTransform{};
    bool crop{},gradient{},pixelMove{},colorPicker{},lassoDraft{},managing{},modalDialog{};
    bool activeLayer{},group{},asset{},adjustmentLayer{},mask{},maskEnabled{},maskSelected{},effectiveVisible{},singleSelected{};
    bool selection{},selectionEmpty{},renderHasPixels{},historyUndo{},historyRedo{},clipboardImage{};
    bool transformable{},hasParent{},canMoveUp{},canMoveDown{},canMerge{},canToggleClipping{},hasOtherProject{},shape{};
    bool moveTool{};
};
bool canStartProjectOperation(const CommandState&);
bool canSwitchProject(const CommandState&);
bool canUseHistory(const CommandState&);
bool canEditLayers(const CommandState&);
bool canPaint(const CommandState&);
bool canCopyPixels(const CommandState&);
bool canAdjustColors(const CommandState&);
bool canInvert(const CommandState&);

enum class CommandClass { Application,Workspace,Document,Layer,Selection,Pixels,Adjustment,Tool,View,Pending };
enum class CommandGate {
    Always,ProjectStart,WorkspaceSwitch,DocumentOperation,Import,Document,Undo,Redo,Layers,ActiveLayer,
    SingleLayer,Duplicate,Parent,MoveUp,MoveDown,Merge,Clipping,OtherProject,Mask,AddMask,ImageAlpha,
    Selection,ModifySelection,Copy,Cut,CopyMerged,Paste,Paint,Clear,Invert,Adjust,NewAdjustment,
    EditAdjustment,ContentAwareFill,Transform,TransformDraft,TransformControls,ShapeStyle,Tool,Palette,
    ApplyTransform,ApplyGradient,ApplyCrop
};
enum class CommandPreparation { None,FinishAppearance,CommitTransformAndGradient,ProjectOperation };
enum class TextCommand { None,Undo,Redo,Cut,Copy,Paste,SelectAll };
struct CommandSpec {
    QString id,menu,label,windowsShortcut;
    CommandClass category{CommandClass::Application};
    CommandGate gate{CommandGate::Always};
    CommandPreparation preparation{CommandPreparation::None};
    TextCommand textCommand{TextCommand::None};
    QString source;
};
bool commandEnabled(CommandGate,const CommandState&);
const std::vector<CommandSpec>& commandCatalog();
const CommandSpec* commandSpec(const QString& menu,const QString& label);
const CommandSpec* commandSpecById(const QString& id);
bool isTextEditingWidget(QWidget*);
bool reservesTextShortcut(const QKeyEvent&);

class CommandRegistry final:public QObject {
public:
    using StateProvider=std::function<CommandState()>;
    using Prepare=std::function<void(CommandPreparation,const QString&)>;
    using Enabled=std::function<bool(const CommandState&)>;
    CommandRegistry(QWidget* owner,StateProvider,Prepare={});
    ~CommandRegistry() override;
    // Register before connecting another triggered handler. The registry owns
    // dispatch, rechecks eligibility at invocation, then calls preparation+invoke.
    void bind(QAction*,const CommandSpec&,std::function<void()> invoke,Enabled extra={});
    void refresh();
    void setErrorHandler(std::function<void(const QString&)> handler){error_=std::move(handler);}
    bool invoke(const QString& id);
    QJsonArray manifest()const;
protected:
    bool eventFilter(QObject*,QEvent*)override;
private:
    struct Entry {QPointer<QAction> action;CommandSpec spec;std::function<void()> invoke;Enabled extra;};
    QPointer<QWidget> owner_;
    StateProvider state_;
    Prepare prepare_;
    std::function<void(const QString&)> error_;
    std::vector<Entry> entries_;
    bool refreshing_{};
    bool invokeEntry(size_t);
};
}
