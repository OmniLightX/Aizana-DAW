#!/usr/bin/env python3
"""Add license/provenance metadata to the qualified ONNX export.

Developer conversion tool; not used by the app or its consumer build.
Requires the isolated evaluation environment's onnx package. The weights and
architecture retain their original MIT notices. No user audio is consumed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import onnx

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('input', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
expected = '1bcfad5784703e38ab03e19db52cee1f796406472a52f8ac44dcad00e2fc95db'
with args.input.open('rb') as stream:
    if hashlib.file_digest(stream, 'sha256').hexdigest() != expected:
        raise SystemExit('Input is not the qualified five-head export')
repository = Path(__file__).resolve().parents[1]
licenses = '\n\n'.join((repository/'docs/models'/name).read_text() for name in ['LICENSE.Mega53', 'LICENSE.BS-RoFormer'])
model = onnx.load(args.input)
# Exporter stack traces are irrelevant for inference and may contain build paths.
def strip_doc_strings(message):
    # Protobuf 7 removed FieldDescriptor.label, used by ONNX's older helper.
    for descriptor, value in list(message.ListFields()):
        if descriptor.name == 'doc_string':
            message.ClearField(descriptor.name)
        elif descriptor.message_type is not None:
            if descriptor.is_repeated:
                for child in value:
                    strip_doc_strings(child)
            else:
                strip_doc_strings(value)
strip_doc_strings(model)
graph_digest = hashlib.sha256(model.graph.SerializeToString()).hexdigest()
properties = {
    'model': 'MVSep Mega 53 Stems v1: selected drums/bass/vocal/guitar/piano heads',
    'license': 'MIT',
    'network_graph_sha256': graph_digest,
    'license_text': licenses,
    'weight_author': 'Roman Solovyev (ZFTurbo)',
    'weight_license_evidence': 'https://github.com/ZFTurbo/Music-Source-Separation-Training/issues/245',
    'weight_source': 'https://github.com/ZFTurbo/Music-Source-Separation-Training/releases/tag/v1.0.21',
    'source_checkpoint_sha256': 'c62820893bbf86d4e734f966bd142d9157cfc8bb8e79e9d8f9ea553f3ff3519f',
    'conversion': 'Five independent original heads selected without retraining; mask network exported with ONNX opset 17. STFT/ISTFT run outside the graph.',
    'other_output': 'Not in this graph: native host subtracts the five estimates from the input waveform.',
    'weight_warranty': 'Original trainer provides no training-data rights warranty or indemnification. See the linked original grant.',
}
onnx.helper.set_model_props(model, properties)
onnx.checker.check_model(model)
args.output.parent.mkdir(parents=True, exist_ok=True)
onnx.save(model, args.output)
with args.output.open('rb') as stream:
    digest = hashlib.file_digest(stream, 'sha256').hexdigest()
print(json.dumps({'name': args.output.name, 'bytes': args.output.stat().st_size, 'sha256': digest}, indent=2))
