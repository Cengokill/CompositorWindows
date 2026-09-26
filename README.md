# Compositor for Windows

A native Windows 11 port of **Compositor**, the image editor created by **[Robbie Tilton](https://robbietilton.com/compositor)**.

Layers, masks, selections, painting, adjustments, filters, and offline background removal, with an interface closely informed by the original Mac app. Windows window controls are the default; an optional Mac-style title bar is available in **View → Appearance**.

![Compositor on Windows, editing the After the wind demonstration](docs/images/editor.png)

## Start here

**[Download the Windows preview](https://github.com/IAmTheBlurr/CompositorWindows/releases)** · **[User guide](docs/user-guide.md)** · **[Try the demo](demo/README.md)** · **[Report an issue](https://github.com/IAmTheBlurr/CompositorWindows/issues)**

Use the **x64 MSI**, or extract the **portable ZIP** and run `Compositor.exe`. Windows 11 x64 is supported. Runtime libraries and the foreground removal model are bundled; no account or first-run model download is needed. These preview builds are unsigned and updates are manual.

Projects are `.comp` directories. Keep the whole directory when copying or backing up a project. Export flattened PNG or JPEG images for other applications.

This is a working preview with [known limitations](KNOWN-ISSUES.md). Large soft brushes can pause; difficult hair, fur, and transparent subjects can need mask cleanup. Full Mac parity and reciprocal project exchange have not been verified.

## Robbie made Compositor

The original application, its editing ideas, and its design came from Robbie. This port exists because he made Compositor and released its source. His work is the foundation and the continuing point of reference for this project.

**[Original source](https://github.com/robbietilton/Compositor)** · **[Compositor for Mac](https://robbietilton.com/compositor)** · **[Robbie on X](https://x.com/robbietilton)**

The Windows effort was initiated and directed by **[IAmTheBlurr](https://github.com/IAmTheBlurr)** and developed primarily with agentic AI using Codex. It is independently maintained. No affiliation or endorsement by Robbie is implied. Original copyright and MIT notices are preserved.

This Windows port was developed from Robbie Tilton's original Mac source. Its code was not forked from or derived from any other Windows port effort.

## Following the original

The intent is to stay in step with Robbie's Compositor: its core tools, editing behavior, design, and project format. Windows improvements and thoughtful enhancements are welcome when they fit that foundation.

**The current port is based on Mac Compositor 1.0.4**, at [`a19db90`](https://github.com/robbietilton/Compositor/tree/a19db9011282399785dc18efcfded904627bdcc2). Newer Mac releases exist; the Windows port has not caught up with them. Windows preview numbers currently use `0.1.x`. Shared upstream version numbering and automated update handling are future work; they are not active synchronization today.

## Have at it

Fork it. Fix something annoying. Make something beautiful. Try something weird. Throw your Codex token budget at an idea and see what happens.

Small fixes and ambitious experiments are welcome. Open an issue, send a PR, or take your fork somewhere surprising. Include what changed and how you checked it. **PRs are welcome; merging depends on fit.** Changes that preserve the core while making the Windows experience better are especially welcome. Useful discoveries may also belong in the original project.

See [contributing](CONTRIBUTING.md) for the short version of working on the code.

## Build and explore

Use Windows 11 x64, PowerShell 7, Visual Studio 2022 C++ tools, Windows SDK 10.0.26100.0, and Python 3.12 for initial model conversion. Dependency inputs are pinned. For the locked codec binaries, retain an extracted matching portable download.

```powershell
. ./scripts/bootstrap.ps1 -Python C:\Python312\python.exe -RuntimeDirectory C:\Extracted\CompositorWindows
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release --target Compositor --parallel 4
```

The [build instructions](docs/source-package.md) cover runtime assets and tests. The [documentation index](docs/README.md) groups tutorials, how-to guides, reference, and explanation for people and coding agents alike. The code is the authority for implementation; documentation explains how to use it and the decisions that need context.

## License and credits

Application source is [MIT licensed](LICENSE), retaining **Copyright (c) 2026 Wonder Assembly LLC**. Qt, image codecs, ONNX Runtime, model weights, and the Inter font have their own licenses and notices in `dependencies/` and `assets/`; downloads include the applicable notices and corresponding sources. Demo artwork and photography have [separate credits](demo/README.md#image-permissions).
