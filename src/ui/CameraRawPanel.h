#pragma once
#include "filters/CameraRaw.h"
#include <QWidget>
#include <array>
#include <functional>
#include <optional>
#include <vector>
class QAbstractButton;
class QComboBox;
class QLabel;

namespace compositor::ui {
// Docked Camera Raw Filter controls. Session memory and the OK job live in FilterPanel.
class CameraRawPanel final : public QWidget {
public:
    enum class Tool { None, WhiteBalance, PointColor, Defringe, Guide, Curve, Mixer };
    explicit CameraRawPanel(filters::CameraRawSettings settings, QWidget* parent = nullptr);
    ~CameraRawPanel() override;
    filters::CameraRawSettings rendered() const;
    filters::CameraRawView previewView() const;
    bool whiteBalanceIsAuto() const;
    Tool tool() const { return tool_; }
    void setScope(const filters::CameraRawScope& scope);
    void setReadout(std::optional<std::array<int, 3>> rgb);
    void selectAuto();
    void applyAuto(std::optional<std::pair<double, double>> balance);
    void sampleOriginal(const Pixel& pixel);
    void sampleGraded(const Pixel& pixel);
    void beginTarget(const Pixel& graded, double viewY);
    void dragTarget(double viewY);
    void beginGuide(double x, double y);
    void dragGuide(double x, double y);
    void endGuide();
    void releaseTool();
    std::function<void()> edited;
    std::function<void()> autoRequested;
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void build();
    void publish();
    void sync();
    void arm(Tool next);
    filters::CameraRawSettings settings_;
    filters::CameraRawGroupEyes eyes_;
    filters::CameraRawScope scope_{};
    bool scopeReady_{};
    bool vectorscope_{};
    bool altDown_{};
    int draggingAlt_{};
    bool shadowOverlay_{};
    bool highlightOverlay_{};
    Tool tool_{Tool::None};
    int pointIndex_{};
    int curveChannel_{};
    bool curveParametric_{true};
    int mixerTab_{};
    int mixerFamily_{};
    double pointScratch_{};
    filters::CameraRawSettings targetStart_;
    double targetY_{};
    double targetTone_{};
    double targetHue_{};
    bool targeting_{};
    filters::CameraRawGeometryGuide draftGuide_{};
    bool guiding_{};
    bool writing_{};
    std::vector<std::function<void()>> sync_;
    QWidget* scopeView_{};
    QLabel* readout_{};
    QComboBox* whiteBalance_{};
    QLabel* processSummary_{};
    std::array<QAbstractButton*, 6> tools_{};
};
}
