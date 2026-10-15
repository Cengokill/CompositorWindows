"""Instrument scalar reads in the real Document.cpp for mask contracts only."""

import argparse
import hashlib
import json
from pathlib import Path


SOURCES = (
    "src/core/Document.cpp", "src/core/Document.h",
    "src/graphics/GrowingBrushSession.cpp", "src/graphics/GrowingBrushSession.h",
    "src/graphics/SamplingSource.cpp", "src/graphics/SamplingSource.h",
    "src/graphics/BrushCoverage.cpp", "src/graphics/BrushCoverage.h",
    "src/graphics/ParallelBatch.cpp", "src/graphics/ParallelBatch.h",
    "tests/mask_composition44/observe-document.py",
)
READS = (
    ("Pixel Raster::pixel(int x,int y)const{", "brushCompositionPixelRead"),
    ("uint8_t GrayRaster::pixel(int x,int y,uint8_t exterior)const{", "brushCompositionGrayRead"),
)


def generate(root: Path, output: Path, manifest: Path) -> None:
    inputs = {name: (root / name).read_bytes() for name in SOURCES}
    source = inputs["src/core/Document.cpp"].decode("utf-8")
    # Refuse to produce an unobserved executable if production signatures change.
    for signature, observer in READS:
        if source.count(signature) != 1:
            raise ValueError(f"Expected exactly one scalar read definition: {signature}")
        source = source.replace(signature, signature + observer + "();", 1)
    declarations = "namespace compositor {\n" + "".join(
        f"void {observer}();\n" for _, observer in READS
    ) + "}\n"
    observed = (declarations + source).encode("utf-8")
    data = {
        "schema": "MASK_COMPOSITION_OBSERVER_V1",
        "sources": [{"path": name, "sha256": hashlib.sha256(content).hexdigest()}
                    for name, content in inputs.items()],
        "observed_cpp_sha256": hashlib.sha256(observed).hexdigest(),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(observed)
    manifest.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output-cpp", required=True, type=Path)
    parser.add_argument("--output-manifest", required=True, type=Path)
    args = parser.parse_args()
    generate(args.source_root, args.output_cpp, args.output_manifest)
