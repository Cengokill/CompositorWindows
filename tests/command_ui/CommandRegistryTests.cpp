#include "ui/CommandRegistry.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace compositor::ui;
namespace {
void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}
CommandState ready(){CommandState s;s.document=s.activeLayer=s.asset=s.effectiveVisible=s.singleSelected=s.historyUndo=s.historyRedo=s.renderHasPixels=s.transformable=true;return s;}
const CommandSpec& spec(const char*id){auto*s=commandSpecById(id);check(s!=nullptr,"missing catalog id");return *s;}
void source_predicates(){
    auto s=ready();check(canStartProjectOperation(s)&&canSwitchProject(s)&&canUseHistory(s)&&canEditLayers(s),"ready session");
    // LevelsTests.swift:38 exact three predicate assertions after beginning Levels.
    s.levels=true;check(!canEditLayers(s)&&!canUseHistory(s)&&!canStartProjectOperation(s),"Levels excludes layers/history/project operations");s.levels=false;
    const std::vector<bool CommandState::*> blocks{&CommandState::projectBusy,&CommandState::importing,&CommandState::brushStroke,&CommandState::warpStroke,&CommandState::showsNewDocument,&CommandState::showsImporter,&CommandState::renamingLayer};
    for(auto flag:blocks){auto b=s;b.*flag=true;check(!canEditLayers(b)&&!canUseHistory(b)&&!canStartProjectOperation(b),"busy/gesture/modal eligibility");}
    s.gradient=true;check(!canEditLayers(s)&&canUseHistory(s)&&!canSwitchProject(s),"gradient separates history and switching");s.historyUndo=false;check(commandEnabled(CommandGate::Undo,s),"pending gradient enables Undo with empty history");
    s.gradient=false;s.transformEdit=true;check(!canUseHistory(s)&&!canEditLayers(s)&&canStartProjectOperation(s)&&canSwitchProject(s),"transform can finalize at project boundary");
    s=ready();s.hueSaturation=true;check(!canEditLayers(s)&&!canSwitchProject(s),"Hue waits for completion");
}
void layer_eligibility(){
    auto s=ready();check(canPaint(s)&&canCopyPixels(s)&&canAdjustColors(s)&&canInvert(s),"ready visible image");
    s.selection=true;s.selectionEmpty=true;check(!canPaint(s)&&!commandEnabled(CommandGate::Paint,s)&&!canCopyPixels(s)&&!canAdjustColors(s)&&!canInvert(s),"explicit empty selection blocks pixels");
    s=ready();s.asset=false;check(canPaint(s)&&!canCopyPixels(s)&&!canAdjustColors(s)&&!canInvert(s),"blank paints but has no source image");
    s=ready();s.group=true;s.asset=false;s.maskSelected=s.mask=s.maskEnabled=true;check(canPaint(s)&&canCopyPixels(s)&&canInvert(s)&&!canAdjustColors(s),"folder mask target");
    s.maskEnabled=false;check(!canPaint(s)&&!canInvert(s)&&canCopyPixels(s),"disabled mask remains copyable");
    s=ready();s.effectiveVisible=false;check(!canPaint(s)&&!canAdjustColors(s)&&!canInvert(s)&&canCopyPixels(s),"copy does not require visible source");
    s=ready();s.singleSelected=false;check(!canPaint(s)&&!canAdjustColors(s)&&canCopyPixels(s),"copy active image differs from paint multi-select");
}
struct Fixture{
    QMainWindow window;CommandState state=ready();int calls{},preparations{};CommandPreparation last{};QString lastId;CommandRegistry registry;
    QLineEdit*line{};QWidget*body{};
    Fixture():registry(&window,[this]{return state;},[this](CommandPreparation preparation,const QString&id){++preparations;last=preparation;lastId=id;}){body=new QWidget;auto*layout=new QVBoxLayout(body);line=new QLineEdit;layout->addWidget(line);window.setCentralWidget(body);window.resize(400,160);window.show();window.activateWindow();body->setFocusPolicy(Qt::StrongFocus);body->setFocus();QApplication::processEvents();}
    QAction*bind(const char*id){const auto&s=spec(id);auto*menu=window.menuBar()->addMenu(s.menu);auto*a=menu->addAction(s.label);registry.bind(a,s,[this]{++calls;});return a;}
};
void pending_preparation(){Fixture f;auto*undo=f.bind("edit.undo");auto*adjust=f.bind("adjust.levels");auto*layer=f.bind("layer.new");auto*all=f.bind("selection.all");auto*create=f.bind("file.new");auto*distort=f.bind("transform.distort");f.state.gradient=true;f.state.historyUndo=false;f.registry.refresh();check(undo->isEnabled()&&adjust->isEnabled()&&!layer->isEnabled()&&!all->isEnabled()&&!create->isEnabled(),"pending gradient menu state");undo->trigger();check(f.calls==1&&f.last==CommandPreparation::None,"Undo must not apply gradient");adjust->trigger();check(f.calls==2&&f.last==CommandPreparation::CommitTransformAndGradient,"adjustment resolves pending image edit");f.state.gradient=false;f.state.transformEdit=true;f.registry.refresh();check(!undo->isEnabled()&&adjust->isEnabled()&&distort->isEnabled(),"transform blocks history but permits adjustment and draft distortion");f.state.importing=true;f.registry.refresh();check(!adjust->isEnabled()&&!distort->isEnabled(),"import disables adjustment and transform draft");}
void stale_activation(){Fixture f;auto*a=f.bind("pixels.fill");check(a->isEnabled(),"initial enabled");f.state.importing=true;a->trigger();check(f.calls==0&&f.preparations==0&&!a->isEnabled(),"invocation rechecks new importing state before preparation");f.state.importing=false;f.registry.refresh();f.state.selection=f.state.selectionEmpty=true;check(!f.registry.invoke("pixels.fill")&&f.calls==0,"dynamic explicit empty selection rejected");f.state=ready();f.state.transformEdit=true;auto*apply=f.bind("transform.apply");check(apply->isEnabled(),"pending apply available");f.state.modalDialog=true;check(!f.registry.invoke("transform.apply"),"modal owns dispatch");}
void text_shortcuts(){Fixture f;f.bind("tool.brush");f.bind("clipboard.copy_merged");f.bind("selection.all");f.bind("edit.undo");f.line->setFocus();QApplication::processEvents();QTest::keyClicks(f.line,"bml");check(f.line->text()=="bml"&&f.calls==0,"single-letter tools do not steal typing");QTest::keyClick(f.line,Qt::Key_A,Qt::ControlModifier);check(f.line->selectedText()=="bml"&&f.calls==0,"Ctrl+A stays text select all");QTest::keyClick(f.line,Qt::Key_C,Qt::ControlModifier|Qt::ShiftModifier);check(f.calls==0,"copy merged reserved in text");QTest::keyClick(f.line,Qt::Key_Z,Qt::ControlModifier);check(f.line->text().isEmpty()&&f.calls==0,"Ctrl+Z uses native text undo");auto*spin=new QSpinBox(f.body);check(isTextEditingWidget(spin)&&isTextEditingWidget(spin->findChild<QLineEdit*>()),"spin child text recognized");QComboBox combo;combo.setEditable(true);check(isTextEditingWidget(&combo),"editable combo recognized");QKeyEvent save(QEvent::ShortcutOverride,Qt::Key_S,Qt::ControlModifier);check(!reservesTextShortcut(save),"Save is not native text editing");}
void text_menu_routes(){Fixture f;f.bind("edit.undo");f.bind("edit.redo");f.bind("clipboard.cut");f.bind("clipboard.copy");f.bind("clipboard.paste");f.bind("selection.all");f.state.modalDialog=true;f.line->setFocus();QApplication::processEvents();QTest::keyClicks(f.line,"alpha");f.registry.refresh();check(f.registry.invoke("selection.all")&&f.line->selectedText()=="alpha","menu selects text during modal edit");f.registry.invoke("clipboard.cut");check(f.line->text().isEmpty()&&QApplication::clipboard()->text()=="alpha"&&f.calls==0,"Cut targets editor");f.registry.invoke("edit.undo");check(f.line->text()=="alpha"&&f.calls==0,"Undo targets editor");f.registry.invoke("edit.redo");check(f.line->text().isEmpty()&&f.calls==0,"Redo targets editor");f.registry.invoke("clipboard.paste");check(f.line->text()=="alpha"&&f.calls==0&&f.preparations==0,"text routes bypass document preparations");f.line->setReadOnly(true);f.registry.invoke("selection.all");f.registry.invoke("clipboard.cut");check(f.line->text()=="alpha","read-only text cannot be cut");}
void catalog_manifest(){std::set<QString>ids,sites;for(const auto&s:commandCatalog()){check(!s.id.isEmpty()&&!s.source.isEmpty(),"attributed catalog entry");check(ids.insert(s.id).second,"unique command ids");check(sites.insert(s.menu+"/"+s.label).second,"unique catalog menu/label");check(commandSpec(s.menu,s.label)==&s,"catalog lookup");}check(commandSpec("&File","&New Canvas…")->id=="file.new","ampersand normalization");check(commandSpec("&File","Unmapped…")==nullptr,"unknown commands remain explicit gaps");Fixture f;f.bind("edit.redo");auto*a=f.bind("file.save");check(a->objectName()=="command.file.save"&&a->property("commandId")=="file.save","stable action identity");const auto array=f.registry.manifest();check(array.size()==2&&array[0].toObject()["shortcuts"].toArray().size()==2,"manifest includes Windows redo alias");QJsonArray catalog;for(const auto&s:commandCatalog())catalog.append(QJsonObject{{"id",s.id},{"menu",s.menu},{"label",s.label},{"windows_shortcut",s.windowsShortcut},{"class",int(s.category)},{"gate",int(s.gate)},{"preparation",int(s.preparation)},{"source",s.source}});QFile output("command-catalog.json");check(output.open(QIODevice::WriteOnly),"catalog output writable");output.write(QJsonDocument(catalog).toJson());std::cout<<"catalog_entries="<<catalog.size()<<'\n';}
void registration(){Fixture f;auto*action=f.bind("layer.new");bool threw=false;try{f.registry.bind(action,spec("layer.new"),[]{});}catch(const std::invalid_argument&){threw=true;}check(threw,"reject duplicate action callbacks");auto*extra=new QAction("extra",&f.window);extra->setObjectName("legacyStableName");f.registry.bind(extra,spec("layer.new"),[&]{++f.calls;},[](const CommandState&s){return s.hasOtherProject;});check(extra->objectName()=="legacyStableName"&&!extra->isEnabled(),"preserve stable objects and extra predicate");f.state.hasOtherProject=true;f.registry.refresh();extra->trigger();check(f.calls==1,"extra predicate permits current state");delete extra;f.registry.refresh();check(f.registry.manifest().size()==1,"destroyed action removed from manifest");}
void preparation_error(){QWidget window;int called=0;QString reported;CommandRegistry registry(&window,[]{return ready();},[](CommandPreparation,const QString&){throw std::runtime_error("rejected draft");});QAction action("Adjust",&window);registry.bind(&action,spec("adjust.levels"),[&]{++called;});registry.setErrorHandler([&](const QString&error){reported=error;});check(!registry.invoke("adjust.levels")&&reported=="rejected draft"&&called==0,"failed preparation invoked command or omitted error");reported.clear();action.trigger();check(reported=="rejected draft"&&called==0,"Qt activation failed to contain preparation exception");registry.setErrorHandler({});bool threw=false;try{registry.invoke("adjust.levels");}catch(const std::runtime_error&){threw=true;}check(threw&&called==0,"test caller should observe unhandled preparation error");}
}
int main(int argc,char**argv){QApplication app(argc,argv);app.setFont(QFont("Segoe UI",10));try{check(argc==2,"provide one case");const std::string name=argv[1];if(name=="source_predicates")source_predicates();else if(name=="layer_eligibility")layer_eligibility();else if(name=="pending_preparation")pending_preparation();else if(name=="stale_activation")stale_activation();else if(name=="text_shortcuts")text_shortcuts();else if(name=="text_menu_routes")text_menu_routes();else if(name=="catalog_manifest")catalog_manifest();else if(name=="registration")registration();else if(name=="preparation_error")preparation_error();else throw std::runtime_error("unknown case");std::cout<<"PASS "<<name<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
