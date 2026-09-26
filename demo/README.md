# After the wind

A short editing demonstration for the Compositor Windows community preview.
Use the MSI-installed application or the matching portable `Compositor.exe`.
Keep recording at normal speed, including processing waits.

1. Import `01-backdrop.png` to create a 1600 × 1000 canvas.
2. Import `02-dandelion.png` as a second layer. Use Remove Background in Basic
   mode; inspect the preview and apply its layer mask.
3. Use Move and the Transform panel to arrange the flower over the circle,
   leaving the title readable. Keep the aspect ratio locked.
4. Select the mask and paint black over unwanted edge fragments; undo and redo
   one stroke. Return to the image thumbnail.
5. Preview a Levels or Hue/Saturation adjustment, cancel once, then apply a
   modest adjustment. Undo and redo to compare.
6. Save the complete project as `After the wind.comp`, close and reopen it,
   then export `After the wind.png`.

The reusable project and exported result are supplied when the demonstration
is completed. Foreground removal can leave background colors around fine
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
https://github.com/robbietilton/Compositor when presenting the community port.
