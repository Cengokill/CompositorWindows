"""Frozen reference metrics and visual review. No predictions become reference labels."""
import hashlib
import html
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageOps


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def alpha(path):
    im = Image.open(path)
    if im.mode != "L":
        raise RuntimeError(f"Expected gray8 alpha: {path}")
    return np.asarray(im, dtype=np.float64) / 255


def boundary(binary):
    padded = np.pad(binary, 1, constant_values=False)
    interior = binary & padded[:-2,1:-1] & padded[2:,1:-1] & padded[1:-1,:-2] & padded[1:-1,2:]
    return binary & ~interior


def disk_band(binary, radius):
    """Exact integer-grid Euclidean distance <= radius, without a new dependency."""
    height, width = binary.shape
    padded = np.pad(binary, radius, constant_values=False)
    result = np.zeros_like(binary)
    for dy in range(-radius, radius+1):
        for dx in range(-radius, radius+1):
            if dx*dx+dy*dy <= radius*radius:
                result |= padded[radius+dy:radius+dy+height, radius+dx:radius+dx+width]
    return result


def reference_metrics(predicted, reference, thresholds):
    error = abs(predicted - reference)
    pred, ref = predicted >= .5, reference >= .5
    pb, rb = boundary(pred), boundary(ref)
    if not rb.any() or not pb.any():
        f1 = 0.0
    else:
        precision = float(np.mean(disk_band(rb, 2)[pb]))
        recall = float(np.mean(disk_band(pb, 2)[rb]))
        f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    band = disk_band(rb, 8) | ((reference > 0) & (reference < 1))
    fg, bg = reference >= .95, reference <= .05
    values = {
        "alpha_mae": float(error.mean()), "boundary_mae": float(error[band].mean()),
        "foreground_recall": float(np.mean(pred[fg])), "background_mean_alpha": float(predicted[bg].mean()),
        "iou": float(np.count_nonzero(pred & ref) / np.count_nonzero(pred | ref)), "boundary_f1": f1,
    }
    checks = {key: values[key.removesuffix("_max").removesuffix("_min")] <= value if key.endswith("_max")
              else values[key.removesuffix("_min")] >= value for key, value in thresholds.items()}
    return dict(values, checks=checks, screening="pass" if all(checks.values()) else "fail", meaning="agreement with published cutout alpha; physical accuracy unknown")


def observable(mask):
    return {"coverage_fraction": float(np.mean(mask >= .5)), "mean_alpha": float(mask.mean()),
            "soft_alpha_fraction": float(np.mean((mask > .05) & (mask < .95)))}


def checker(size):
    yy, xx = np.indices((size[1], size[0]))
    g = np.where((xx // 16 + yy // 16) % 2, 195, 235).astype(np.uint8)
    return Image.fromarray(np.repeat(g[:, :, None], 3, axis=2), "RGB")


def tile(im, size=(320, 260), background="#20252b"):
    out = Image.new("RGB", size, background)
    fit = ImageOps.contain(im.convert("RGB"), size, Image.Resampling.LANCZOS)
    out.paste(fit, ((size[0]-fit.width)//2, (size[1]-fit.height)//2))
    return out


def font(size=17):
    path = Path("C:/Windows/Fonts/segoeui.ttf")
    return ImageFont.truetype(str(path), size) if path.exists() else ImageFont.load_default()


def save_visuals(source, directory, crop, status):
    visual = {"source": source}
    for mode in ("basic", "advanced"):
        if status == "no_subject":
            blank = Image.new("RGB", source.size, "#252a30")
            ImageDraw.Draw(blank).text((12, 12), "No subject detected; no matte returned", fill="white", font=font())
            visual[mode] = blank
        else:
            cutout = Image.open(directory / f"{mode}-cutout.png").convert("RGBA")
            visual[mode] = Image.alpha_composite(checker(source.size).convert("RGBA"), cutout).convert("RGB")
            visual[mode].save(directory / f"{mode}-checker.png")
            for color, name in (("white", "white"), ("#121820", "dark")):
                bg = Image.new("RGBA", source.size, color)
                Image.alpha_composite(bg, cutout).convert("RGB").save(directory / f"{mode}-{name}.png")
        visual[mode].crop(crop).save(directory / f"{mode}-edge.png")
    source.crop(crop).save(directory / "source-edge.png")
    panel = Image.new("RGB", (960, 296), "#20252b")
    draw = ImageDraw.Draw(panel)
    for i, name in enumerate(("source", "basic", "advanced")):
        panel.paste(tile(visual[name].crop(crop)), (320*i, 36))
        draw.text((320*i+12, 9), f"{name.title()} / fixed edge crop", fill="white", font=font())
    panel.save(directory / "edge-comparison.png")
    return visual


def main(run):
    run = Path(run).resolve()
    evidence = run.parent
    criteria_path = Path(__file__).with_name("quality_criteria.json")
    criteria = json.loads(criteria_path.read_text(encoding="utf-8"))
    manifest = json.loads((evidence / "prepared-manifest.json").read_text(encoding="utf-8"))
    provenance = json.loads((run / "provenance.json").read_text(encoding="utf-8-sig"))
    if digest(criteria_path) != manifest["criteria_sha256"] or provenance["criteria_sha256"] != manifest["criteria_sha256"]:
        raise RuntimeError("Criteria freeze mismatch")
    sources = {s["id"]: s for s in json.loads((evidence / "sources.json").read_text(encoding="utf-8-sig"))}
    sheet = Image.new("RGB", (1280, 328*len(manifest["cases"])+56), "#151a20")
    draw = ImageDraw.Draw(sheet)
    draw.text((14, 14), "Foreground quality v1 | Native CPU | Basic / Advanced defaults | Human acceptance pending", fill="white", font=font(21))
    results, sections = [], []
    for i, case in enumerate(manifest["cases"]):
        source_path = evidence / case["input_file"]
        if digest(source_path) != case["input_sha256"]:
            raise RuntimeError("Prepared input hash mismatch")
        source = Image.open(source_path).convert("RGB")
        directory = run / case["id"]
        native = json.loads((directory / "native.json").read_text(encoding="utf-8"))
        row = dict(id=case["id"], category=case["category"], reference_status="published_cutout_agreement_only" if case["reference"] else "qualitative_only", native=native)
        visual = save_visuals(source, directory, case["crop_pixels"], native["status"])
        if native["status"] == "produced_masks":
            basic, advanced = alpha(directory / "basic-mask.png"), alpha(directory / "advanced-mask.png")
            if basic.shape != (case["height"], case["width"]) or advanced.shape != basic.shape:
                raise RuntimeError("Native mask dimensions differ")
            row["basic_advanced_mae"] = float(np.abs(basic-advanced).mean())
            for mode, mask in (("basic", basic), ("advanced", advanced)):
                row[mode] = observable(mask)
                row[mode]["reference_metrics"] = None
                row[mode]["reference_metrics_reason"] = "No independent reference alpha; qualitative-only"
                if case["reference"]:
                    ref_path = evidence / case["reference_file"]
                    if digest(ref_path) != case["reference_sha256"]:
                        raise RuntimeError("Reference changed")
                    row[mode]["reference_metrics"] = reference_metrics(mask, alpha(ref_path), criteria["published_reference_screening_thresholds"])
                    row[mode]["reference_metrics_reason"] = "Published channel agreement only; unknown original extraction method and physical accuracy"
        else:
            row.update(basic=None, advanced=None, basic_advanced_mae=None, metrics_reason="Provider returned no subject and no alpha mask; do not fabricate a zero mask")
        results.append(row)
        top = 56+328*i
        draw.text((12, top+8), f"{i+1}. {case['category']} | {row['reference_status']}", fill="#b8dafe", font=font(18))
        for j, name in enumerate(("source", "basic", "advanced")):
            sheet.paste(tile(visual[name]), (j*320, top+56))
            draw.text((j*320+12, top+34), name.title(), fill="white", font=font(16))
        edge = Image.open(directory / "edge-comparison.png")
        sheet.paste(tile(edge, (320,260)), (960,top+56))
        draw.text((972,top+34), "Fixed edge crops", fill="white", font=font(16))
        src = sources[case["source"]]
        cards = ''.join(f'<figure><a href="{case["id"]}/{mode}-checker.png"><img src="{case["id"]}/{mode}-checker.png" alt="{mode} result"></a><figcaption>{mode.title()} <a href="{case["id"]}/{mode}-mask.png">alpha</a> · <a href="{case["id"]}/{mode}-white.png">white</a> · <a href="{case["id"]}/{mode}-dark.png">dark</a> · <a href="{case["id"]}/{mode}-cutout.png">RGBA</a></figcaption></figure>' for mode in ("basic","advanced")) if native["status"] == "produced_masks" else '<p class="warning">Provider reported no foreground subject. No alpha file was returned.</p>'
        sections.append(f'<section id="{case["id"]}"><h2>{html.escape(case["category"])}</h2><p class="warning">{html.escape(row["reference_status"])}</p><div class="triptych"><figure><a href="../{case["input_file"]}"><img src="../{case["input_file"]}" alt="source"></a><figcaption>Prepared source</figcaption></figure>{cards}</div><a href="{case["id"]}/edge-comparison.png"><img class="edges" src="{case["id"]}/edge-comparison.png" alt="Source, Basic and Advanced fixed edge crops"></a><p>Source: <a href="{html.escape(src["page"])}">{html.escape(src["id"])}</a>. {html.escape(src["license"])}. <a href="../{src["metadata"]}">Retained license metadata</a>.</p><details><summary>Measurements and limitations</summary><pre>{html.escape(json.dumps(row,indent=2))}</pre></details></section>')
    payload = {"criteria": criteria["id"], "criteria_sha256": manifest["criteria_sha256"], "overall_status": "human_acceptance_pending", "scientific_ground_truth_cases": 0, "published_alpha_reference_cases": 1, "qualitative_only_cases": 7, "cases": results}
    (run / "results.json").write_text(json.dumps(payload, indent=2)+"\n", encoding="utf-8")
    sheet.save(run / "contact-sheet.png")
    body = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Foreground quality review v1</title><style>body{margin:0 auto;max-width:1440px;padding:30px;background:#151a20;color:#e8edf3;font:16px/1.5 system-ui}h1,h2{line-height:1.2}a{color:#9dd3ff}section{padding:24px 0;border-top:1px solid #46515e}.triptych{display:grid;grid-template-columns:repeat(3,1fr);gap:14px}figure{margin:0}figure img{width:100%;height:330px;object-fit:contain;background:#20252b}.edges{max-width:100%;width:960px}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#20252b;padding:16px}.warning{color:#ffd49b}.intro{max-width:1050px}.links{padding:12px;background:#20252b}@media(max-width:700px){.triptych{grid-template-columns:1fr}body{padding:14px}}</style><h1>Foreground quality review v1</h1><div class="intro"><p class="warning">Experimental replacement. Human acceptance pending. Seven photographs have no reference alpha; their accuracy metrics are unavailable. One independently published cutout supplies alpha-agreement scores, with unknown physical accuracy. This is not macOS Vision parity evidence.</p><p>All candidates use the pinned native CPU model. Basic retains the base mask. Advanced uses Refine Edges 12, Shift Edge 0, Contrast 25 in source order, on the full prepared image. Criteria, inputs and edge crops were frozen before inference. No settings were tuned per case. NASA photographs appear only as factual evaluation illustrations; NASA neither reviewed nor endorsed this model, and the predicted outputs are produced by this experimental application.</p></div><p class="links"><a href="contact-sheet.png">Contact sheet</a> · <a href="results.json">Per-image metrics</a> · <a href="provenance.json">Run provenance</a> · <a href="../prepared-manifest.json">Pinned fixtures</a> · <a href="../LICENSES.md">Image permissions</a></p>'''
    (run / "report.html").write_text(body+''.join(sections)+"</html>\n", encoding="utf-8")
    print(json.dumps({"cases":len(results), "report":str(run / "report.html"), "status":payload["overall_status"]}))


if __name__ == "__main__":
    main(sys.argv[1])
