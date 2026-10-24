#include "ui/CommandPalette.h"
#include "ui/CommandRegistry.h"
#include "ui/KeyboardShortcutDialog.h"
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTest>
#include <QVBoxLayout>
#include <algorithm>
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
    s=ready();s.repeatLastFilter=true;check(commandEnabled(CommandGate::RepeatLastFilter,s)&&commandEnabled(CommandGate::Adjust,s),"ready Last Filter matches Adjust");
    s.hueSaturation=true;check(!commandEnabled(CommandGate::RepeatLastFilter,s)&&!commandEnabled(CommandGate::Adjust,s),"Hue disables Last Filter with Adjust");
    s=ready();s.repeatLastFilter=true;s.warpStroke=true;check(!commandEnabled(CommandGate::RepeatLastFilter,s)&&!commandEnabled(CommandGate::Adjust,s),"retouch disables Last Filter with Adjust");
    s=ready();s.repeatLastFilter=true;s.adjustmentEditing=true;check(!commandEnabled(CommandGate::RepeatLastFilter,s)&&!commandEnabled(CommandGate::Adjust,s),"live adjustment disables Last Filter with Adjust");
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
void palette_filter(){
    Fixture f;f.bind("help.command_palette");f.bind("tool.brush");f.bind("edit.undo");f.bind("view.fit");
    auto* duplicate=new QAction("Brush copy",&f.window);f.registry.bind(duplicate,spec("tool.brush"),[&]{++f.calls;});
    const auto listed=f.registry.listCommands();
    std::set<QString> ids;for(const auto& item:listed)check(ids.insert(item.id).second,"listCommands unique ids");
    check(ids.count("help.command_palette")&&ids.count("tool.brush"),"bound commands listed");
    check(filterCommands(listed,{}).size()==listed.size()-1,"empty query omits palette command");
    auto filtered=filterCommands(listed,"BRUSH");
    check(filtered.size()==1&&filtered[0].id=="tool.brush","case-insensitive tool filter");
    check(filterCommands(listed,"help.command_palette").empty(),"palette command excluded from results");
    filtered=filterCommands(listed,"undo");
    check(!filtered.empty()&&filtered[0].id=="edit.undo","label prefix ranked first");
    filtered=filterCommands(listed,"View");
    check(std::any_of(filtered.begin(),filtered.end(),[](const CommandListItem& item){return item.id=="view.fit";}),"menu match");
    filtered=filterCommands(listed,"tool.brush");
    check(filtered.size()==1&&filtered[0].id=="tool.brush","id match");
}
void palette_keyboard(){
    Fixture f;f.bind("tool.brush");f.bind("tool.move");f.bind("edit.undo");f.line->setFocus();QApplication::processEvents();
    auto* palette=new CommandPalette(&f.window,f.registry,f.line);palette->popup();QApplication::processEvents();
    auto* search=palette->findChild<QLineEdit*>("commandPaletteSearch");auto* list=palette->findChild<QListWidget*>("commandPaletteList");
    check(search&&list&&search->hasFocus(),"search focused");
    const int start=list->currentRow();
    QTest::keyClick(search,Qt::Key_Down);check(search->hasFocus()&&list->currentRow()==start+1,"down keeps search focus");
    QTest::keyClick(search,Qt::Key_Up);check(search->hasFocus()&&list->currentRow()==start,"up restores row");
    QTest::keyClick(search,Qt::Key_Escape);QApplication::processEvents();
    check(f.line->hasFocus(),"escape restores prior field");
}
void palette_enabled(){
    Fixture f;f.bind("layer.new");f.state.importing=true;f.registry.refresh();
    auto* palette=new CommandPalette(&f.window,f.registry,f.line);palette->popup();QApplication::processEvents();
    auto* search=palette->findChild<QLineEdit*>("commandPaletteSearch");
    QTest::keyClicks(search,"New Layer");QTest::keyClick(search,Qt::Key_Return);QApplication::processEvents();
    check(f.calls==0&&palette->isVisible(),"disabled command not invoked");
    palette->close();
}
void palette_invoke(){
    Fixture f;f.bind("layer.new");f.line->setFocus();QApplication::processEvents();
    auto* palette=new CommandPalette(&f.window,f.registry,f.line);palette->popup();QApplication::processEvents();
    auto* search=palette->findChild<QLineEdit*>("commandPaletteSearch");
    QTest::keyClicks(search,"New Layer");QTest::keyClick(search,Qt::Key_Return);QApplication::processEvents();
    check(f.calls==1&&f.line->hasFocus(),"invoke restores prior focus");
}
QList<QKeySequence> keys(std::initializer_list<const char*> values){QList<QKeySequence> result;for(auto* value:values)result.push_back(QKeySequence(value));return result;}
void shortcut_assign_clear(){
    Fixture f;auto*move=f.bind("tool.move");auto*redo=f.bind("edit.redo");auto*layer=f.bind("layer.new");
    auto*secondary=new QAction("Move copy",&f.window);f.registry.bind(secondary,spec("tool.move"),[&]{++f.calls;});f.registry.releaseShortcut(secondary);
    check(redo->shortcuts()==keys({"Ctrl+Shift+Z","Ctrl+Y"})&&f.registry.defaultShortcuts()["edit.redo"]==keys({"Ctrl+Shift+Z","Ctrl+Y"}),"Redo ships with both bindings");
    check(move->shortcuts()==keys({"V"})&&secondary->shortcuts().isEmpty(),"released secondary action carries no shortcut");
    check(f.registry.applyShortcuts({{"tool.move",keys({"K"})},{"layer.new",{}},{"edit.redo",keys({"Ctrl+Y"})}}),"valid assignment applies");
    check(move->shortcuts()==keys({"K"})&&secondary->shortcuts().isEmpty()&&layer->shortcuts().isEmpty()&&redo->shortcuts()==keys({"Ctrl+Y"}),"actions follow assigned and cleared shortcuts");
    check(f.registry.invokeShortcut(QKeySequence("K"))&&f.calls==1,"direct dispatch uses the new key");
    check(!f.registry.invokeShortcut(QKeySequence("V"))&&!f.registry.invokeShortcut(QKeySequence("Ctrl+Shift+N"))&&f.calls==1,"old and cleared keys no longer dispatch");
    const auto before=f.registry.shortcuts();QString error;
    check(!f.registry.applyShortcuts({{"tool.move",keys({"Escape"})}},&error)&&!error.isEmpty(),"reserved canvas key rejected");
    check(!f.registry.applyShortcuts({{"tool.move",{QKeySequence("Ctrl+K, Ctrl+L")}}},&error),"multi-chord key rejected");
    check(!f.registry.applyShortcuts({{"no.such",keys({"F9"})}},&error),"unknown command rejected");
    check(f.registry.shortcuts()==before&&move->shortcuts()==keys({"K"}),"invalid input leaves mappings unchanged");
    check(shortcutProblem(QKeySequence("Shift+Delete")).isEmpty()&&shortcutProblem(QKeySequence("F11")).isEmpty(),"shipped special keys remain assignable");
}
void shortcut_conflicts(){
    Fixture f;auto*move=f.bind("tool.move");auto*redo=f.bind("edit.redo");f.bind("layer.new");
    auto conflicts=findShortcutConflicts({{"a",keys({"K"})},{"b",keys({"Ctrl+K","K"})},{"c",keys({"Ctrl+Y","Ctrl+Y"})}});
    check(conflicts.size()==1&&conflicts[0].key==QKeySequence("K")&&conflicts[0].ids==QStringList({"a","b"}),"duplicate across commands reported once; repeats within a command are not conflicts");
    const auto before=f.registry.shortcuts();QString error;
    check(!f.registry.applyShortcuts({{"tool.move",keys({"Ctrl+Y"})}},&error)&&error.contains("edit.redo"),"conflict with Redo's second binding rejected");
    check(f.registry.shortcuts()==before&&move->shortcuts()==keys({"V"})&&redo->shortcuts()==keys({"Ctrl+Shift+Z","Ctrl+Y"}),"conflict leaves actions unchanged");
    check(f.registry.applyShortcuts({{"tool.move",keys({"Ctrl+Y"})},{"edit.redo",keys({"Ctrl+Shift+Z"})}}),"moving a key resolves the conflict");
}
void shortcut_persistence(){
    QTemporaryDir dir;check(dir.isValid(),"temporary settings directory");const auto path=dir.filePath("shortcuts.ini");
    const ShortcutMap changes{{"tool.move",keys({"K"})},{"layer.new",{}},{"edit.redo",keys({"Ctrl+Y"})}};
    {Fixture f;f.bind("tool.move");f.bind("edit.redo");f.bind("layer.new");QSettings settings(path,QSettings::IniFormat);
        check(f.registry.saveShortcuts(changes,settings),"save succeeds");
        settings.beginGroup(shortcutSettingsGroup);check(settings.childKeys().size()==3&&settings.value("tool.move").toString()=="K"&&settings.value("layer.new").toString().isEmpty(),"only overrides stored, cleared as empty");settings.endGroup();
        QString error;check(!f.registry.saveShortcuts({{"tool.move",keys({"Ctrl+Y"})}},settings,&error)&&!error.isEmpty(),"conflicting save fails");
        settings.beginGroup(shortcutSettingsGroup);check(settings.value("tool.move").toString()=="K","failed save leaves stored settings");settings.endGroup();}
    {Fixture f;auto*move=f.bind("tool.move");auto*redo=f.bind("edit.redo");auto*layer=f.bind("layer.new");QSettings settings(path,QSettings::IniFormat);
        check(f.registry.restoreShortcuts(settings),"fresh registry restores");
        check(move->shortcuts()==keys({"K"})&&layer->shortcuts().isEmpty()&&redo->shortcuts()==keys({"Ctrl+Y"}),"fresh instance restores overrides");
        check(f.registry.saveShortcuts(f.registry.defaultShortcuts(),settings),"restore defaults saves");
        check(move->shortcuts()==keys({"V"})&&redo->shortcuts()==keys({"Ctrl+Shift+Z","Ctrl+Y"})&&layer->shortcuts()==keys({"Ctrl+Shift+N"}),"defaults restored including both Redo bindings");
        settings.beginGroup(shortcutSettingsGroup);check(settings.childKeys().isEmpty(),"defaults leave no overrides");settings.endGroup();}
    {QSettings settings(path,QSettings::IniFormat);settings.beginGroup(shortcutSettingsGroup);settings.setValue("no.such","F9");settings.setValue("tool.move","Ctrl+K, Ctrl+L");settings.setValue("layer.new","Escape");settings.setValue("edit.redo",42);settings.endGroup();settings.sync();
        Fixture f;auto*move=f.bind("tool.move");auto*redo=f.bind("edit.redo");f.bind("layer.new");const auto before=f.registry.shortcuts();
        f.registry.restoreShortcuts(settings);check(f.registry.shortcuts()==before&&move->shortcuts()==keys({"V"})&&redo->shortcuts().size()==2,"malformed and stale values ignored");
        settings.remove(shortcutSettingsGroup);settings.setValue(QString(shortcutSettingsGroup)+"/tool.move","Ctrl+Y");settings.sync();
        check(!f.registry.restoreShortcuts(settings)&&f.registry.shortcuts()==before,"conflicting stored values ignored");}
    {Fixture f;auto*move=f.bind("tool.move");f.bind("edit.redo");f.bind("layer.new");
        QSettings settings(path,QSettings::IniFormat);settings.clear();settings.sync();
        QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::ReadUser);
        QString error;const bool saved=f.registry.saveShortcuts({{"tool.move",keys({"K"})}},settings,&error);
        QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ReadUser|QFileDevice::WriteUser);
        check(!saved&&!error.isEmpty()&&move->shortcuts()==keys({"V"})&&f.registry.shortcuts()["tool.move"]==keys({"V"}),"unwritable settings keep active shortcuts");}
}
QTreeWidgetItem* shortcutRow(QTreeWidget* list,const QString& id){for(int i=0;i<list->topLevelItemCount();++i)if(list->topLevelItem(i)->data(0,Qt::UserRole).toString()==id)return list->topLevelItem(i);throw std::runtime_error("shortcut row missing");}
void shortcut_dialog(){
    Fixture f;auto*move=f.bind("tool.move");f.bind("edit.redo");f.bind("layer.new");int commits=0;bool accept=false;
    auto open=[&]{return new KeyboardShortcutDialog(&f.window,f.registry,[&](const ShortcutMap& draft,QString* error){++commits;return accept?f.registry.applyShortcuts(draft,error):false;});};
    auto* dialog=open();dialog->show();QApplication::processEvents();
    auto* list=dialog->findChild<QTreeWidget*>("shortcutList");auto* primary=dialog->findChild<QKeySequenceEdit*>("shortcutPrimary");auto* save=dialog->findChild<QPushButton*>("shortcutSave");auto* status=dialog->findChild<QLabel*>("shortcutStatus");
    check(list&&primary&&save&&status,"dialog controls");
    check(shortcutRow(list,"edit.redo")->text(2).contains("Ctrl+Y")&&shortcutRow(list,"edit.redo")->text(2).contains("Ctrl+Shift+Z"),"Redo row shows both bindings");
    list->setCurrentItem(shortcutRow(list,"tool.move"));check(primary->keySequence()==QKeySequence("V"),"selection loads current key");
    primary->setFocus();QTest::keyClick(primary,Qt::Key_Y,Qt::ControlModifier);QApplication::processEvents();
    check(!save->isEnabled()&&status->text().contains("Save is unavailable")&&status->text().contains("Redo"),"conflict explained and Save blocked");
    save->click();check(commits==0&&move->shortcuts()==keys({"V"}),"blocked Save does not commit");
    dialog->findChild<QPushButton*>("shortcutClear")->click();check(save->isEnabled()&&shortcutRow(list,"tool.move")->text(2).isEmpty()&&dialog->draft().at("tool.move").isEmpty(),"Clear resolves the conflict");
    primary->setFocus();QTest::keyClick(primary,Qt::Key_K);QApplication::processEvents();check(dialog->draft().at("tool.move")==keys({"K"}),"assignment recorded in draft");
    save->click();QApplication::processEvents();check(commits==1&&dialog->isVisible()&&move->shortcuts()==keys({"V"})&&!status->text().isEmpty(),"failed commit keeps dialog and active shortcuts");
    dialog->findChild<QPushButton*>("shortcutCancel")->click();check(dialog->result()==QDialog::Rejected&&move->shortcuts()==keys({"V"})&&f.registry.shortcuts()["tool.move"]==keys({"V"}),"Cancel discards the draft");
    delete dialog;accept=true;dialog=open();dialog->show();QApplication::processEvents();
    list=dialog->findChild<QTreeWidget*>("shortcutList");primary=dialog->findChild<QKeySequenceEdit*>("shortcutPrimary");
    list->setCurrentItem(shortcutRow(list,"tool.move"));primary->setFocus();QTest::keyClick(primary,Qt::Key_K);QApplication::processEvents();
    dialog->findChild<QPushButton*>("shortcutSave")->click();check(dialog->result()==QDialog::Accepted&&move->shortcuts()==keys({"K"}),"Save applies the draft");
    delete dialog;dialog=open();list=dialog->findChild<QTreeWidget*>("shortcutList");
    check(shortcutRow(list,"tool.move")->text(2)=="K","dialog reflects active shortcuts");
    dialog->findChild<QPushButton*>("shortcutRestoreDefaults")->click();
    check(dialog->draft()==f.registry.defaultShortcuts()&&shortcutRow(list,"tool.move")->text(2)=="V"&&move->shortcuts()==keys({"K"}),"Restore Defaults updates the draft only");
    delete dialog;
}}
int main(int argc,char**argv){QApplication app(argc,argv);app.setFont(QFont("Segoe UI",10));try{check(argc==2,"provide one case");const std::string name=argv[1];if(name=="source_predicates")source_predicates();else if(name=="layer_eligibility")layer_eligibility();else if(name=="pending_preparation")pending_preparation();else if(name=="stale_activation")stale_activation();else if(name=="text_shortcuts")text_shortcuts();else if(name=="text_menu_routes")text_menu_routes();else if(name=="catalog_manifest")catalog_manifest();else if(name=="registration")registration();else if(name=="preparation_error")preparation_error();else if(name=="palette_filter")palette_filter();else if(name=="palette_keyboard")palette_keyboard();else if(name=="palette_enabled")palette_enabled();else if(name=="palette_invoke")palette_invoke();else if(name=="shortcut_assign_clear")shortcut_assign_clear();else if(name=="shortcut_conflicts")shortcut_conflicts();else if(name=="shortcut_persistence")shortcut_persistence();else if(name=="shortcut_dialog")shortcut_dialog();else throw std::runtime_error("unknown case");std::cout<<"PASS "<<name<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
