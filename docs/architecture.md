# Initial architecture and fixed acceptance criteria

The implementation uses C++20 and Qt 6.8.3 Widgets with the installed MSVC 2022 x64 14.44.35207 toolset and Windows SDK 10.0.26100.0. Qt's published 6.8 supported platform table explicitly includes Windows 11 x64 / MSVC 2022: https://doc.qt.io/qt-6.8/supported-platforms.html . The baseline source remains untouched.

The value document model has no QWidget, GPU, or UI ownership. Raster storage is immutable premultiplied RGBA8 sRGB with 256-pixel tiles. Gray masks represent coverage without color conversion. Renderer input is an immutable document snapshot; history owns transaction and saved revision decisions. File codecs expose explicit conversions. The desktop owns HWND/D3D11/D2D lifetimes; device recreation cannot change document state. Software and WARP remain independent validation routes.

Before calibration: integer state and unaffected pixels require exact equality. Synthetic source-over/blend formula tests allow one 8-bit channel unit for final rounding only. HLSL/float CPU quadrature comparison uses maximum one gray byte plus explicit crossing/opacity/tail invariants. Mac rasterization comparisons remain blocked until reference fixtures exist; no global similarity score substitutes for local errors. No tolerance may be loosened to close a failing case without a recorded operation-specific decision.

Initial responsiveness target: p95 input-to-completed-preview update below 16.7 ms on the declared test hardware for the specified brush workload, with separately reported commit latency and CPU/WARP profiles. This is a target, not an upstream measurement. Benchmark boundaries include GPU readback completion when pixels are consumed by the CPU.

The initial renderer's implemented subset and deviations must remain in the ledger. A runnable experiment is not the full application acceptance gate.
