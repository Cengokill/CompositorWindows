# Architecture and upstream relationship

Compositor for Windows uses C++20, Qt Widgets, and Direct3D 11. The goal is to bring Robbie Tilton's editing model and visual experience to Windows while using Windows facilities for the window, file system, graphics device, and input.

The [upstream lock](../dependencies.lock.json) identifies the Mac source baseline. The port currently follows Compositor 1.0.4, not every subsequent Mac release. Shared C pixel routines retain the original MIT attribution. Swift/AppKit/Core Image/Vision behavior required Windows implementations; the bundled foreground model is a different implementation from Apple Vision. Source correspondence alone does not prove identical runtime output.

The document and history are independent of UI and GPU ownership. Rendering consumes document state; recreating the graphics device must not change the project. The UI coordinates commands and previews, while persistence and image codecs own their file contracts. This separation lets tests exercise editing, rendering, and recovery without driving every action through a window.

Useful entry points:

| Area | Source |
| --- | --- |
| Document state and history | [src/core](../src/core) |
| Editing and pixel operations | [src/editing](../src/editing), [src/graphics](../src/graphics), [src/effects](../src/effects) |
| Rendering | [src/render](../src/render) |
| Windows interface | [src/ui](../src/ui) |
| Project storage | [src/persistence](../src/persistence) |
| Image codecs and foreground removal | [src/imaging](../src/imaging) |
| Build and test inventory | [CMakeLists.txt](../CMakeLists.txt) |

These links lead to the actual implementation rather than a parallel API inventory. The [project format](project-format-v7.md) documents the external storage contract. [Known issues](../KNOWN-ISSUES.md) covers where the current preview or its verification falls short.

Windows preview versions are independent today. The intended direction is to track upstream releases, evaluate their changes, and integrate them while preserving Windows usability. Automated porting and shared version numbering have not been set up. Enhancements are welcome when they preserve the core editing model and compatibility.
