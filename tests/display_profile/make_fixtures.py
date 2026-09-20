"""Create original ICC v2 test data; no downloaded or system profiles copied."""
from pathlib import Path
import struct
import hashlib
import json

ROOT = Path(__file__).resolve().parent / "fixtures"
ROOT.mkdir(exist_ok=True)
be = lambda n: struct.pack(">I", n)
fixed = lambda x: struct.pack(">i", round(x * 65536))
xyz = lambda v: b"XYZ " + bytes(4) + b"".join(fixed(n) for n in v)
description = b"Original linear RGB matrix test profile\0"
tags = {
    b"desc": b"desc" + bytes(4) + be(len(description)) + description + bytes(4 + 4 + 2 + 1 + 67),
    b"cprt": b"text" + bytes(4) + b"Original generated test data, CC0-1.0\0",
    b"wtpt": xyz((0.9642, 1, 0.8249)),
    b"rXYZ": xyz((0.4360747, 0.2225045, 0.0139322)),
    b"gXYZ": xyz((0.3850649, 0.7168786, 0.0971045)),
    b"bXYZ": xyz((0.1430804, 0.0606169, 0.7141733)),
    b"rTRC": b"curv" + bytes(4) + be(1) + struct.pack(">H", 256),
    b"gTRC": b"curv" + bytes(4) + be(1) + struct.pack(">H", 256),
    b"bTRC": b"curv" + bytes(4) + be(1) + struct.pack(">H", 256),
}
header = bytearray(128)
header[8:12] = be(0x02100000)
header[12:24] = b"mntrRGB XYZ "
header[24:36] = struct.pack(">6H", 2026, 9, 19, 0, 0, 0)
header[36:40] = b"acsp"
header[40:44] = b"MSFT"
header[64:68] = be(1)
header[68:80] = b"".join(fixed(n) for n in (.9642, 1, .8249))
table = be(len(tags)); payload = bytes(); offset = 132 + len(tags) * 12
for signature, data in tags.items():
    table += signature + be(offset + len(payload)) + be(len(data))
    payload += data + bytes((-len(data)) % 4)
profile = header + table + payload
profile[:4] = be(len(profile))
path = ROOT / "linear-rgb.icc"
path.write_bytes(profile)
(ROOT / "manifest.json").write_text(json.dumps({
    "generator": "tests/display_profile/make_fixtures.py",
    "license": "CC0-1.0: original profile bytes generated for this test; no external profile copied",
    "profile": path.name, "sha256": hashlib.sha256(profile).hexdigest(),
    "intent": "D50-adapted sRGB primary matrix with gamma 1.0 tone curves",
    "oracle": "Neutral sRGB byte v maps to round(255*((v/255+0.055)/1.055)^2.4) above 0.04045; below it maps to round(v/12.92)",
    "fixed_tolerance_bytes": 2,
    "scope": "Original synthetic RGB ICC fixture; physical monitor calibration remains external acceptance"
}, indent=2) + "\n")
