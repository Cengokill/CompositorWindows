# After the wind

A short editing demonstration for the Compositor Windows preview.
The project and input images below provide a complete editing exercise.
Use the MSI-installed application or the matching portable `Compositor.exe`.
Open the complete `After the wind.comp` directory for the editable result;
`After the wind.png` is the actual exported 1600 × 1000 image.

1. Import `01-backdrop.png` to create a 1600 × 1000 canvas.
2. Import `02-dandelion.png` as a second layer. Use Filters → Remove Background in Basic
   mode; inspect the preview and apply its layer mask.
3. Use Move and the Transform panel to arrange the flower over the circle,
   leaving the title readable. Keep the aspect ratio locked. This project uses
   width 1900, X 187 and Y 37; click Apply Transform.
4. Select the mask. Make a rectangular selection across the bottom of the stem
   and paint black with a 120 px brush. Undo, redo, then undo to restore the stem.
   Deselect and return to the image thumbnail.
5. Preview Hue/Saturation with Hue 90, then Cancel. Reopen the adjustment and
   apply Saturation −45 with Hue and Lightness at zero.
6. Save the complete project as `After the wind.comp`, close and reopen it,
   then export `After the wind.png`.

The native workflow includes import, layers, linked masks, transformation,
selection, painting, undo/redo, adjustment preview/cancel/apply, project
save/reopen, PNG export and normal shutdown. The current editor is shown in [the repository screenshot](../docs/images/editor.png).

Foreground removal can leave background colors around fine
filaments; a mask cannot reconstruct clean foreground colors or transparency.
The broader quality check includes difficult fur, transparent-object and
no-subject cases, as described in the release notes.

## Image permissions

`02-dandelion.png` is the unmodified public-domain photograph **Dandelion**
by Huw Williams (Huwmanbeing), 14 June 2008:
https://commons.wikimedia.org/wiki/File:Dandelion.png . The photographer
dedicated the image to the public domain with an unrestricted fallback grant.

`01-backdrop.png` and `create-backdrop.py` are original artwork created for this
demonstration and released under CC0 1.0. The script renders text with Windows
Georgia and Segoe UI; no font files are redistributed. To regenerate it on
Windows, run `python create-backdrop.py` with Pillow installed.

The demonstration does not imply endorsement by the photographer or the
original Compositor author. Credit Robbie Tilton and
https://github.com/robbietilton/Compositor when presenting the independent port.
