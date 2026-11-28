#pragma once
#include "Document.h"
#include <array>
#include <optional>
#include "imaging/stream_export.h"

namespace compositor {
// Selection/editor overlays are excluded. Canonical stack rendering retains global
// document coordinates across stripes, including deterministic grain and masks.
imaging::StreamingExportResult exportDocumentAtomic(const Document&, const std::filesystem::path&,
    imaging::ExportOptions = {}, const imaging::StreamExportLimits& = {}, const imaging::ExportProgress& = {});
// A copy of the document that renders only layer `id`: the layer is shown even when hidden, inside its folders (their
// opacity and masks still apply), and a folder keeps the visibility of its contents. Everything else is removed except
// hidden clipping sources, whose coverage still cuts a clipped layer. A clipping base therefore exports without the
// layers clipped to it, and a clipped layer exports cut by its base without the base. Throws for an unknown id.
Document isolateLayerForExport(const Document&, const std::string& id);
// Writes layer `id` alone as a PNG trimmed to its non-transparent pixels, rendered at the canvas position it has in the
// document, so it matches the canvas pixel for pixel. Returns nullopt, writing nothing, when no pixel is visible
// (an adjustment, an empty layer, a folder whose contents are all hidden). Cancellation and failure leave an existing
// destination untouched. A root layer that is only 16-bit raster stays 16-bit; any other result is 8-bit.
std::optional<imaging::StreamingExportResult> exportLayerPngAtomic(const Document&, const std::string& id, const std::filesystem::path&,
    imaging::ExportOptions = {}, const imaging::StreamExportLimits& = {}, const imaging::ExportProgress& = {});
// Empty source means black; "white" is the white constant; layer sources are prefixed with "L".
imaging::StreamingExportResult exportChannelPackAtomic(const Document&, const std::filesystem::path&,
    const std::array<std::string, 4>&, imaging::ExportOptions = {}, const imaging::StreamExportLimits& = {}, const imaging::ExportProgress& = {});
}
