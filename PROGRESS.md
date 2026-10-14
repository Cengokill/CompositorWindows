# Current checkpoint

Windows preview **0.1.8** is based on Mac Compositor 1.0.4 at `a19db9011282399785dc18efcfded904627bdcc2`. The public repository is https://github.com/IAmTheBlurr/CompositorWindows. The complete existing Windows commit history is retained.

The current interface has a vector tool rail, custom dark controls, Inter typography, compact adjustment panels, a themed project picker, Mac-informed layer rows, a searchable command palette (Ctrl+Shift+P), a canvas navigator when the document extends past the view, and canvas rulers with draggable guides. Native Windows title bars are the default; View → Appearance → Mac-style title bar enables the saved optional appearance. This preference only changes window chrome.

The initial publication consolidates user/build documentation, adds contributor guidance, and removes 712 generated or obsolete tracked files from the current tree. Local raw evidence and archived material are private to the development workspace. Historical commits remain intact. Known limitations are in KNOWN-ISSUES.md; actual checks are summarized in VALIDATION.md.

The 0.1.8 package identifies source commit `f09ef02473bf83c707c414aeb860aae1a590353f`, with 527 corresponding source files and `sourceDirty: false`. The MSI and portable archive include the runtime, model, licenses, and dependency sources. The matching source ZIP, demo ZIP, hashes, and manifest accompany the release. Six portable deployment checks pass. MSI install, repair, and upgrade checks were not run because a community preview is already registered. Default installation is `%LOCALAPPDATA%\Programs\CompositorWindows\Compositor.exe`.

Maintenance intent: stay aligned with Robbie's core features, editing behavior, design, and project format. The Windows preview now implements the observable behavior from upstream 1.0.4 through 1.4.5 that applies on this port: the later blend modes, folder opacity, guides, finishing adjustments and filters, CPU layer effects, DirectWrite text, line shapes, and PSD, SVG, and RAW import. The dependency pin remains `a19db901` until a project written by the Mac 1.4.5 tree is read here. Mac exchange is unverified. The Metal compositor is not ported; rendering stays on the existing CPU and Direct3D paths.

Keep this as the single current checkpoint. Follow AGENTS.md for documentation boundaries. No implementation backlog is implied by historical reports or old tests; start with the user's current request.
