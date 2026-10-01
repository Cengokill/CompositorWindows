# Known issues

The Windows preview follows the observable behavior of Compositor 1.0.4 through 1.4.5. The dependency pin remains `a19db9011282399785dc18efcfded904627bdcc2`. Full Mac equivalence, reciprocal project exchange, and Photoshop compatibility have not been verified. The Metal compositor is not ported.

| Area | Limitation |
| --- | --- |
| Large soft brushes | On a Core Ultra 9 285K / RTX 5070, 4000 px documents with 800 px soft brushes measured 38.6–50.4 ms p95 event-to-DwmFlush latency. Worst stroke start/release was 427/522 ms. Background CPU load was present. This is not a 60 Hz or physical-input latency guarantee. |
| Foreground removal | Hair, fur, and dandelion edges can retain background color. Transparent bubbles fail the quality case; Advanced is not uniformly better. An eight-image run produced seven masks and one no-subject result, with inference around 2.1–2.2 seconds and peak memory around 1.4 GiB. Inspect and refine masks. Object selection on the wand uses the same BiRefNet matte and has the same hair limit. |
| Imports and Camera Raw | PSD and PSB import is 8-bit RGB. CMYK is refused. SVG is rasterized on import. RAW development needs a WIC codec and is a separate step from the Camera Raw filter. Camera Raw is the CPU panel (white balance, exposure, contrast, color, and a simple vignette); it does not include the upright modes that ignore the picture, and it is not a verified match of the Mac 1.4.5 curves. |
| Regression discrepancies | The last full normal run passed 926/932. Six failures concern thumbnail cursor behavior, Gaussian border alpha, a deleted upstream cursor observer, M/L shortcut expectations, and a frozen Levels cancellation reference containing a repaired defect. These remain failures, not parity evidence. |
| Sanitizer shutdown | The last full ASAN run passed 925/932. In addition to the six discrepancies, one test printed PASS then timed out on exit. A separate repeat captured an intermittent allocator wait during CRT shutdown. The cause is unresolved. The distributed executable uses the normal build. |
| Signing and updates | Downloads are unsigned. Updates are manual; the application ignores development update feeds. Development keys/helpers are excluded from runtime packages. |
| Coverage | Installation has been checked on the development machine. Fresh Windows environments, physical pens, multiple monitors, Narrator, high contrast, IME, and a wider range of GPUs have not been verified. |

The former blend-preview test harness crash was repaired; it is separate from the intermittent ASAN shutdown issue. No known ordinary-use crash or project corruption is being accepted as an intentional limitation.

See [validation](VALIDATION.md) for the measured checkpoints. Please [report problems](https://github.com/IAmTheBlurr/CompositorWindows/issues) with the preview version and reproduction steps.
