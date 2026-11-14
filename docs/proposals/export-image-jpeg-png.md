# Export Image: JPEG and PNG implementation proposal

This is a proposal for review; it does not describe shipped behavior.

## Goal and current behavior

Provide understandable JPEG quality and encoding choices, plus PNG transparency and interlacing options, in **File → Export Image**. Today `src/ui/FileActions.cpp` offers JPEG quality from 0 to 100, remembers it, generates a debounced background export, displays its encoded preview and size, and saves that exact prepared file. `src/imaging/stream_export.cpp` uses Windows Imaging Component (WIC) row encoding and decodes the JPEG preview; it handles RGB and profile-converted CMYK, DPI, ICC profiles, cancellation and temporary destination replacement. PNG is encoded with alpha. `src/imaging/wic_codec.cpp` has a separate in-memory path.

## Proposed controls

- JPEG: retain the existing 0–100 quality setting and its saved value. Offer Standard, Optimized and Progressive encoding as mutually exclusive choices, with a short explanation of progressive loading. Use Standard by default for compatibility. Do not label this scale Photoshop's 0–12 scale or claim equivalent output.
- PNG: offer an interlace checkbox and a transparency choice for ordinary PNG exports. Default to existing alpha-preserving, non-interlaced behavior. Flatten against the selected background when transparency is disabled. Keep Channel Pack's exact channel values independent of these controls.
- Avoid PNG-8 quantization in this change: it changes colors and alpha and requires its own palette, preview and quality design. Keep dimensions, format, RGB/CMYK selection and existing controls intact.
- Changing any applicable setting cancels or supersedes the current job and regenerates both the preview and actual encoded size. Only the latest completed file can be saved. Hide settings for unrelated formats and preserve the existing project and history.

## Encoder selection and feasibility gate

JPEG quality, optimized Huffman tables and progressive scans are separate decisions. Candidate: **libjpeg-turbo**, using its scanline API, standard Huffman encoding for Standard, optimized Huffman coding for Optimized, and progressive coding for Progressive. It supports SIMD and the conventional JPEG format. Benchmark against current WIC on representative photographs, gradients, text and transparent composites: compare output size at visually comparable quality, encode time, peak memory, and decode compatibility. Compare MozJPEG as a size-focused alternative if smaller output justifies its cost; do not claim either is universally optimal without measurements. References: [libjpeg-turbo on MozJPEG](https://www.libjpeg-turbo.org/About/Mozjpeg), [MozJPEG](https://github.com/mozilla/mozjpeg).

Before committing to a backend, verify WIC's available properties and actual output on the supported Windows build. A new dependency is warranted only if WIC cannot reliably provide the required modes or measurements justify it. Check progressive/optimized encoders' full-image coefficient storage and worst-case memory at the 100 MP export ceiling; row input alone does not guarantee bounded memory. Establish an explicit budget and report unsupported cases clearly without damaging an existing destination. Preserve RGB and CMYK, profile embedding, DPI, matte handling and the ability to decode the encoded preview. Compare actual output with independent decoders and inspect JPEG markers; do not silently downgrade a selected mode.

For PNG, first evaluate WIC interlace support and its buffering and cancellation behavior. If it cannot implement the checkbox reliably, evaluate a pinned libpng backend with the same budget and transactional guarantees. PNG compression is lossless; color conversion, transparency removal, and indexed-palette conversion are distinct operations. Sources: [Adobe PNG export](https://helpx.adobe.com/photoshop/desktop/save-and-export/save-files/save-in-png-and-gif-formats.html), [Adobe export settings](https://helpx.adobe.com/photoshop/desktop/save-and-export/export-files-to-different-formats/export-settings-and-export-location-preferences.html).

## Implementation sequence

1. Measure WIC capabilities and candidate JPEG encoders using a representative corpus and Windows x64 release builds. Record image inputs, quality settings, actual sizes, time and memory, and choose the backend and defaults from results.
2. Add typed JPEG mode and PNG settings to `ExportOptions`, validate combinations, and route all relevant callers through existing ownership. Decide explicitly whether the separate `WicCodec::encode` path must implement or reject each setting; never silently ignore it.
3. Implement selected codecs with bounded allocations, cancellation, progress, temporary files and error handling. Pin any new dependency in `dependencies.lock.json` and update build, packaging, notices and source provenance. Preserve ICC, DPI, RGB/CMYK and the existing rendered composite.
4. Add format-specific controls in `FileActions.cpp`; connect each to the existing generation and debounce lifecycle. Preview and displayed size must correspond to the exact bytes eventually saved. Keep saved JPEG quality compatible with existing preferences.
5. Update the user guide with the controls and explain lossless PNG versus lossy JPEG, transparency and progressive loading. Capture a real Windows screenshot for the implementation PR.

## Acceptance checks

- Decode exports with independent tools; inspect JPEG frame markers and scan structure for each selected mode and verify PNG interlace metadata. Check ICC and DPI, RGB and CMYK, alpha and matte pixels, and color consistency with the existing WIC output.
- Exercise zero and maximum JPEG quality, detailed and flat images, transparent edges, large documents, all modes, format switching, rapid repeated changes, cancellation and errors. Confirm old destinations remain recoverable and no stale preview is saved.
- Exercise the actual Export Image dialog, including visibility, quality preference migration, latest generation, actual saved bytes and unchanged document/history. Retain Channel Pack's exact channel semantics.
- Run affected existing imaging, CMYK and export-stream CTest targets and meaningful regressions on Windows; report commands and results. Measure memory and time for the progressive path at representative and maximum supported sizes, and document any explicit restrictions.
