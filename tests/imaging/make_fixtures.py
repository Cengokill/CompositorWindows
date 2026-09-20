"""Generate synthetic metadata/format fixtures; no downloaded image processing code."""
from pathlib import Path
import struct,zlib,hashlib,json
from PIL import Image,ImageOps,ImageCms
OUT=Path(__file__).resolve().parents[2]/'evidence/imaging'
source=Image.new('RGB',(3,2));source.putdata([(255,0,0),(0,255,0),(0,0,255),(255,255,0),(0,255,255),(255,0,255)])
for o in range(1,9):
    exif=Image.Exif();exif[274]=o
    source.save(OUT/f'orientation-{o}.tif',tiffinfo=exif)
    with Image.open(OUT/f'orientation-{o}.tif') as image:
        # Pillow TIFF loader already applies orientation; use the coordinate definition explicitly.
        methods={2:Image.Transpose.FLIP_LEFT_RIGHT,3:Image.Transpose.ROTATE_180,4:Image.Transpose.FLIP_TOP_BOTTOM,5:Image.Transpose.TRANSPOSE,6:Image.Transpose.ROTATE_270,7:Image.Transpose.TRANSVERSE,8:Image.Transpose.ROTATE_90}
        expected=source.transpose(methods[o]) if o!=1 else source
        expected.save(OUT/f'oriented-{o}.png')
exif=Image.Exif();exif[274]=6
source.save(OUT/'orientation-6.jpg',exif=exif)
Image.new('I;16',(2,2),40000).save(OUT/'rgba16.png')
Image.new('L',(2,2),128).save(OUT/'transparent-gray.png',transparency=128)
source.save(OUT/'animated.png',save_all=True,append_images=[source.transpose(Image.Transpose.FLIP_LEFT_RIGHT)],duration=100,loop=0)
adobe=OUT/'AdobeCompat-v2.icc'
if adobe.exists():
    original=Image.new('RGB',(4,2));original.putdata([(180,90,80),(20,120,80),(70,60,150),(200,120,10)]*2)
    original.save(OUT/'adobe-rgb.png',icc_profile=adobe.read_bytes())
    target=ImageCms.createProfile('sRGB');converted=ImageCms.profileToProfile(original,str(adobe),target,renderingIntent=0,outputMode='RGB')
    converted.save(OUT/'adobe-rgb-srgb.png')
required=[f'orientation-{o}.tif' for o in range(1,9)]+[f'oriented-{o}.png' for o in range(1,9)]+['orientation-6.jpg','rgba16.png','transparent-gray.png','animated.png','adobe-rgb.png','adobe-rgb-srgb.png']
manifest=[]
for name in required:
    path=OUT/name
    if not path.is_file():raise RuntimeError('Required fixture missing: '+name)
    manifest.append({'file':name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size})
(OUT/'fixture-manifest.json').write_text(json.dumps({'required':manifest},indent=2))
