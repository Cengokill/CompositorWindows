// Coefficient construction and two-pass byte rounding are derived from Pillow
// Resample.c at bb1d8e8ab8d29048624d96e3ee53cecf7c13d13d (MIT-CMU).
// The full copyright and permission notice is retained in
// dependencies/imaging/notices/Pillow-LICENSE.txt in the Windows source tree.
#include "subject_input.h"
#include <array>
#include <cmath>

namespace compositor::imaging {
namespace {
constexpr int size = 1024;
constexpr int precision = 22;
constexpr int maximumKernel = 61; // 2*ceil(30000/1024)+1.
constexpr std::size_t rowBytes = std::size_t(size)*3;
struct Bound { int first{}, count{}; };
struct Axis {
    int kernel{};
    std::vector<Bound> bounds;
    std::vector<std::int32_t> coefficients;
};
void poll(const ImportOptions& options, SubjectInputStats& stats) {
    ++stats.cancellationChecks;
    checkCancelled(options);
}
int kernelSize(std::uint32_t input) {
    return int(std::ceil(std::max(1., double(input)/size)))*2+1;
}
Axis axis(std::uint32_t input, const ImportOptions& options, SubjectInputStats& stats) {
    Axis out{kernelSize(input), std::vector<Bound>(size), {}};
    out.coefficients.resize(std::size_t(size)*out.kernel);
    const double scale = double(input)/size;
    const double support = std::max(1., scale);
    const double inverse = 1./support;
    std::array<double, maximumKernel> weights{};
    for (int i=0; i<size; ++i) {
        poll(options, stats);
        const double center = (i+.5)*scale;
        const int first = std::max(0, int(center-support+.5));
        const int count = std::min(int(input), int(center+support+.5))-first;
        out.bounds[i] = {first, count};
        double total = 0;
        for (int j=0; j<count; ++j) {
            const double distance = std::abs((j+first-center+.5)*inverse);
            weights[j] = distance<1. ? 1.-distance : 0.;
            total += weights[j];
        }
        for (int j=0; j<count; ++j)
            out.coefficients[std::size_t(i)*out.kernel+j] =
                std::int32_t(.5 + (weights[j]/total)*(1<<precision));
    }
    return out;
}
std::uint8_t straight(const std::uint8_t* pixel, int channel) {
    return pixel[3] ? std::uint8_t(std::min(255U,
        (unsigned(pixel[channel])*255U+unsigned(pixel[3])/2)/pixel[3])) : 0;
}
std::uint8_t rounded(std::int64_t sum) {
    return std::uint8_t(std::clamp<std::int64_t>(sum>>precision, 0, 255));
}
}

std::vector<float> subjectInputTensor(const RgbaImage& image, const ImportOptions& options,
                                     SubjectInputStats* resultStats) {
    SubjectInputStats local;
    auto& stats = resultStats ? *resultStats : local;
    stats = {};
    poll(options, stats);
    checkedBytes(image.width, image.height, 4, options);
    if (image.stride<std::size_t(image.width)*4 ||
        image.stride>std::numeric_limits<std::size_t>::max()/image.height ||
        image.pixels.size()<image.stride*image.height)
        throw std::runtime_error("Invalid RGBA buffer stride or storage");
    const int xKernel = kernelSize(image.width), yKernel = kernelSize(image.height);
    stats.sourceBytes = image.stride*image.height;
    stats.outputBytes = std::uint64_t(size)*size*3*sizeof(float);
    stats.maximumCachedRows = std::uint32_t(yKernel);
    // Pillow Image.py uses this ordering to avoid a huge intermediate for a
    // narrow portrait. Byte rounding makes the ordering numerically observable.
    stats.verticalFirst = image.height>std::uint64_t(image.width)*100 && image.height>size;
    stats.temporaryBytes = std::uint64_t(size)*(xKernel+yKernel)*sizeof(std::int32_t) +
        std::uint64_t(size)*2*sizeof(Bound) + std::uint64_t(yKernel)*(rowBytes+sizeof(int)) +
        maximumKernel*sizeof(double) + rowBytes;
    const auto newBytes = stats.outputBytes+stats.temporaryBytes;
    if (newBytes>options.maxWorkingBytes || stats.sourceBytes>options.maxWorkingBytes-newBytes)
        throw std::runtime_error("Foreground preprocessing exceeds working buffer budget");
    for (std::uint32_t y=0; y<image.height; ++y) {
        poll(options, stats);
        for (std::uint32_t x=0; x<image.width; ++x) {
            if ((x&4095U)==0) poll(options, stats);
            const auto* p = image.pixels.data()+std::size_t(y)*image.stride+std::size_t(x)*4;
            if (p[0]>p[3] || p[1]>p[3] || p[2]>p[3])
                throw std::runtime_error("RGBA buffer is not premultiplied");
            ++stats.validatedPixels;
        }
    }
    auto horizontal = axis(image.width, options, stats);
    auto vertical = axis(image.height, options, stats);
    std::vector<std::uint8_t> rows(std::size_t(yKernel)*rowBytes);
    std::vector<int> tags(yKernel, -1);
    std::vector<float> tensor(std::size_t(size)*size*3);
    std::array<std::uint8_t,rowBytes> verticalRow{};
    constexpr float means[]{.485F,.456F,.406F}, deviations[]{.229F,.224F,.225F};
    for (int y=0; y<size; ++y) {
        poll(options, stats);
        const auto bound = vertical.bounds[y];
        for (int sourceY=bound.first; sourceY<bound.first+bound.count; ++sourceY) {
            const int slot = sourceY%yKernel;
            if (tags[slot]==sourceY) continue;
            poll(options, stats);
            auto* destination = rows.data()+std::size_t(slot)*rowBytes;
            const auto* input = image.pixels.data()+std::size_t(sourceY)*image.stride;
            if (stats.verticalFirst) {
                // The branch requires width<300 at the existing side cap.
                for (std::uint32_t x=0; x<image.width; ++x) {
                    if ((x&63U)==0) poll(options, stats);
                    for (int c=0; c<3; ++c) destination[x*3+c]=straight(input+x*4,c);
                }
            } else {
            for (int x=0; x<size; ++x) {
                if ((x&63)==0) poll(options, stats);
                const auto xb = horizontal.bounds[x];
                const auto* weights = horizontal.coefficients.data()+std::size_t(x)*xKernel;
                std::int64_t sum[3]{1<<(precision-1),1<<(precision-1),1<<(precision-1)};
                for (int j=0; j<xb.count; ++j) {
                    const auto* p = input+std::size_t(xb.first+j)*4;
                    for (int c=0; c<3; ++c) sum[c] += std::int64_t(straight(p,c))*weights[j];
                }
                for (int c=0; c<3; ++c) destination[x*3+c] = rounded(sum[c]);
            }
            }
            tags[slot] = sourceY;
            ++stats.preparedSourceRows;
        }
        const auto* weights = vertical.coefficients.data()+std::size_t(y)*yKernel;
        const int verticalWidth=stats.verticalFirst ? int(image.width) : size;
        for (int x=0; x<verticalWidth; ++x) {
            std::int64_t sum[3]{1<<(precision-1),1<<(precision-1),1<<(precision-1)};
            for (int j=0; j<bound.count; ++j) {
                const auto* p = rows.data()+std::size_t((bound.first+j)%yKernel)*rowBytes+x*3;
                for (int c=0; c<3; ++c) sum[c] += std::int64_t(p[c])*weights[j];
            }
            for (int c=0; c<3; ++c) {
                if (stats.verticalFirst) verticalRow[x*3+c]=rounded(sum[c]);
                else tensor[std::size_t(c)*size*size+y*size+x] =
                         (float(rounded(sum[c]))/255.F-means[c])/deviations[c];
            }
        }
        if (stats.verticalFirst) {
            for (int x=0; x<size; ++x) {
                if ((x&63)==0) poll(options, stats);
                const auto xb=horizontal.bounds[x];
                const auto* xWeights=horizontal.coefficients.data()+std::size_t(x)*xKernel;
                std::int64_t sum[3]{1<<(precision-1),1<<(precision-1),1<<(precision-1)};
                for (int j=0; j<xb.count; ++j)
                    for (int c=0; c<3; ++c)
                        sum[c]+=std::int64_t(verticalRow[(xb.first+j)*3+c])*xWeights[j];
                for (int c=0; c<3; ++c)
                    tensor[std::size_t(c)*size*size+y*size+x]=
                        (float(rounded(sum[c]))/255.F-means[c])/deviations[c];
            }
        }
    }
    poll(options, stats);
    return tensor;
}
}
