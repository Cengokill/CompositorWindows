#pragma once
#include "Levels.h"
#include "CurveMath.h"
namespace compositor::effects_tools {
struct CurvesSettings {
    LevelsChannel channel{LevelsChannel::RGB};
    std::array<std::vector<Point>,4> channels;
    CurvesSettings();
    bool valid() const;
    bool operator==(const CurvesSettings&)const=default;
};
std::array<Point,256> curveGraph(const CurvesSettings&);
std::optional<size_t> beginCurveDrag(CurvesSettings&,Point curveCoordinates);
void moveCurvePoint(CurvesSettings&,size_t,Point curveCoordinates);
bool removeCurvePoint(CurvesSettings&,size_t);
void resetCurve(CurvesSettings&);
CurvesSettings curvesFromAdjustmentJson(std::string_view);
std::string withCurvesSettings(std::string_view,const CurvesSettings&);
}
