#pragma once
#include "Document.h"
#include <array>
#include "imaging/stream_export.h"

namespace compositor {
// Selection/editor overlays are excluded. Canonical stack rendering retains global
// document coordinates across stripes, including deterministic grain and masks.
imaging::StreamingExportResult exportDocumentAtomic(const Document&, const std::filesystem::path&,
    imaging::ExportOptions = {}, const imaging::StreamExportLimits& = {}, const imaging::ExportProgress& = {});
// Empty source means black; "white" is the white constant; layer sources are prefixed with "L".
imaging::StreamingExportResult exportChannelPackAtomic(const Document&, const std::filesystem::path&,
    const std::array<std::string, 4>&, imaging::ExportOptions = {}, const imaging::StreamExportLimits& = {}, const imaging::ExportProgress& = {});
}
