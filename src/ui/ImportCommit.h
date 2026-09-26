#pragma once
#include "MainWindow.h"
#include "ImportActions.h"
#include <stdexcept>
#include <utility>

namespace compositor::ui {
// Install a prepared import and its history entry atomically. UI rendering and
// notifications run only after this transaction has finished successfully.
inline bool commitPreparedImport(EditorProject& project,const ImportResult& result){
    if(project.document!=result.before.document||project.active!=result.before.active)
        throw std::runtime_error("The import destination changed; the prepared images were not applied");
    const bool first=!project.document;
    auto document=result.after.document;
    auto active=result.after.active;
    std::vector<std::string> selected{active};
    auto collapsed=project.collapsedGroups;
    for(const auto& id:result.expandedGroups)collapsed.erase(id);
    project.history.begin("Import Images",project.document,project.active);
    try{
        project.document=std::move(document);
        project.active=std::move(active);
        project.history.end(project.document,project.active);
    }catch(...){
        if(auto snapshot=project.history.cancel()){
            project.document=std::move(snapshot->document);
            project.active=std::move(snapshot->activeLayer);
        }
        throw;
    }
    project.selected=std::move(selected);
    project.maskSelected=false;
    project.collapsedGroups=std::move(collapsed);
    return first;
}
}
