# HDR images for manual testing

These two free images show the same studio photographed by Sergej Majboroda:
one Radiance RGBE `.hdr` and one OpenEXR `.exr`, both 1024 × 512 pixels.
They are unmodified Poly Haven downloads, distributed under CC0 1.0.
Their combined size is about 2.8 MiB. The stretched panoramic layout is normal.

## Quick check in CompositorWindows

1. Choose **File → Import Image…** and open `studio_small_09_1k.exr` from this folder.
   Its layer should display **HDR**. Repeat with `studio_small_09_1k.hdr` to test the other decoder.
2. Select the image layer, then choose **Adjustments → Exposure…**.
   Keep **Preview** checked, **Offset = 0** and **Gamma = 1**.
3. Try **Exposure = -2**, then **-6**. Look at the bright lamps on the right:
   their grids and light patterns become easier to distinguish as exposure falls.
   The rest of the room becomes very dark; this is expected.
4. Click **Cancel**: the original appearance must return. Reopen the adjustment,
   apply a change, then use **Edit → Undo** and **Edit → Redo** to check restoration.
5. Use these images for the PR's save/reopen, EXR export and 16-bit conversion checks.
   Export to another folder so the reference inputs remain unchanged.

The files contain real values above white, not SDR pictures renamed as HDR.
Both passed the project's HDR decoders, Exposure processing and EXR encode/decode
round-trip checks on the Linux host. Native Windows visual verification is separate.
These small inputs are not suitable for measuring large-image performance or
reliably catching a Cancel button during a long operation.

## Source and license

- Asset: [Studio Small 09](https://polyhaven.com/a/studio_small_09)
- Photographer: **Sergej Majboroda**, published by **Poly Haven**
- License: **CC0-1.0**; see [LICENSE.md](LICENSE.md) and [Poly Haven's license page](https://polyhaven.com/license)
- Downloaded: **2026-11-01**
- Original [HDR download](https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/studio_small_09_1k.hdr) — 1,615,248 bytes
- Original [EXR download](https://dl.polyhaven.org/file/ph-assets/HDRIs/exr/1k/studio_small_09_1k.exr) — 1,319,434 bytes

SHA-256 of the unmodified downloads:

```text
e7cfda5f4e98e623db12b8bfd0184e048488e4855d9c83e2751fb44a32e80c45  studio_small_09_1k.hdr
44dcbed056512ecd8afd2a3d7ba73d2dd4c24694d0c72005fafcb2469bbc8fd4  studio_small_09_1k.exr
```
