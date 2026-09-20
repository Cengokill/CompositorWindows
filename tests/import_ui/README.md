# Import queue and native drop checks

`ImportUiTests.cpp` exercises the real WIC adapter, `ImportQueue` and actual `MainWindow` drop handlers. Run the integrated `import_ui_tests` target with `QT_QPA_PLATFORM=offscreen`. Required EXIF/ICC fixtures are in `evidence/imaging`; absence is a failure. Results belong in this directory's `build` folder. No Mac image comparison or human interaction acceptance is implied.

The final Release run passed **11 of 11 cases**. `results.log` records the run, `build.log` records its targeted build, and `evidence.json` pins the implementation, harness, inputs and logs. The initial nine-case run is retained in `results-09.log`. This is bounded native evidence; no complete upstream invocation is promoted by this harness alone.

Source contracts read at `a19db9011282399785dc18efcfded904627bdcc2`:

- `EditorSession.swift:599–662`: ordered FIFO requests; one `Import Images` history transaction per request; failures do not consume pixel budget; the first successful image creates the canvas at resolution 72; an entire initially empty batch ignores its supplied drop point; insertion floors center minus half dimensions, appends in source order, inherits the active group/parent and expands that parent.
- `ImageFileDrop.swift:6–57`: resolve providers in order; retain a copy of transient image data until decode completes; aggregate unreadable items.
- `ProjectWorkspace.swift:120–143`: capture the destination before asynchronous work; targeted drops import into an existing project, while external/new-tab image opens create one project per image.
- `ImageImporter.swift:34–60`: validate content type, side limit and accumulated 100-megapixel budget; orient and normalize to sRGB before insertion.
- `ImageImportTests.swift:84–142`, `HistoryTests.swift:103–137`, `ProjectTests.swift:159–180`: dimension/placement/partial-failure order, separate queued undo entries, all-failed no-op, and deferral while a project operation is active.

Workers receive immutable document snapshots and never call a widget. The GUI host verifies that its destination still matches the captured document and active layer, installs the complete result, and records one history entry. A `QPointer` to each project's persistent canvas prevents a closed destination from receiving completion. Native cancellation discards the entire prepared batch, including files already decoded; the source has no equivalent explicit batch cancel UI, so this is an additional Windows behavior with preservation assertions.

The harness covers:

1. Empty project, ignored drop point, Unicode names, ordered partial failure, exact undo/redo raster identity, and all-failed history preservation.
2. Fractional coordinate flooring, group inheritance/expansion and budget exhaustion after earlier successes.
3. FIFO worker execution, no partial GUI document, separate undo entries and GUI-only host callbacks.
4. Busy project deferral, queued cancellation and active cancellation preserving the original.
5. Stale document rejection and destroyed destination safety.
6. Required real TIFF EXIF orientation and embedded ICC normalization.
7. Actual Qt canvas multi-file drop using document coordinates and one history transaction.
8. Actual native cancellation preserving the document/history.
9. Bitmap MIME copy lifetime/premultiplication and rapid external-open separation into tabs.
10. A project already at the default 100-megapixel budget, side limits, and a corrupt first file followed by a successful file.
11. A bitmap queued behind a busy project remains valid after the original MIME object is destroyed; New/Undo commands preserve the busy document before the queued import starts.

Injected delayed decoders are used only in explicitly named queue scheduling tests. Production and the native drop cases use WIC/standalone libheif. Decode cancellation is checked around codec calls; an individual codec call may finish before its discarded result returns. This does not promise instantaneous cancellation.

Qt local file URL and bitmap MIME drops are implemented. Native Windows `IDataObject` virtual-file promise formats are not decoded directly by this module; applications that provide neither a resolved local URL nor bitmap data remain an explicit drop interoperability gap. Internal layer transfer is the separate layer/workspace path. Pixel/color parity, complete upstream invocation mapping and actual cross-application drag acceptance remain separate gates.
