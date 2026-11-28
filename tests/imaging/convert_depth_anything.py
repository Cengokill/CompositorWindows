"""Audited local-only conversion of Depth Anything V2 Small to ONNX.

No remote code, pickle checkpoint, or CUDA is used. The export keeps height and
width dynamic (multiples of 14); every aspect ratio the application uses is
compared against PyTorch before the file is accepted. Weights are stored as
float16 and cast to float32 at load, so inference still runs in float32 on the
ONNX Runtime CPU provider.
"""
import argparse, hashlib, json, os, time
from pathlib import Path
os.environ['HF_HUB_OFFLINE'] = '1'
os.environ['TRANSFORMERS_OFFLINE'] = '1'
import numpy as np
from PIL import Image
import torch
import onnx
from onnx import helper, numpy_helper, TensorProto
import onnxruntime as ort
from safetensors.torch import load_file
from transformers import DepthAnythingConfig, DepthAnythingForDepthEstimation
from transformers.models.depth_anything import modeling_depth_anything as dam
ROOT = Path(__file__).resolve().parents[2]
MODEL = ROOT / 'dependencies/imaging/depth-model'
OUT = ROOT / 'evidence/imaging'
FP32 = MODEL / 'depth-anything-v2-small-fp32.onnx'
FINAL = MODEL / 'depth-anything-v2-small.onnx'
REVISION = '5426e4f0f36572d16453bbda7a8389317b1bef99'
assert hashlib.sha256((MODEL / 'model.safetensors').read_bytes()).hexdigest() == \
    '3152477ce0d8d6978d76b995120de97cb5b928701fd0f817769f59e249a16b70'
assert hashlib.sha256((MODEL / 'config.json').read_bytes()).hexdigest() == \
    'c56698d3643dde1f83ea2212759e6b31a22b8f827246a36dd007ee8a22b3ff75'

# Long edge 644 = 46 * 14, the multiple of 14 nearest to 640 px.
SHAPES = [(644, 644), (490, 644), (644, 490), (364, 644), (644, 364)]


class Depth(torch.nn.Module):
    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, x):
        return self.model(pixel_values=x).predicted_depth.unsqueeze(1)


def traceable_fusion(self, hidden_state, residual=None, size=None):
    # The upstream shape comparison is frozen by the tracer. Resizing to the same
    # size with align_corners=False is an exact identity, so always resize.
    if residual is not None:
        residual = torch.nn.functional.interpolate(
            residual, size=(hidden_state.shape[2], hidden_state.shape[3]), mode='bilinear', align_corners=False)
        hidden_state = hidden_state + self.residual_layer1(residual)
    hidden_state = self.residual_layer2(hidden_state)
    modifier = {'scale_factor': 2} if size is None else {'size': size}
    hidden_state = torch.nn.functional.interpolate(hidden_state, **modifier, mode='bilinear', align_corners=True)
    return self.projection(hidden_state)


def traceable_head(self, hidden_states, patch_height, patch_width):
    # Same computation as upstream without int(), which the tracer would freeze.
    hidden_states = hidden_states[self.head_in_index]
    predicted_depth = self.conv1(hidden_states)
    predicted_depth = torch.nn.functional.interpolate(
        predicted_depth, (patch_height * self.patch_size, patch_width * self.patch_size), mode='bilinear', align_corners=True)
    predicted_depth = self.conv2(predicted_depth)
    predicted_depth = self.activation1(predicted_depth)
    predicted_depth = self.conv3(predicted_depth)
    predicted_depth = self.activation2(predicted_depth) * self.max_depth
    return predicted_depth.squeeze(dim=1)


def tensor(image, height, width):
    rgb = np.asarray(image.convert('RGB').resize((width, height), Image.Resampling.BILINEAR), dtype=np.float32) / 255
    rgb = (rgb - np.array([.485, .456, .406], dtype=np.float32)) / np.array([.229, .224, .225], dtype=np.float32)
    return rgb.transpose(2, 0, 1).copy()[None]


def fp16_weights(source, target):
    """Store large float initializers as float16 followed by a Cast to float32."""
    model = onnx.load(str(source))
    graph = model.graph
    kept, casts = [], []
    for init in graph.initializer:
        if init.data_type == TensorProto.FLOAT and int(np.prod(init.dims)) >= 64:
            half = numpy_helper.from_array(numpy_helper.to_array(init).astype(np.float16), init.name + '_fp16')
            kept.append(half)
            casts.append(helper.make_node('Cast', [half.name], [init.name], to=TensorProto.FLOAT, name=init.name + '_cast'))
        else:
            kept.append(init)
    del graph.initializer[:]
    graph.initializer.extend(kept)
    nodes = list(graph.node)
    del graph.node[:]
    graph.node.extend(casts + nodes)
    onnx.checker.check_model(model)
    onnx.save(model, str(target))


def session(path):
    options = ort.SessionOptions()
    options.intra_op_num_threads = 8
    options.inter_op_num_threads = 1
    return ort.InferenceSession(str(path), sess_options=options, providers=['CPUExecutionProvider'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-export', action='store_true')
    args = parser.parse_args()
    torch.set_num_threads(8)
    config = DepthAnythingConfig.from_json_file(str(MODEL / 'config.json'))
    model = DepthAnythingForDepthEstimation(config)
    model.load_state_dict(load_file(str(MODEL / 'model.safetensors')), strict=True)
    model = Depth(model.eval()).eval()
    source = Image.open(OUT / 'astronaut.png')
    if not args.skip_export:
        x = torch.from_numpy(tensor(source, 644, 644))
        originals = dam.DepthAnythingFeatureFusionLayer.forward, dam.DepthAnythingDepthEstimationHead.forward
        dam.DepthAnythingFeatureFusionLayer.forward, dam.DepthAnythingDepthEstimationHead.forward = traceable_fusion, traceable_head
        with torch.inference_mode():
            torch.onnx.export(model, x, str(FP32), opset_version=20, input_names=['image'], output_names=['depth'],
                              dynamic_axes={'image': {2: 'height', 3: 'width'}, 'depth': {2: 'height', 3: 'width'}},
                              dynamo=False, do_constant_folding=True)
        # PyTorch references below use the unmodified upstream code.
        dam.DepthAnythingFeatureFusionLayer.forward, dam.DepthAnythingDepthEstimationHead.forward = originals
        fp16_weights(FP32, FINAL)
    onnx.checker.check_model(str(FINAL))
    sessions = {'fp32': session(FP32), 'fp16_weights': session(FINAL)}
    report = {'model_repo': 'depth-anything/Depth-Anything-V2-Small-hf', 'revision': REVISION, 'opset': 20,
              'sizes': {k: p.stat().st_size for k, p in (('fp32', FP32), ('fp16_weights', FINAL))}, 'cases': []}
    passed = True
    for height, width in SHAPES:
        inp = tensor(source, height, width)
        with torch.inference_mode():
            started = time.perf_counter(); ref = model(torch.from_numpy(inp)).numpy(); torch_time = time.perf_counter() - started
        span = float(ref.max() - ref.min())
        case = {'height': height, 'width': width, 'torch_seconds': torch_time, 'range': span}
        for name, s in sessions.items():
            s.run(['depth'], {'image': inp})
            started = time.perf_counter(); pred = s.run(['depth'], {'image': inp})[0]; elapsed = time.perf_counter() - started
            assert pred.shape == ref.shape, (pred.shape, ref.shape)
            assert np.isfinite(pred).all()
            error = np.abs(pred - ref) / span
            gate = .002 if name == 'fp32' else .01
            ok = bool(error.max() <= gate)
            passed &= ok
            case[name] = {'seconds': elapsed, 'max_rel': float(error.max()), 'mean_rel': float(error.mean()), 'gate': gate, 'passed': ok}
        report['cases'].append(case)
        print(case, flush=True)
    report['onnx_sha256'] = hashlib.sha256(FINAL.read_bytes()).hexdigest()
    report['passed'] = passed
    (OUT / 'depth-model-results.json').write_text(json.dumps(report, indent=2))
    print('sha256', report['onnx_sha256'], 'sizes', report['sizes'])
    if not passed:
        raise SystemExit('ONNX depth conversion numerical gate failed')
