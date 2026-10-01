#pragma once
#include "Selection.h"
#include <string>

namespace compositor::editing {
enum class CanvasUnit { Pixels, Percent, Inches, Centimeters };
struct CanvasSizeDraft {
    int originalWidth,originalHeight;
    double resolution,width,height;
    bool relative{},locked{};
    CanvasUnit unit{CanvasUnit::Pixels};
    CanvasSizeDraft(int width,int height,double resolution);
    bool valid() const;
    double displayed(bool widthAxis) const;
    void set(double value,bool widthAxis);
};
struct CanvasSizeOptions {
    int width,height;
    int anchor{4}; // row-major 0..8
    std::optional<Pixel> fill;
    std::optional<Point> contentOffset;
    Point offset(int oldWidth,int oldHeight) const;
};
// Canvas/crop retain source raster handles. Colored extension is a separate
// bottom layer and leaves the old canvas transparent. Changed size drops selection.
Document canvasResize(const Document&,const CanvasSizeOptions&);
Document cropDocument(const Document&,Rect);
Document trimDocument(const Document&);
struct ImageSizeOptions {
    int width,height;
    double resolution{72};
    Transform::Sampling sampling{Transform::Sampling::High};
};
Document imageResize(const Document&,const ImageSizeOptions&);
Transform mirroredTransform(const Transform&,bool horizontally,double axis);
Document flipCanvas(const Document&,bool horizontally);
Document flipLayers(const Document&,std::span<const std::string> selectedIDs,bool horizontally);
Rect snappedCrop(Rect);
bool validCrop(Rect);
Rect createCrop(Point start,Point end,std::optional<double> ratio = {},bool symmetric = false);
struct CropDrag {
    enum class Mode { Create, Move, Resize };
    Point start;
    Rect original;
    Mode mode{Mode::Create};
    int handle{};
    Rect updated(Point,std::optional<double> ratio = {},bool symmetric = false) const;
};
struct CropSnap {
    std::vector<double> xs,ys;
    double tolerance{};
    Rect apply(Rect,const CropDrag&,Point,std::optional<double> ratio = {},bool symmetric = false) const;
};
CropSnap cropSnapTargets(const Document&,double tolerance);
}
