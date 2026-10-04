// Command contracts from a19db9011282399785dc18efcfded904627bdcc2.
// Source predicates deliberately remain separate from Qt focus/modal policy.
#include "CommandRegistry.h"
#include <QAction>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QClipboard>
#include <QComboBox>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>
#include <algorithm>
#include <stdexcept>

namespace compositor::ui {
bool canStartProjectOperation(const CommandState&s){return !s.projectBusy&&!s.importing&&!s.brushStroke&&!s.warpStroke&&!s.levels&&!s.showsNewDocument&&!s.showsImporter&&!s.renamingLayer&&!s.importError&&!s.adjustmentEditing;}
bool canSwitchProject(const CommandState&s){return !s.managing&&canStartProjectOperation(s)&&!s.hueSaturation&&!s.filterEdit&&!s.gradient&&!s.pixelMove&&!s.colorPicker;}
bool canUseHistory(const CommandState&s){return !s.projectBusy&&!s.importing&&!s.brushStroke&&!s.warpStroke&&!s.levels&&!s.showsNewDocument&&!s.showsImporter&&!s.renamingLayer&&!s.importError&&!s.transformEdit;}
bool canEditLayers(const CommandState&s){return s.document&&!s.brushStroke&&!s.warpStroke&&!s.projectBusy&&!s.importing&&!s.showsNewDocument&&!s.showsImporter&&!s.renamingLayer&&!s.transformEdit&&!s.crop&&!s.gradient&&!s.pixelMove&&!s.hueSaturation&&!s.levels&&!s.filterEdit&&!s.adjustmentEditing;}
bool canPaint(const CommandState&s){return canEditLayers(s)&&s.singleSelected&&s.activeLayer&&(!s.group||s.maskSelected)&&(!s.selection||!s.selectionEmpty)&&s.effectiveVisible&&(!s.maskSelected||(s.mask&&s.maskEnabled))&&(s.maskSelected||!s.adjustmentLayer);}
bool canCopyPixels(const CommandState&s){return canEditLayers(s)&&s.activeLayer&&(!s.group||s.maskSelected)&&(!s.selection||!s.selectionEmpty)&&(s.maskSelected?s.mask:s.asset);}
bool canAdjustColors(const CommandState&s){return !s.levels&&!s.filterEdit&&s.document&&s.activeLayer&&!s.projectBusy&&!s.importing&&!s.brushStroke&&!s.pixelMove&&!s.renamingLayer&&!s.showsNewDocument&&!s.showsImporter&&s.singleSelected&&!s.group&&!s.maskSelected&&s.asset&&s.effectiveVisible&&(!s.selection||!s.selectionEmpty);}
bool canInvert(const CommandState&s){return s.document&&s.activeLayer&&!s.projectBusy&&!s.importing&&!s.brushStroke&&!s.pixelMove&&!s.renamingLayer&&!s.showsNewDocument&&!s.showsImporter&&s.singleSelected&&(!s.group||s.maskSelected)&&s.effectiveVisible&&(!s.selection||!s.selectionEmpty)&&(s.maskSelected?s.mask&&s.maskEnabled:s.asset);}

bool commandEnabled(CommandGate gate,const CommandState&s){
    // Modal widgets own the key window. Busy/import state also blocks native
    // settings/tool dispatch even where a Swift menu relies on modal routing.
    if(s.modalDialog&&gate!=CommandGate::Always)return false;
    const bool editable=canEditLayers(s),selected=s.selection&&!s.selectionEmpty;
    switch(gate){
    case CommandGate::Always:return true;
    case CommandGate::ProjectStart:return canStartProjectOperation(s)&&!s.managing;
    case CommandGate::WorkspaceSwitch:return canSwitchProject(s);
    case CommandGate::DocumentOperation:return s.document&&canStartProjectOperation(s)&&!s.managing;
    case CommandGate::Import:return !s.projectBusy&&!s.levels&&!s.showsBusy&&!s.importing&&!s.showsNewDocument;
    case CommandGate::Document:return s.document;
    case CommandGate::Undo:return canUseHistory(s)&&(s.historyUndo||s.gradient);
    case CommandGate::Redo:return canUseHistory(s)&&s.historyRedo;
    case CommandGate::Layers:return editable;
    case CommandGate::ActiveLayer:return editable&&s.activeLayer;
    case CommandGate::SingleLayer:return editable&&s.singleSelected&&s.activeLayer;
    case CommandGate::Duplicate:return canCopyPixels(s)||(!s.selection&&editable&&s.activeLayer&&!s.group);
    case CommandGate::Parent:return editable&&s.activeLayer&&s.hasParent;
    case CommandGate::MoveUp:return editable&&s.canMoveUp;
    case CommandGate::MoveDown:return editable&&s.canMoveDown;
    case CommandGate::Merge:return editable&&s.canMerge;
    case CommandGate::Clipping:return editable&&s.canToggleClipping;
    case CommandGate::OtherProject:return editable&&s.activeLayer;
    case CommandGate::Mask:return editable&&s.singleSelected&&s.activeLayer&&s.mask;
    case CommandGate::AddMask:return editable&&s.singleSelected&&s.activeLayer&&!s.mask;
    case CommandGate::ImageAlpha:return editable&&s.asset;
    case CommandGate::Selection:return editable&&s.selection;
    case CommandGate::ModifySelection:return editable&&selected&&!s.lassoDraft;
    case CommandGate::Copy:return canCopyPixels(s);
    case CommandGate::Cut:return s.selection&&canCopyPixels(s);
    case CommandGate::CopyMerged:return editable&&(!s.selection||!s.selectionEmpty)&&s.renderHasPixels;
    case CommandGate::Paste:return editable&&s.clipboardImage;
    case CommandGate::Paint:return canPaint(s);
    case CommandGate::Clear:return s.selection&&canPaint(s);
    case CommandGate::Invert:return !s.warpStroke&&!s.levels&&!s.hueSaturation&&!s.filterEdit&&!s.adjustmentEditing&&canInvert(s);
    case CommandGate::Adjust:return !s.warpStroke&&!s.hueSaturation&&!s.adjustmentEditing&&canAdjustColors(s);
    case CommandGate::NewAdjustment:return editable;
    case CommandGate::EditAdjustment:return editable&&s.activeLayer&&s.adjustmentLayer;
    case CommandGate::ContentAwareFill:return !s.warpStroke&&!s.hueSaturation&&canAdjustColors(s)&&selected;
    case CommandGate::Transform:return editable&&s.transformable;
    case CommandGate::TransformDraft:return (editable&&s.transformable)||(s.transformEdit&&!s.projectBusy&&!s.importing&&!s.brushStroke&&!s.warpStroke);
    case CommandGate::TransformControls:return s.document&&s.moveTool;
    case CommandGate::ShapeStyle:return editable&&s.activeLayer&&s.shape;
    case CommandGate::Tool:return !s.projectBusy&&!s.importing&&!s.brushStroke&&!s.warpStroke&&!s.levels;
    case CommandGate::Palette:return !s.projectBusy&&!s.importing&&!s.brushStroke;
    case CommandGate::ApplyTransform:return s.transformEdit&&!s.projectBusy&&!s.importing;
    case CommandGate::ApplyGradient:return s.gradient&&!s.projectBusy&&!s.importing;
    case CommandGate::ApplyCrop:return s.crop&&canStartProjectOperation(s);
    }
    return false;
}

const std::vector<CommandSpec>& commandCatalog(){
    static const auto catalog=[](){
        using C=CommandClass;using G=CommandGate;using P=CommandPreparation;using T=TextCommand;
        std::vector<CommandSpec> list;
        auto add=[&](const char*id,const char*menu,const char*label,const char*key,C category,G gate,P preparation,const char*source,T text=T::None){list.push_back({QString::fromUtf8(id),QString::fromUtf8(menu),QString::fromUtf8(label),QString::fromUtf8(key),category,gate,preparation,text,QString::fromUtf8(source)});};
        add("file.new","File","New Canvas…","Ctrl+N",C::Workspace,G::WorkspaceSwitch,P::ProjectOperation,"Compositor/Document/ProjectWorkspace.swift:53");
        add("file.open","File","Open Project…","Ctrl+O",C::Workspace,G::WorkspaceSwitch,P::ProjectOperation,"Compositor/Document/ProjectWorkspace.swift:59");
        add("file.import","File","Import Image…","",C::Document,G::Import,P::None,"Compositor/CompositorApp.swift:58");
        add("file.export","File","Export Image…","Ctrl+Shift+E",C::Document,G::DocumentOperation,P::ProjectOperation,"Compositor/CompositorApp.swift:68");
        add("file.save","File","Save Project","Ctrl+S",C::Document,G::DocumentOperation,P::ProjectOperation,"Compositor/CompositorApp.swift:62");
        add("file.save_as","File","Save Project As…","Ctrl+Shift+S",C::Document,G::DocumentOperation,P::ProjectOperation,"Compositor/CompositorApp.swift:64");
        add("file.close","File","Close Project","Ctrl+W",C::Workspace,G::WorkspaceSwitch,P::ProjectOperation,"Compositor/Document/ProjectWorkspace.swift:107");
        add("app.exit","File","Exit","Alt+F4",C::Application,G::WorkspaceSwitch,P::None,"Compositor/IO/CompositorApplicationDelegate.swift:39");
        add("app.about","Help","About Compositor","",C::Application,G::Always,P::None,"Native Windows application information");
        add("help.check_updates","Help","Check for Updates…","",C::Application,G::Always,P::None,"Windows signed development updater");
        add("edit.undo","Edit","Undo","Ctrl+Z",C::Document,G::Undo,P::None,"Compositor/Document/EditorSession.swift:444",T::Undo);
        add("edit.redo","Edit","Redo","Ctrl+Shift+Z",C::Document,G::Redo,P::None,"Compositor/Document/EditorSession.swift:444",T::Redo);
        add("clipboard.cut","Clipboard","Cut","Ctrl+X",C::Pixels,G::Cut,P::FinishAppearance,"Compositor/CompositorApp.swift:113",T::Cut);
        add("clipboard.copy","Clipboard","Copy","Ctrl+C",C::Pixels,G::Copy,P::None,"Compositor/CompositorApp.swift:120",T::Copy);
        add("clipboard.paste","Clipboard","Paste","Ctrl+V",C::Pixels,G::Paste,P::FinishAppearance,"Compositor/CompositorApp.swift:127",T::Paste);
        add("clipboard.copy_merged","Clipboard","Copy Merged","Ctrl+Shift+C",C::Pixels,G::CopyMerged,P::None,"Compositor/Document/SelectionClipboard.swift:55");
        add("layer.via_copy","Clipboard","Layer via Copy","Ctrl+J",C::Layer,G::Duplicate,P::FinishAppearance,"Compositor/CompositorApp.swift:228");
        add("pixels.clear","Clipboard","Clear","",C::Pixels,G::Clear,P::FinishAppearance,"Compositor/CompositorApp.swift:149");
        add("selection.all","Select","All","Ctrl+A",C::Selection,G::Layers,P::None,"Compositor/Document/Selection.swift:230",T::SelectAll);
        add("selection.deselect","Select","Deselect","Ctrl+D",C::Selection,G::Selection,P::FinishAppearance,"Compositor/CompositorApp.swift:162");
        add("selection.inverse","Select","Inverse","Ctrl+Shift+I",C::Selection,G::Selection,P::FinishAppearance,"Compositor/CompositorApp.swift:164");
        add("selection.expand","Select","Expand…","",C::Selection,G::ModifySelection,P::FinishAppearance,"Compositor/Document/Selection.swift:274");
        add("selection.contract","Select","Contract…","",C::Selection,G::ModifySelection,P::FinishAppearance,"Compositor/Document/Selection.swift:274");
        add("pixels.fill","Image","Fill…","",C::Pixels,G::Paint,P::FinishAppearance,"Compositor/Document/SelectionEdits.swift:35");
        add("pixels.invert","Image","Invert","Ctrl+I",C::Pixels,G::Invert,P::CommitTransformAndGradient,"Compositor/Document/SelectionEdits.swift:77");
        add("image.trim","Image","Trim","",C::Document,G::DocumentOperation,P::FinishAppearance,"Compositor/Document/Canvas.swift");
        add("selection.feather","Select","Feather…","",C::Selection,G::ModifySelection,P::FinishAppearance,"Compositor/Document/Selection.swift");
        add("selection.color_range","Select","Color Range…","",C::Selection,G::ModifySelection,P::FinishAppearance,"Compositor/Document/Selection.swift");
        add("view.guides","View","Guides","",C::View,G::Always,P::None,"Compositor/Document/Guides.swift");
        add("view.mac_title_bar","Appearance","Mac-style title bar","",C::View,G::Always,P::None,"Windows title bar preference");
        add("view.snap","View","Snap","",C::View,G::Always,P::None,"Compositor/Document/Guides.swift");
        add("layer.text","Layer","Text Layer…","",C::Layer,G::Layers,P::FinishAppearance,"Compositor/Document/Text.swift");
        add("help.shortcuts","Help","Keyboard Shortcuts…","",C::Application,G::Always,P::None,"Windows shortcut window");
        add("view.fit","View","Fit Canvas","Ctrl+0",C::View,G::Document,P::None,"Compositor/CompositorApp.swift:87");
        add("view.actual","View","Actual Pixels","Ctrl+1",C::View,G::Document,P::None,"Compositor/CompositorApp.swift:88");
        add("view.pixel_grid","View","Pixel Grid","",C::View,G::Always,P::None,"Compositor/CompositorApp.swift:93");
        add("canvas.size","Canvas","Canvas Size…","Ctrl+Alt+C",C::Document,G::DocumentOperation,P::ProjectOperation,"Compositor/CompositorApp.swift:196");
        add("canvas.image_size","Canvas","Image Size…","Ctrl+Alt+I",C::Document,G::DocumentOperation,P::ProjectOperation,"Compositor/CompositorApp.swift:199");
        add("canvas.flip_horizontal","Canvas","Flip Canvas Horizontally","",C::Document,G::Layers,P::FinishAppearance,"Compositor/CompositorApp.swift:204");
        add("canvas.flip_vertical","Canvas","Flip Canvas Vertically","",C::Document,G::Layers,P::FinishAppearance,"Compositor/CompositorApp.swift:206");
        add("canvas.crop_tool","Canvas","Crop Tool","",C::Tool,G::Tool,P::None,"Compositor/Document/EditorSession.swift:282");
        add("crop.apply","Canvas","Apply Crop","",C::Pending,G::ApplyCrop,P::None,"Compositor/Document/Crop.swift:187");
        add("crop.cancel","Canvas","Cancel Crop","",C::Pending,G::ApplyCrop,P::None,"Compositor/Document/Crop.swift:178");
        add("layer.new","Layer","New Layer","Ctrl+Shift+N",C::Layer,G::Layers,P::FinishAppearance,"Compositor/CompositorApp.swift:241");
        add("layer.duplicate","Layer","Duplicate Layer","Ctrl+Shift+J",C::Layer,G::Duplicate,P::FinishAppearance,"Compositor/CompositorApp.swift:228");
        add("layer.rename","Layer","Rename Layer…","",C::Layer,G::ActiveLayer,P::FinishAppearance,"Compositor/CompositorApp.swift:243");
        add("layer.delete","Layer","Delete Layer","",C::Layer,G::ActiveLayer,P::FinishAppearance,"Compositor/CompositorApp.swift:263");
        add("layer.raise","Layer","Raise Layer","Ctrl+]",C::Layer,G::MoveUp,P::FinishAppearance,"Compositor/CompositorApp.swift:249");
        add("layer.lower","Layer","Lower Layer","Ctrl+[",C::Layer,G::MoveDown,P::FinishAppearance,"Compositor/CompositorApp.swift:251");
        add("layer.new_group","Layer","New Group","",C::Layer,G::Layers,P::FinishAppearance,"Compositor/Document/LayerGroups.swift");
        add("layer.group","Layer","Group Selected","Ctrl+G",C::Layer,G::Layers,P::FinishAppearance,"Compositor/CompositorApp.swift:237");
        add("layer.ungroup","Layer","Ungroup","Shift+Ctrl+G",C::Layer,G::Layers,P::FinishAppearance,"Compositor/Document/LayerGroups.swift");
        add("layer.move_out","Layer","Move Out of Group","",C::Layer,G::Parent,P::FinishAppearance,"Compositor/CompositorApp.swift:239");
        add("layer.clipping","Layer","Create / Release Clipping Mask","Ctrl+Alt+G",C::Layer,G::Clipping,P::FinishAppearance,"Compositor/CompositorApp.swift:232");
        add("layer.merge","Layer","Merge Layers","Ctrl+E",C::Layer,G::Merge,P::FinishAppearance,"Compositor/CompositorApp.swift:254");
        add("layer.copy_project","Layer","Copy Layer to Project…","",C::Workspace,G::OtherProject,P::FinishAppearance,"Compositor/Document/ProjectWorkspace.swift:158");
        const char*maskLabels[]{"Add White Mask (Reveal Selection)","Add Black Mask (Hide Selection)","Reveal All","Hide All","Enable / Disable Mask","Link / Unlink Mask","Delete Mask","Edit Image","Edit Mask","Select Image Alpha","Select Mask Black Areas"};
        const char*maskIds[]{"mask.add_white","mask.add_black","mask.reveal_all","mask.hide_all","mask.enabled","mask.linked","mask.delete","mask.edit_image","mask.edit_mask","mask.load_alpha","mask.load_black"};
        for(int i=0;i<11;++i)add(maskIds[i],"Mask",maskLabels[i],"",C::Layer,i<4?G::AddMask:i==7?G::SingleLayer:i==9?G::ImageAlpha:G::Mask,P::None,"Compositor/Document/LayerMask.swift:221");
        add("transform.free","Transform","Free Transform","Ctrl+T",C::Layer,G::Transform,P::FinishAppearance,"Compositor/CompositorApp.swift:226");
        add("transform.distort","Transform","Distort","",C::Layer,G::TransformDraft,P::FinishAppearance,"Compositor/Document/Distort.swift:188");
        add("transform.draft_flip_horizontal","Transform Options","Flip H","",C::Layer,G::TransformDraft,P::FinishAppearance,"Compositor/Document/EditorSession.swift:310");
        add("transform.draft_flip_vertical","Transform Options","Flip V","",C::Layer,G::TransformDraft,P::FinishAppearance,"Compositor/Document/EditorSession.swift:310");
        add("transform.apply","Transform","Apply Transform","",C::Pending,G::ApplyTransform,P::None,"Compositor/Rendering/EditorCanvas.swift:1496");
        add("transform.cancel","Transform","Cancel Transform","",C::Pending,G::ApplyTransform,P::None,"Compositor/Rendering/EditorCanvas.swift:1493");
        add("transform.flip_horizontal","Transform","Flip Layer Horizontally","",C::Layer,G::Transform,P::FinishAppearance,"Compositor/CompositorApp.swift:257");
        add("transform.flip_vertical","Transform","Flip Layer Vertically","",C::Layer,G::Transform,P::FinishAppearance,"Compositor/CompositorApp.swift:259");
        add("transform.scale","Transform","Scale…","",C::Layer,G::TransformDraft,P::FinishAppearance,"Compositor/Document/EditorSession.swift:246");
        const char*adjustments[]{"Hue/Saturation…","Levels…","Curves…","Exposure…","Gradient Map…","Grain…","Invert…","Black and White…","Color Balance…","Gaussian Blur…","Motion Blur…","Add Noise…"};
        const char*slugs[]{"hue_saturation","levels","curves","exposure","gradient_map","grain","invert","black_white","color_balance","gaussian","motion","noise"};
        for(int i=0;i<12;++i){auto id=QString("adjust.%1").arg(slugs[i]).toUtf8();add(id.constData(),"Adjustments",adjustments[i],i==0?"Ctrl+U":i==1?"Ctrl+L":i==2?"Ctrl+M":"",C::Adjustment,G::Adjust,P::CommitTransformAndGradient,"Compositor/Document/HueSaturation.swift:414");id=QString("adjust.new.%1").arg(slugs[i]).toUtf8();add(id.constData(),"New Adjustment Layer",adjustments[i],"",C::Adjustment,G::NewAdjustment,P::FinishAppearance,"Compositor/CompositorApp.swift:217");}
        add("adjust.edit","Adjustments","Edit Adjustment Layer…","",C::Adjustment,G::EditAdjustment,P::FinishAppearance,"Compositor/CompositorApp.swift:222");
        const char*filters[]{"Gaussian Blur…","Motion Blur…","Add Noise…","Lens Correction…","Content-Aware Fill…","Vignette…","Bloom…","Tonal Contrast…","Dither…","Scanlines…","Camera Raw Filter…","Remove Background…"};
        const char*filterIds[]{"filter.gaussian","filter.motion","filter.noise","filter.lens","filter.content_aware","filter.vignette","filter.bloom","filter.tonal","filter.dither","filter.scanlines","filter.camera_raw","filter.subject"};
        for(int i=0;i<12;++i)add(filterIds[i],"Filters",filters[i],i==4?"Shift+Delete":"",C::Adjustment,i==4?G::ContentAwareFill:G::Adjust,P::CommitTransformAndGradient,"Compositor/Document/Filters.swift:299");
        const char*toolLabels[]{"Move (V)","Hand (H)","Marquee (M)","Lasso (L)","Polygon","Wand (W)","Brush (B)","Eraser (E)","Clone (S)","Heal (J)","Retouch (R)","Gradient (G)","Shape (U)","Crop (C)","Eyedropper (I)","Zoom (Z)","Type (T)"};
        const char*toolIds[]{"move","hand","marquee","lasso","polygon","wand","brush","eraser","clone","heal","retouch","gradient","shape","crop","eyedropper","zoom","type"};
        const char*toolKeys[]{"V","H","M","L","","W","B","E","S","J","R","G","U","C","I","Z","T"};
        for(int i=0;i<17;++i){auto id=QString("tool.%1").arg(toolIds[i]).toUtf8();add(id.constData(),"Tools",toolLabels[i],toolKeys[i],C::Tool,G::Tool,P::None,"Compositor/Rendering/EditorCanvas.swift:1520");}
        add("palette.foreground","Tools","Foreground","",C::Tool,G::Palette,P::None,"Compositor/Document/ColorPalette.swift:25");
        add("palette.background","Tools","Background","",C::Tool,G::Palette,P::None,"Compositor/Document/ColorPalette.swift:25");
        add("palette.swap","Tools","Swap (X)","X",C::Tool,G::Palette,P::None,"Compositor/Document/ColorPalette.swift:25");
        add("palette.defaults","Tools","Default (D)","D",C::Tool,G::Palette,P::None,"Compositor/Document/ColorPalette.swift:25");
        add("gradient.apply","Drawing Options","Apply Gradient","",C::Pending,G::ApplyGradient,P::None,"Compositor/Rendering/EditorCanvas.swift:1485");
        add("gradient.cancel","Drawing Options","Cancel Gradient","",C::Pending,G::ApplyGradient,P::None,"Compositor/Rendering/EditorCanvas.swift:1482");
        add("shape.style","Drawing Options","Update Shape Style","",C::Layer,G::ShapeStyle,P::FinishAppearance,"Compositor/Document/ShapeTool.swift");
        return list;
    }();
    return catalog;
}
namespace {
QString normalized(QString value){return value.remove('&').trimmed();}
bool clipboardHasText(){const auto*mime=QApplication::clipboard()->mimeData();return mime&&mime->hasText();}
QWidget* textEditor(QWidget* widget){
    for(auto* item=widget;item;item=item->parentWidget()){
        if(qobject_cast<QLineEdit*>(item)||qobject_cast<QTextEdit*>(item)||qobject_cast<QPlainTextEdit*>(item))return item;
        if(auto* spin=qobject_cast<QAbstractSpinBox*>(item))return spin->findChild<QLineEdit*>();
        if(auto* combo=qobject_cast<QComboBox*>(item);combo&&combo->isEditable())return combo->lineEdit();
    }return nullptr;
}
bool textEnabled(QWidget* widget,TextCommand command){
    if(auto*line=qobject_cast<QLineEdit*>(widget)){switch(command){case TextCommand::Undo:return !line->isReadOnly()&&line->isUndoAvailable();case TextCommand::Redo:return !line->isReadOnly()&&line->isRedoAvailable();case TextCommand::Cut:return !line->isReadOnly()&&line->hasSelectedText();case TextCommand::Copy:return line->hasSelectedText();case TextCommand::Paste:return !line->isReadOnly()&&clipboardHasText();case TextCommand::SelectAll:return !line->text().isEmpty();default:return false;}}
    auto rich=qobject_cast<QTextEdit*>(widget);auto plain=qobject_cast<QPlainTextEdit*>(widget);if(!rich&&!plain)return false;
    const auto*doc=rich?rich->document():plain->document();const bool readOnly=rich?rich->isReadOnly():plain->isReadOnly();const bool selected=rich?rich->textCursor().hasSelection():plain->textCursor().hasSelection();
    switch(command){case TextCommand::Undo:return !readOnly&&doc->isUndoAvailable();case TextCommand::Redo:return !readOnly&&doc->isRedoAvailable();case TextCommand::Cut:return !readOnly&&selected;case TextCommand::Copy:return selected;case TextCommand::Paste:return !readOnly&&clipboardHasText();case TextCommand::SelectAll:return !doc->isEmpty();default:return false;}
}
bool dispatchText(QWidget* widget,TextCommand command){
    if(!widget||command==TextCommand::None)return false;
    if(!textEnabled(widget,command))return true;
    auto invoke=[&](auto* text){switch(command){case TextCommand::Undo:text->undo();break;case TextCommand::Redo:text->redo();break;case TextCommand::Cut:text->cut();break;case TextCommand::Copy:text->copy();break;case TextCommand::Paste:text->paste();break;case TextCommand::SelectAll:text->selectAll();break;default:break;}};
    if(auto*line=qobject_cast<QLineEdit*>(widget))invoke(line);else if(auto*rich=qobject_cast<QTextEdit*>(widget))invoke(rich);else if(auto*plain=qobject_cast<QPlainTextEdit*>(widget))invoke(plain);return true;
}
}
const CommandSpec* commandSpec(const QString& menu,const QString& label){const auto m=normalized(menu),l=normalized(label);const auto&items=commandCatalog();auto it=std::find_if(items.begin(),items.end(),[&](const CommandSpec&s){return s.menu==m&&s.label==l;});return it==items.end()?nullptr:&*it;}
const CommandSpec* commandSpecById(const QString& id){const auto&items=commandCatalog();auto it=std::find_if(items.begin(),items.end(),[&](const CommandSpec&s){return s.id==id;});return it==items.end()?nullptr:&*it;}
bool isTextEditingWidget(QWidget*widget){return textEditor(widget)!=nullptr;}
bool reservesTextShortcut(const QKeyEvent&e){
    const auto key=e.key();const auto mods=e.modifiers();
    if(key==Qt::Key_Delete||key==Qt::Key_Backspace||key==Qt::Key_Left||key==Qt::Key_Right||key==Qt::Key_Up||key==Qt::Key_Down||key==Qt::Key_Home||key==Qt::Key_End||key==Qt::Key_Return||key==Qt::Key_Enter)return true;
    if((mods&~Qt::ShiftModifier)==Qt::NoModifier)return key>=Qt::Key_Space&&key<=Qt::Key_AsciiTilde;
    if((mods&Qt::ControlModifier)&&(mods&~(Qt::ControlModifier|Qt::ShiftModifier))==Qt::NoModifier)return key==Qt::Key_A||key==Qt::Key_C||key==Qt::Key_X||key==Qt::Key_V||key==Qt::Key_Z||key==Qt::Key_Y||key==Qt::Key_Insert||key==Qt::Key_Left||key==Qt::Key_Right;
    return false;
}
CommandRegistry::CommandRegistry(QWidget*owner,StateProvider state,Prepare prepare):QObject(owner),owner_(owner),state_(std::move(state)),prepare_(std::move(prepare)){
    if(!owner||!state_)throw std::invalid_argument("Command registry requires window and state provider");
    qApp->installEventFilter(this);connect(qApp,&QApplication::focusChanged,this,[this](QWidget*,QWidget*){refresh();});connect(QApplication::clipboard(),&QClipboard::dataChanged,this,[this]{refresh();});
}
CommandRegistry::~CommandRegistry(){if(qApp)qApp->removeEventFilter(this);}
void CommandRegistry::bind(QAction*action,const CommandSpec&spec,std::function<void()>invoke,Enabled extra){
    if(!action||spec.id.isEmpty()||!invoke)throw std::invalid_argument("Invalid command binding");
    if(std::any_of(entries_.begin(),entries_.end(),[&](const Entry&e){return e.action==action;}))throw std::invalid_argument("Action already registered");
    action->setProperty("commandId",spec.id);action->setProperty("commandSource",spec.source);if(action->objectName().isEmpty())action->setObjectName("command."+spec.id);
    if(!spec.windowsShortcut.isEmpty())action->setShortcut(QKeySequence(spec.windowsShortcut));
    if(spec.id=="edit.redo")action->setShortcuts({QKeySequence("Ctrl+Shift+Z"),QKeySequence("Ctrl+Y")});
    action->setShortcutContext(Qt::WindowShortcut);
    const auto index=entries_.size();entries_.push_back({action,spec,std::move(invoke),std::move(extra)});connect(action,&QAction::triggered,this,[this,index]{invokeEntry(index);});
    if(auto*menu=qobject_cast<QMenu*>(action->parent()))connect(menu,&QMenu::aboutToShow,this,[this]{refresh();});refresh();
}
void CommandRegistry::refresh(){
    if(refreshing_||!owner_)return;refreshing_=true;
    try{const auto state=state_();auto*text=textEditor(QApplication::focusWidget());for(const auto&e:entries_)if(e.action){bool enabled=text&&e.spec.textCommand!=TextCommand::None?textEnabled(text,e.spec.textCommand):commandEnabled(e.spec.gate,state)&&(!e.extra||e.extra(state));e.action->setEnabled(enabled);}}
    catch(...){refreshing_=false;throw;}refreshing_=false;
}
bool CommandRegistry::invokeEntry(size_t index){
    if(index>=entries_.size()||!entries_[index].action)return false;auto entry=entries_[index];
    try{
    auto*text=textEditor(QApplication::focusWidget());if(dispatchText(text,entry.spec.textCommand)){refresh();return true;}
    const auto state=state_();if(!commandEnabled(entry.spec.gate,state)||(entry.extra&&!entry.extra(state))){refresh();return false;}
    if(prepare_)prepare_(entry.spec.preparation,entry.spec.id);entry.invoke();refresh();return true;
    }catch(const std::exception&error){if(error_){error_(QString::fromUtf8(error.what()));return false;}throw;}
}
bool CommandRegistry::invoke(const QString&id){for(size_t i=0;i<entries_.size();++i)if(entries_[i].spec.id==id&&entries_[i].action)return invokeEntry(i);return false;}
QJsonArray CommandRegistry::manifest()const{QJsonArray result;for(const auto&e:entries_)if(e.action){QJsonArray shortcuts;for(const auto&key:e.action->shortcuts())shortcuts.append(key.toString(QKeySequence::PortableText));result.append(QJsonObject{{"id",e.spec.id},{"object_name",e.action->objectName()},{"menu",e.spec.menu},{"label",e.action->text()},{"shortcuts",shortcuts},{"class",int(e.spec.category)},{"gate",int(e.spec.gate)},{"preparation",int(e.spec.preparation)},{"text_command",int(e.spec.textCommand)},{"source",e.spec.source},{"enabled",e.action->isEnabled()}});}return result;}
bool CommandRegistry::eventFilter(QObject*object,QEvent*event){
    if(owner_&&event->type()==QEvent::ShortcutOverride){auto*widget=qobject_cast<QWidget*>(object);auto*focus=QApplication::focusWidget();if(widget&&focus&&(focus==owner_||owner_->isAncestorOf(focus))&&isTextEditingWidget(focus)&&reservesTextShortcut(*static_cast<QKeyEvent*>(event))){event->accept();return true;}}
    return QObject::eventFilter(object,event);
}
}
