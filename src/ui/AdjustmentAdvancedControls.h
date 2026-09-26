#pragma once
#include "effects_tools/Levels.h"
#include "effects_tools/Hue.h"
#include "effects_tools/Curves.h"
#include <QWidget>
#include <functional>
#include <memory>

namespace compositor {
// Supplementary controls for the existing adjustment dialog. All callbacks run
// on its GUI thread. Setting state does not emit a change or mutate a document.
class LevelsAdvancedControls final : public QWidget {
public:
    explicit LevelsAdvancedControls(QWidget* parent = nullptr);
    ~LevelsAdvancedControls() override;
    void setAdjustmentJson(std::string);
    std::string adjustmentJson() const;
    void setHistogram(effects_tools::LevelsHistogram);
    const effects_tools::LevelsHistogram& histogram() const;
    bool histogramReady() const;
    std::optional<effects_tools::LevelsSample> sampleMode() const;
    void applySample(std::array<double,3> originalStraightRGB);
    std::function<void(const std::string&)> onChanged;
    std::function<void(std::optional<effects_tools::LevelsSample>)> onSampleModeChanged;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class HueAdvancedControls final : public QWidget {
public:
    explicit HueAdvancedControls(QWidget* parent = nullptr);
    ~HueAdvancedControls() override;
    void setAdjustmentJson(std::string);
    std::string adjustmentJson() const;
    std::optional<effects_tools::HueSample> sampleMode() const;
    bool targeting() const;
    void applySample(effects_tools::PaletteColor compositeColor);
    bool beginTarget(effects_tools::PaletteColor compositeColor);
    void dragTarget(double viewDelta,bool adjustsHue);
    void endTarget();
    std::function<void(const std::string&)> onChanged;
    std::function<void(std::optional<effects_tools::HueSample>)> onSampleModeChanged;
    std::function<void(bool)> onTargetingChanged;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class CurvesAdvancedControls final : public QWidget {
public:
    explicit CurvesAdvancedControls(QWidget* parent = nullptr);
    ~CurvesAdvancedControls() override;
    void setAdjustmentJson(std::string);
    std::string adjustmentJson() const;
    std::function<void(const std::string&)> onChanged;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
