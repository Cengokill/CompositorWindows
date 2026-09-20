// Reuse the frozen fixture and bounded UIA client; its 37 assertions stay unchanged.
#define main standardAccessibilityWitnessMain
#include "AccessibilityWitness.cpp"
#undef main
#include <QAccessible>

int main(int argc,char** argv){
    QApplication app(argc,argv);if(argc!=2)return 2;
    MainWindow window(true);window.addProject(fixtureDocument(),"Description fixture");window.setWindowTitle("Compositor accessibility description witness");window.show();
    const auto hwnd=reinterpret_cast<HWND>(window.winId());const QString path=QString::fromLocal8Bit(argv[1]);std::thread worker;int exitCode=2;
    QTimer::singleShot(300,&window,[&]{worker=std::thread([&]{QJsonObject output;
        const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try{checked(initialized,"COM");{Client client;auto rootElement=client.root(hwnd);QJsonArray rows;
            for(const auto* label:{L"Foreground with mask",L"Select image: Foreground with mask",L"Select mask: Foreground with mask",L"Unlink mask: Foreground with mask"}){
                auto element=client.named(rootElement.Get(),label);require(bool(element),"named element");auto record=client.describe(element.Get());
                record["full_description"]=property(element.Get(),UIA_FullDescriptionPropertyId);record["item_status"]=property(element.Get(),UIA_ItemStatusPropertyId);rows.append(record);
            }output["uia"]=rows;
            gui(window,[&]{auto* tree=window.findChild<QTreeWidget*>("layersTree");auto* accessible=QAccessible::queryAccessibleInterface(tree);require(accessible&&accessible->tableInterface(),"Qt accessible table");QJsonArray cells;
                for(int row=0;row<accessible->tableInterface()->rowCount();++row)for(int col=0;col<4;++col){auto* cell=accessible->tableInterface()->cellAt(row,col);if(cell)cells.append(QJsonObject{{"row",row},{"column",col},{"name",cell->text(QAccessible::Name)},{"description",cell->text(QAccessible::Description)},{"help",cell->text(QAccessible::Help)}});}output["qt_cells"]=cells;});
            }output["status"]="observed";exitCode=0;
        }catch(const std::exception& e){output["error"]=e.what();}
        if(SUCCEEDED(initialized))CoUninitialize();try{write(path,output);}catch(...){exitCode=2;}QMetaObject::invokeMethod(&window,[&]{app.quit();},Qt::QueuedConnection);
    });});app.exec();if(worker.joinable())worker.join();return exitCode;
}
