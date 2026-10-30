"""Generate the MIT-licensed synthetic CMYK test profile. Not a printing profile.

The idealized subtractive model reduces RGB saturation by 20 percent. ICC v2
LUT16 tags describe both directions, with D50 XYZ as their connection space.
Only Python's standard library is required; production never runs this script.
"""
import base64
import itertools
import struct
from pathlib import Path

def u16(n): return struct.pack('>H', max(0, min(65535, round(n))))
def u32(n): return struct.pack('>I', n)
def fixed(n): return struct.pack('>i', round(n*65536))
def linear(x): return x/12.92 if x<=0.04045 else ((x+0.055)/1.055)**2.4
def gamma(x): return 12.92*x if x<=0.0031308 else 1.055*x**(1/2.4)-0.055
def mul(m,v): return [sum(a*b for a,b in zip(row,v)) for row in m]
M=((.4360747,.3850649,.1430804),(.2225045,.7168786,.0606169),(.0139322,.0971045,.7141733))
I=((3.1338561,-1.6168667,-.4906146),(-.9787684,1.9161415,.0334540),(.0719453,-.2289914,1.4052427))
def forward(v):
    c,m,y,k=v;rgb=[(1-q)*(1-k) for q in (c,m,y)];mean=sum(rgb)/3
    return [x/2 for x in mul(M,[linear(.8*q+.2*mean) for q in rgb])]
def reverse(v):
    rgb=[gamma(max(0,x)) for x in mul(I,[2*q for q in v])];mean=sum(rgb)/3
    rgb=[max(0,min(1,(q-.2*mean)/.8)) for q in rgb];k=1-max(rgb)
    return [0 if k>=1 else max(0,min(1,(1-q-k)/(1-k))) for q in rgb]+[k]
def lut(inputs,outputs,grid,fn):
    out=b'mft2'+bytes(4)+bytes((inputs,outputs,grid,0))
    out+=b''.join(fixed(float(i%4==0)) for i in range(9))+u16(2)+u16(2)
    out+=(u16(0)+u16(65535))*inputs
    for p in itertools.product(range(grid),repeat=inputs):out+=b''.join(u16(x*65535) for x in fn([i/(grid-1) for i in p]))
    return out+(u16(0)+u16(65535))*outputs
name=b'Compositor Synthetic CMYK\0'
desc=b'desc'+bytes(4)+u32(len(name))+name+bytes(4+4+2+1+67)
tags=[(b'desc',desc),(b'cprt',b'text'+bytes(4)+b'MIT; CompositorWindows test fixture\0'),(b'wtpt',b'XYZ '+bytes(4)+b''.join(fixed(x) for x in (.9642,1,.8249))),(b'A2B0',lut(4,3,9,forward)),(b'B2A0',lut(3,4,17,reverse))]
header=bytearray(128);header[8:12]=bytes((2,16,0,0));header[12:24]=b'prtrCMYKXYZ ';header[24:36]=struct.pack('>6H',2026,1,1,0,0,0);header[36:40]=b'acsp';header[40:44]=b'MSFT';header[68:80]=b''.join(fixed(x) for x in (.9642,1,.8249))
offset=132+12*len(tags);table=b'';payload=b''
for sig,data in tags:
    table+=sig+u32(offset)+u32(len(data));padded=data+bytes((-len(data))%4);payload+=padded;offset+=len(padded)
header[:4]=u32(offset)
Path(__file__).with_name('synthetic-cmyk.icc.base64').write_bytes(base64.encodebytes(header+u32(len(tags))+table+payload))
