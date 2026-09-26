#include "MainWindow.h"
#include "UpdatePanel.h"
#include "update/Updater.h"
#include <QApplication>
#include <QTimer>

namespace compositor {
void MainWindow::checkForUpdates(){
    ui::UpdatePanelHost host;
    host.restart=[this](const std::filesystem::path& root){
        // Keep the old process usable if a save prompt or launch fails.
        const bool quit=qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        try{
            if(!close()){qApp->setQuitOnLastWindowClosed(quit);return false;}
            update::launchCurrent(root,{});
        }catch(...){
            // Accepted closing already released every project. Reopen a normal
            // welcome session so a launch error leaves a usable editor.
            closingWindow_=false;
            qApp->setQuitOnLastWindowClosed(quit);
            if(projects_.empty())addEmptyProject(false);
            show();throw;
        }
        qApp->setQuitOnLastWindowClosed(quit);
        if(quit)QTimer::singleShot(0,qApp,&QCoreApplication::quit);
        return true;
    };
    ui::openUpdatePanel(this,std::move(host));
}
}
