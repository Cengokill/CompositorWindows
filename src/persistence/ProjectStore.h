#pragma once
#include "core/Document.h"
#include <filesystem>
#include <functional>
#include <string>

namespace compositor {
// Codecs must reject non-PNG, multiple frames, >8-bit samples and non-gray/alpha masks.
// Each decoder must check dimensions before allocating and return canonical top-left pixels.
struct ProjectAssetCodec {
    std::function<std::shared_ptr<const Raster>(const std::filesystem::path&)> readColor;
    std::function<std::shared_ptr<const GrayRaster>(const std::filesystem::path&)> readGray;
    std::function<void(const std::filesystem::path&,const Raster&)> writeColor;
    std::function<void(const std::filesystem::path&,const GrayRaster&)> writeGray;
};
struct OpenProject { Document document; std::string activeLayer; int readVersion{7}; };
enum class SaveFaultPoint { AfterStage, AfterJournal, AfterBackup, AfterInstall, BeforeCleanup };
using SaveFaultInjector=std::function<void(SaveFaultPoint)>;
class IProjectStore {
public:
    virtual ~IProjectStore()=default;
    virtual OpenProject load(const std::filesystem::path&)=0;
    virtual void save(const std::filesystem::path&,const Document&,const std::string& activeLayer)=0;
    virtual void recover(const std::filesystem::path&)=0;
};
// Directory replacement is recoverable with a sibling journal, not claimed atomic.
class ProjectStore final:public IProjectStore {
    ProjectAssetCodec codec_;
    SaveFaultInjector fault_;
    OpenProject loadPackage(const std::filesystem::path&) const;
    void recoverLocked(const std::filesystem::path&);
public:
    explicit ProjectStore(ProjectAssetCodec codec,SaveFaultInjector fault={});
    OpenProject load(const std::filesystem::path&) override;
    void save(const std::filesystem::path&,const Document&,const std::string& activeLayer) override;
    void recover(const std::filesystem::path&) override;
};
// Production WIC adapter. Defined in WicProjectCodec.cpp.
ProjectAssetCodec makeWicProjectCodec();
}
