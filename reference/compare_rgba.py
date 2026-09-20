"""Compare reference/export raw RGBA8 files using a predeclared absolute byte tolerance."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

def main():
    p=argparse.ArgumentParser()
    p.add_argument("reference",type=Path); p.add_argument("actual",type=Path)
    p.add_argument("--width",required=True,type=int); p.add_argument("--height",required=True,type=int)
    p.add_argument("--max-channel-error",default=0,type=int)
    p.add_argument("--tolerance-rationale",default="Exact deterministic RGBA8 equality")
    p.add_argument("--output",required=True,type=Path)
    a=p.parse_args()
    if min(a.width,a.height)<=0 or not 0<=a.max_channel_error<=255: p.error("Invalid dimensions or threshold")
    if a.max_channel_error and a.tolerance_rationale=="Exact deterministic RGBA8 equality": p.error("A nonzero threshold requires an operation-specific rationale set before comparison")
    result={"reference":str(a.reference),"actual":str(a.actual),"width":a.width,"height":a.height,
            "max_allowed_channel_error":a.max_channel_error,"tolerance_rationale":a.tolerance_rationale}
    if not a.reference.is_file(): result.update(result="blocked_reference")
    elif not a.actual.is_file(): result.update(result="blocked_implementation")
    else:
        expected=a.reference.read_bytes(); actual=a.actual.read_bytes()
        result.update(reference_sha256=hashlib.sha256(expected).hexdigest(),actual_sha256=hashlib.sha256(actual).hexdigest())
        if len(expected)!=a.width*a.height*4 or len(actual)!=len(expected): result.update(result="fail",reason="Buffer size mismatch")
        else:
            maximum=0; different=0; exceeded=0; first=None
            for i,(x,y) in enumerate(zip(expected,actual)):
                d=abs(x-y); maximum=max(maximum,d)
                different+=d!=0; exceeded+=d>a.max_channel_error
                if first is None and d>a.max_channel_error: first={"x":(i//4)%a.width,"y":(i//4)//a.width,"channel":"RGBA"[i%4],"reference":x,"actual":y}
            result.update(result="pass" if not exceeded else "fail",max_channel_error=maximum,different_channels=different,exceeded_channels=exceeded,first_exceeded=first)
    a.output.parent.mkdir(parents=True,exist_ok=True); a.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result)); return 0 if result["result"]=="pass" else 2

if __name__=="__main__": sys.exit(main())
