#pragma once
#include "core/Document.h"
#include "effects_tools/Color.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>
class QDialog;

namespace compositor::ui {
struct EditPanelResult {
    Document document;
    std::string active;
    bool maskSelected{};
    // Optional pixel-filter merge carries the CURRENT owned mask after rendering.
    std::function<Layer(const Layer&)> mergeLayer;
};
struct EditPanelHost {
    QPointer<QWidget> canvas;
    std::function<bool()> valid;
    std::function<void(std::shared_ptr<const Document>)> preview;
    std::function<void(EditPanelResult)> commit;
    std::function<std::shared_ptr<const Document>()> view;
    std::function<std::optional<Document>()> currentDocument;
    std::function<std::optional<effects_tools::PaletteColor>(Point)> sampleComposite;
    std::function<void(effects_tools::PaletteColor,const QString&,std::function<void(effects_tools::PaletteColor)>)> openColor;
    std::function<void(bool)> closeColor;
    std::function<void()> stateChanged;
    std::function<void()> closed;
    std::function<void(const QString&)> error;
};
// The concrete panel owns immutable worker inputs and QWidget state. Every host
// callback runs on the presentation thread; workers never retain widgets.
class EditPanelSession:public QObject {
public:
    enum class Kind { Levels,Hue,Filter };
    EditPanelSession(QObject* parent,Kind kind,bool live,EditPanelHost host)
        :QObject(parent),kind_(kind),live_(live),host_(std::move(host)){}
    ~EditPanelSession() override=default;
    virtual QDialog* panel()const=0;
    virtual bool committing()const=0;
    virtual void cancel()=0;
    virtual bool samplePress(Point,double,Qt::KeyboardModifiers){return false;}
    virtual bool sampleMove(Point,double,Qt::KeyboardModifiers,bool){return false;}
    virtual void sampleHover(Point){}
    // Complete adjustment state uses the ordinary preview revision and commit
    // path. Unsupported panel kinds and closed/committing editors return false.
    // Invalid settings throw before changing the draft.
    virtual std::string adjustmentSettings()const{return {};}
    virtual std::optional<std::string> originalAdjustmentSettings()const{return {};}
    virtual bool updateAdjustmentSettings(const std::string&,bool){return false;}
    Kind kind()const{return kind_;}
    bool live()const{return live_;}
    QWidget* canvas()const{return host_.canvas;}
    std::shared_ptr<const Document> previewDocument()const{return host_.view?host_.view():nullptr;}
protected:
    Kind kind_;
    bool live_{};
    EditPanelHost host_;
};
}
