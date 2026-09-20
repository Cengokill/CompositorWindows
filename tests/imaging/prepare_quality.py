"""Deterministic licensed-source preparation. Does not run a segmentation model."""
import hashlib
import io
import json
import struct
from pathlib import Path
from PIL import Image, ImageCms, ImageOps

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "evidence/imaging/quality-v1"
CRITERIA = Path(__file__).with_name("quality_criteria.json")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    criteria = json.loads(CRITERIA.read_text(encoding="utf-8"))
    sources = {s["id"]: s for s in json.loads((EVIDENCE / "sources.json").read_text(encoding="utf-8-sig"))}
    prepared = EVIDENCE / "inputs"
    prepared.mkdir(exist_ok=True)
    existing_path = EVIDENCE / "prepared-manifest.json"
    existing = json.loads(existing_path.read_text(encoding="utf-8")) if existing_path.exists() else None
    expected_cases = {c["id"]: c for c in existing["cases"]} if existing else {}
    pending = []
    manifest = {"criteria_sha256": sha(CRITERIA), "model_sha256": criteria["model_sha256"], "cases": []}
    for case in criteria["cases"]:
        source = sources[case["source"]]
        path = EVIDENCE / source["file"]
        if not path.is_file() or sha(path) != source["sha256"]:
            raise RuntimeError(f"Required source missing or hash mismatch: {path}")
        im = ImageOps.exif_transpose(Image.open(path))
        alpha = im.getchannel("A") if "A" in im.getbands() else None
        profile = im.info.get("icc_profile")
        rgb = im.convert("RGB")
        color = "no embedded ICC; assumed sRGB"
        if profile:
            rgb = ImageCms.profileToProfile(rgb, ImageCms.ImageCmsProfile(io.BytesIO(profile)), ImageCms.createProfile("sRGB"), outputMode="RGB")
            # LittleCMS creates identical sRGB transforms with a wall-clock ICC header.
            # Pin only that metadata date to the first frozen preparation; pixel values do not change.
            out_profile = rgb.info.get("icc_profile")
            if out_profile:
                rgb.info["icc_profile"] = out_profile[:24] + struct.pack(">6H", 2026, 9, 20, 1, 33, 41) + out_profile[36:]
            color = "embedded ICC converted to sRGB by Pillow ImageCms"
        scale = min(1.0, criteria["preparation"]["max_long_side"] / max(im.size))
        size = tuple(max(1, round(d * scale)) for d in im.size)
        rgb = rgb.resize(size, Image.Resampling.LANCZOS) if size != im.size else rgb
        alpha = alpha.resize(size, Image.Resampling.LANCZOS) if alpha and size != im.size else alpha
        row = dict(case, source_file=source["file"], source_sha256=source["sha256"], color_handling=color, width=size[0], height=size[1])
        if case["reference"]:
            if alpha is None or alpha.getextrema() != (0, 255):
                raise RuntimeError("Published alpha reference absent or unexpected")
            background = Image.new("RGB", size, tuple(criteria["preparation"]["published_reference_background_rgb"]))
            rgb = Image.composite(rgb, background, alpha)
            ref_path = prepared / (case["id"] + "-reference.png")
            ref_data = io.BytesIO(); alpha.save(ref_data, format="PNG")
            ref_bytes = ref_data.getvalue()
            pending.append((ref_path, ref_bytes))
            row.update(reference_file=ref_path.relative_to(EVIDENCE).as_posix(), reference_sha256=hashlib.sha256(ref_bytes).hexdigest())
        out_path = prepared / (case["id"] + ".png")
        data = io.BytesIO(); rgb.save(data, format="PNG")
        encoded_image = data.getvalue()
        row.update(input_file=out_path.relative_to(EVIDENCE).as_posix(), input_sha256=hashlib.sha256(encoded_image).hexdigest())
        row["crop_pixels"] = [round(case["crop_fraction"][i] * size[i % 2]) for i in range(4)]
        if expected_cases and row != expected_cases.get(case["id"]):
            raise RuntimeError(f"Prepared case changed before writing any outputs: {case['id']}")
        pending.append((out_path, encoded_image))
        manifest["cases"].append(row)
    path = EVIDENCE / "prepared-manifest.json"
    encoded = json.dumps(manifest, indent=2) + "\n"
    if path.exists() and path.read_text(encoding="utf-8") != encoded:
        raise RuntimeError("Prepared manifest changed: create a new corpus version instead of silently replacing it")
    for out_path, content in pending:
        if not out_path.exists():
            out_path.write_bytes(content)
        elif out_path.read_bytes() != content:
            raise RuntimeError(f"Existing prepared bytes changed; preserve and investigate: {out_path}")
    path.write_text(encoded, encoding="utf-8")
    print(f"Prepared {len(manifest['cases'])} required cases; criteria SHA256 {manifest['criteria_sha256']}")


if __name__ == "__main__":
    main()
