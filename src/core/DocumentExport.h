#pragma once
#include "Document.h"
#include "imaging/stream_export.h"

namespace compositor {
// Selection/editor overlays are excluded. Canonical stack rendering retains global
// document coordinates across stripes, including deterministic grain and masks.
imaging::StreamingExportResult exportDocumentAtomic(const Document&, const std::filesystem::path&,
    imaging::ExportOptions = {}, const imaging::StreamExportLimits& = {}, const imaging::ExportProgress& = {});
}
