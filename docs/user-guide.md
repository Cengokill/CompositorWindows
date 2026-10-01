# Using Compositor on Windows

This Windows 11 x64 preview opens and edits layered images locally. It is an independent port of Compositor 1.0.4 by Robbie Tilton, based on upstream commit `a19db9011282399785dc18efcfded904627bdcc2`. See the release notes for supported workflows and measured limitations.

## Start a project

In an installed build, use the Start menu shortcut. In a portable build, open `Compositor.exe`. Choose **File > New Canvas** to enter canvas dimensions, or **File > Import Image** to import a PNG, JPEG, TIFF or HEIC image. Import adds a layer to the current document. Each project has its own tab, layer selection and tool settings. The Layers panel selects the image or mask you are editing.

Use **File > Open Project** for a saved `.comp` project. A `.comp` project is a directory containing its manifest and image assets. Copy or back up the complete directory. Save with **Ctrl+S**, or use **Save Project As** to make a separate project. An asterisk in the tab indicates unsaved changes. Closing a modified project offers Save, Discard or Cancel.

## Window appearance

Windows title bars and system controls are the default. Turn on **View > Appearance > Mac-style title bar** for colored controls on the left; turn it off to restore the native Windows title bar. The choice applies immediately to the editor and dialogs and is remembered between launches. It changes only the window chrome; the editor's visual design stays the same.

## Navigate and arrange layers

Use **Ctrl+0** to fit the canvas and **Ctrl+1** for actual pixels. Hold **Space** and drag to pan. The mouse wheel pans; Ctrl or Alt with the wheel zooms around the pointer. Pixel Grid is available in the View menu and appears at high zoom.

Select a layer in the Layers panel. Use its visibility control to hide or show it, and the blend and opacity controls to change its appearance. The Layer menu contains duplication, grouping, ordering, clipping and merge operations. A mask thumbnail selects mask editing; select the image thumbnail to return to the image. Mask operations are in the Mask menu. Painting white reveals coverage and painting black hides it.

The **Move** tool moves selected layers. Transform handles scale and rotate them; the Transform panel accepts numeric position, size and angle. Ordinary layer transforms preserve source pixels. A free-distortion draft has Apply and Cancel actions and resamples on Apply. **Escape** cancels an active operation; **Ctrl+Z** undoes a committed change.

## Select, paint and draw

Marquee, Lasso, Polygon and Wand create selections. Shift adds and Alt subtracts. Polygon closes when you click near its first point, double-click, or press Enter; Backspace removes its most recent corner. With a selection tool active in New selection mode, dragging inside a selection moves its outline. With a selection tool active, Ctrl-drag moves selected image pixels; Ctrl+Alt-drag duplicates them. Arrow keys move by one pixel and Shift+arrow by ten.

Brush and Eraser use the current tip controls. Clone, Heal and Retouch expose their own options in the upper toolbar. Select a mask before using the same painting tools to edit coverage. The Foreground and Background controls open the color palette; **X** swaps them and **D** restores defaults. Editing is disabled when the target is hidden, incompatible with the tool, or waiting for another operation.

Gradient keeps a pending preview until Apply. Cancel or Escape discards it. Shape creates rectangles or ellipses with editable style; Shift+U switches the shape kind. Crop has an explicit Apply action. Image and Canvas commands change the image dimensions, canvas bounds or resolution; check the selected anchor and units before applying.

Each project remembers its Freehand/Polygonal lasso choice and separate Expand/Contract amounts. **L** returns to the remembered lasso kind. Crop offers Free, Original, 1:1, 4:3, 3:4, 16:9, and 9:16. When a selection exists, a new crop starts from that selection. The middle mouse button pans the canvas. View → Guides shows document guides and adds one at the vertical center when the document has none. View → Snap includes those guides while transforming.

## Adjust an image

Image > Adjustments and the Filters menu provide color and tonal adjustments, blur, noise, lens correction, content-aware fill, vignette, bloom, tonal contrast, dither, scanlines, and Camera Raw. Select an image layer first; a selection restricts applicable pixel edits. A vignette can cover an empty layer. Inspect the preview, then Apply to commit one undoable change or Cancel to discard it. Adjustment-layer commands keep editable adjustment parameters in the layer stack, including Invert, Black and White, Color Balance, and blur or noise layers.

Layer effects (drop shadow, inner shadow, outer and inner glow, stroke, and color overlay) are stored on the layer and rendered on the CPU. Hidden effects keep their settings. Type (T), or Layer → Text Layer…, starts on-canvas text at the click. With the Move tool, double-click live text to edit it. Drag across the letters, or Shift-click, to highlight a range; double-click a word to highlight that word. Escape discards the edit. Ctrl+Enter, Done, or another tool commits it as one undo step, New Text Layer or Edit Text. The type bar sets the face, size, and color of the selection, or of the whole text when the caret has no selection. Hovering a face in the font menu previews it; leaving the menu without choosing restores the previous face. Several colors and faces in one layer are saved as text runs. A later destructive pixel edit flattens the text. Layer → Ungroup promotes the folder's children. Help → Keyboard Shortcuts lists the catalogued shortcuts. Tab, with the wand active and no modifier, switches between color and object selection. Object selection needs the bundled BiRefNet model.

Filter dialogs reopen the choices last used with Apply, including Curves, Exposure, Grain and background-removal controls. Cancel preserves the previously remembered choices, and each project keeps its own values. A new Gradient Map always starts from the current foreground and background colors. Live adjustment layers retain their own parameters.

Remove Background runs the bundled model offline. Basic uses its initial mask; Advanced exposes refinement, edge shift and contrast. Applying creates or updates an undoable layer mask. Inspect fine edges before accepting the result. Fine hair and fur can retain source-background colors, and transparent objects such as soap bubbles can lose their appearance. Advanced refinement is not always an improvement. A no-subject result leaves the image unchanged; textured backgrounds can be ambiguous. Use the mask painting tools for cleanup.

## Export and exchange files

Choose **File > Export Image** for PNG or JPEG. PNG retains transparency. JPEG uses the selected matte and quality. Wait for the encoded preview to finish; Save writes those prepared bytes. Cancel leaves an existing destination unchanged. Export supports at most 30,000 pixels on either side and 100 million pixels in total; larger editing canvases can require resizing before export.

Project Save preserves layers, masks and editable metadata. Export creates a flattened image for other applications. Copy/Cut use the selected target, Copy Merged uses the visible composite, and Paste inserts clipboard image pixels.

The Windows reader implements project versions 1–7 and writes version 7. Actual Mac-generated compatibility fixtures have not yet been supplied. Keep an original copy when testing project exchange with the Mac application.

## Common shortcuts

Shortcuts apply to the editor when a text field or dialog is not using the keys.

| Action | Shortcut |
|---|---|
| New / Open / Save | Ctrl+N / Ctrl+O / Ctrl+S |
| Save As / Export | Ctrl+Shift+S / Ctrl+Shift+E |
| Close project | Ctrl+W |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z |
| Cut / Copy / Paste | Ctrl+X / Ctrl+C / Ctrl+V |
| Fit / Actual pixels | Ctrl+0 / Ctrl+1 |
| Move / Hand | V / H |
| Marquee / Lasso / Wand | M / L / W |
| Brush / Eraser | B / E |
| Clone / Heal / Retouch | S / J / R |
| Gradient / Shape / Crop | G / U / C |
| Eyedropper / Zoom | I / Z |
| Temporary pan | Hold Space and drag |
| Foreground/background swap / defaults | X / D |
| Cancel a pending gesture | Escape |

Menu items show additional shortcuts and current availability. Preview updates are manual: install the next MSI, or extract the next portable download into a separate folder. Keep projects outside the application folder and back up each complete `.comp` directory. Automatic updates are disabled in this preview.
