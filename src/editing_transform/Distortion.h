#pragma once
#include "TransformGeometry.h"
#include <stdexcept>

namespace compositor::editing_transform {
using CancelCheck=std::function<bool()>;
class TransformCancelled:public std::runtime_error {public:TransformCancelled():std::runtime_error("Transform cancelled"){};};
struct Homography {
    std::array<double,9> matrix{1,0,0,0,1,0,0,0,1};
    Point map(Point)const;
    Homography inverse()const;
    static Homography fromCorners(const Corners&);
};
Corners carriedCorners(const Transform& placement,const Transform& enclosing,const Corners& target);
struct WarpOptions {int longestSide{};bool trim{};CancelCheck cancelled;};
struct RasterWarp {std::shared_ptr<const Raster> raster;Transform transform;Rect crop;};
struct GrayWarp {std::shared_ptr<const GrayRaster> raster;Transform transform;};
RasterWarp warpRaster(std::shared_ptr<const Raster>,const Transform&,const Corners&,WarpOptions={});
GrayWarp warpGray(std::shared_ptr<const GrayRaster>,const Transform&,const Corners&,uint8_t exterior=0,WarpOptions={});
Layer distortLayer(const Layer& original,const Transform& draft,const Corners&,WarpOptions={});
class DistortionSession {
public:
    explicit DistortionSession(Layer original);
    bool update(const Transform& draft,const Corners&);
    Layer preview(int longestSide=2048,CancelCheck={})const;
    Layer apply(CancelCheck={});
    Layer cancel();
    const Corners& shape()const{return corners_;}
private:
    Layer original_,result_;
    Transform draft_;
    Corners corners_;
    bool finished_{};
};
}
