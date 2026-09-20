#pragma once
#include "image_types.h"
#include <span>
#include <memory>
namespace compositor::imaging {
struct MatteSettings { bool advanced{false}; double refineEdges{12},contrast{25},shiftEdge{0}; };
class ISubjectMaskProvider {
public:
    virtual ~ISubjectMaskProvider()=default;
    virtual GrayMask infer(const RgbaImage&,const ImportOptions& = {})=0;
};
std::vector<float> boxMean(std::span<const float>,std::uint32_t,std::uint32_t,int);
std::vector<float> guidedFilter(std::span<const float> mask,std::span<const float> guide,std::uint32_t,std::uint32_t,int,float epsilon=1e-4F);
GrayMask refineSubjectMask(const GrayMask&,const RgbaImage&,const MatteSettings&,bool preview=false,const GrayMask* existing=nullptr,const ImportOptions& = {});
RgbaImage applySubjectMask(const RgbaImage&,const GrayMask&);
}
