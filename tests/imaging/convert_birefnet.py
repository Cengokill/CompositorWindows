"""Audited local-only conversion: no remote code fetch, pickle checkpoint, or CUDA.

Native ONNX DeformConv maps torchvision's documented argument order explicitly.
The fixed 1024 shape is intentional: tracing this Swin implementation at another
shape requires a new export and new numerical validation.
"""
import argparse, hashlib, importlib.util, json, os, sys, time, types
from pathlib import Path
os.environ['HF_HUB_OFFLINE']='1'
os.environ['TRANSFORMERS_OFFLINE']='1'
import numpy as np
from PIL import Image
import torch
from torch.onnx.symbolic_helper import parse_args, _get_tensor_sizes
from safetensors.torch import load_file
import onnx
import onnxruntime as ort

ROOT=Path(__file__).resolve().parents[2]
MODEL=ROOT/'dependencies/imaging/model'
OUT=ROOT/'evidence/imaging'
assert hashlib.sha256((MODEL/'model.safetensors').read_bytes()).hexdigest()=='4417d89795250e698c3cb0ae8df15743810065f646f48a694fdfa7ca052d0815'
assert hashlib.sha256((MODEL/'birefnet.py').read_bytes()).hexdigest()=='af8568b5be406bf4d2a68a7ed6d72e40f73b37a1fb6fc9ebd71b5b3cbcd069c9'
package=types.ModuleType('audited_birefnet');package.__path__=[str(MODEL)];sys.modules[package.__name__]=package
spec=importlib.util.spec_from_file_location('audited_birefnet.birefnet',MODEL/'birefnet.py')
module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)

@parse_args('v','v','v','v','v','i','i','i','i','i','i','i','i','b')
def deform(g,x,weight,offset,mask,bias,sh,sw,ph,pw,dh,dw,group,offset_group,use_mask):
    args=[x,weight,offset,bias]
    if use_mask: args.append(mask)
    return g.op('DeformConv',*args,strides_i=[sh,sw],pads_i=[ph,pw,ph,pw],dilations_i=[dh,dw],group_i=group,offset_group_i=offset_group,kernel_shape_i=_get_tensor_sizes(weight)[-2:])
torch.onnx.register_custom_op_symbolic('torchvision::deform_conv2d',deform,20)

class LastMask(torch.nn.Module):
    def __init__(self,model):super().__init__();self.model=model
    def forward(self,x):return self.model(x)[-1].sigmoid()

def tensor(image):
    rgb=np.asarray(image.convert('RGB').resize((1024,1024),Image.Resampling.BILINEAR),dtype=np.float32)/255
    rgb=(rgb-np.array([.485,.456,.406],dtype=np.float32))/np.array([.229,.224,.225],dtype=np.float32)
    return torch.from_numpy(rgb.transpose(2,0,1).copy()[None])

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--skip-export',action='store_true');args=parser.parse_args()
    torch.set_num_threads(8);torch.manual_seed(7)
    started=time.perf_counter();model=module.BiRefNet(config=module.BiRefNetConfig(bb_pretrained=False))
    model.load_state_dict(load_file(str(MODEL/'model.safetensors')),strict=True);model=LastMask(model.eval()).eval()
    print('Loaded pinned official weights',time.perf_counter()-started,flush=True)
    source=Image.open(OUT/'astronaut.png');x=tensor(source)
    with torch.inference_mode():
        started=time.perf_counter();ref=model(x).numpy();torch_time=time.perf_counter()-started
        print('PyTorch CPU inference seconds',torch_time,flush=True)
        if not args.skip_export:
            started=time.perf_counter();torch.onnx.export(model,x,str(MODEL/'birefnet-lite.onnx'),opset_version=20,input_names=['image'],output_names=['mask'],dynamo=False,do_constant_folding=True)
            print('ONNX export seconds',time.perf_counter()-started,flush=True)
    onnx.checker.check_model(str(MODEL/'birefnet-lite.onnx'))
    options=ort.SessionOptions();options.intra_op_num_threads=8;options.inter_op_num_threads=1
    session=ort.InferenceSession(str(MODEL/'birefnet-lite.onnx'),sess_options=options,providers=['CPUExecutionProvider'])
    report={'model_repo':'ZhengPeng7/BiRefNet_lite','revision':'aa62cd87eafb9cc43056d08ef3615a14628b831d','providers':session.get_providers(),'torch_cpu_seconds':torch_time,'opset':20,'cases':[],'acceptance':'experimental; no Vision references or annotated alpha ground truth'}
    for name in ['astronaut','chelsea']:
        image=Image.open(OUT/(name+'.png')).convert('RGB');inp=tensor(image).numpy()
        started=time.perf_counter();pred=session.run(['mask'],{'image':inp})[0];elapsed=time.perf_counter()-started
        if name=='astronaut':
            error=np.abs(pred-ref);report['conversion_max_abs']=float(error.max());report['conversion_mean_abs']=float(error.mean());report['conversion_gate']={'max_abs':.002,'mean_abs':.00002,'passed':bool(error.max()<=.002 and error.mean()<=.00002)}
        assert np.isfinite(pred).all()
        mask=Image.fromarray(np.clip(pred[0,0]*255+.5,0,255).astype('uint8')).resize(image.size,Image.Resampling.BILINEAR)
        mask.save(OUT/(name+'-mask.png'));rgba=image.convert('RGBA');rgba.putalpha(mask);rgba.save(OUT/(name+'-cutout.png'))
        report['cases'].append({'name':name,'width':image.width,'height':image.height,'cpu_seconds':elapsed,'foreground_fraction':float((pred>.5).mean()),'soft_fraction':float(((pred>.05)&(pred<.95)).mean())})
        print(name,report['cases'][-1],flush=True)
    report['onnx_sha256']=hashlib.sha256((MODEL/'birefnet-lite.onnx').read_bytes()).hexdigest()
    (OUT/'model-results.json').write_text(json.dumps(report,indent=2))
    if not report['conversion_gate']['passed']:raise SystemExit('ONNX conversion numerical gate failed')
