#!/usr/bin/env python3
"""Deterministic generator for the YSE-authored (CC0) seed of the content pack.

Everything this script emits is authored from scratch here — no third-party
sample data is read or embedded — so the outputs are unambiguously CC0 and are
committed alongside this generator as their own provenance (issue #179).

Re-run to regenerate byte-for-byte:  python content/tools/generate_content.py

Outputs (relative to the repo's content/ directory):
  wavetables/ysewt_{sine,saw,square,triangle}.wav  single-cycle tables (600 smp)
  sfz/samples/yse_pulse_c4.wav                      short one-shot for the SFZ
  sfz/yse_pulse.sfz                                 minimal SFZ v1-subset map
  fm/original/yse_originals.syx                      32-voice DX7 packed bank

Third-party collections (AKWF, VSCO2/VCSL, Salamander, FreePats, DX7 factory)
are NOT produced here — they are pulled by the opt-in CMake fetch path
(cmake/YseContentPack.cmake + content/pack-manifest.cmake) and never committed.
"""

import math
import os
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
CONTENT = os.path.dirname(HERE)
SR = 44100


def _write_wav_mono16(path, samples):
    """Write a mono 16-bit PCM WAV from floats in [-1, 1]."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    frames = bytearray()
    for s in samples:
        v = int(max(-1.0, min(1.0, s)) * 32767.0)
        frames += struct.pack("<h", v)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(bytes(frames))
    print("wrote", os.path.relpath(path, CONTENT))


# ── single-cycle wavetables ───────────────────────────────────────────────────
def gen_wavetables():
    n = 600  # AKWF-style single-cycle length
    two_pi = 2.0 * math.pi
    tables = {
        "sine": [math.sin(two_pi * i / n) for i in range(n)],
        "saw": [2.0 * (i / n) - 1.0 for i in range(n)],
        "square": [1.0 if i < n // 2 else -1.0 for i in range(n)],
        "triangle": [
            (4.0 * (i / n) - 1.0) if i < n // 2 else (3.0 - 4.0 * (i / n))
            for i in range(n)
        ],
    }
    for name, data in tables.items():
        _write_wav_mono16(os.path.join(CONTENT, "wavetables", f"ysewt_{name}.wav"), data)


# ── SFZ one-shot sample + mapping ─────────────────────────────────────────────
def gen_sfz():
    # A short decaying sine at C4 (MIDI 60), the SFZ pitch_keycenter.
    freq = 440.0 * (2.0 ** ((60 - 69) / 12.0))
    dur = 0.35
    n = int(SR * dur)
    two_pi = 2.0 * math.pi
    samples = [
        math.sin(two_pi * freq * i / SR) * math.exp(-5.0 * i / n) for i in range(n)
    ]
    _write_wav_mono16(
        os.path.join(CONTENT, "sfz", "samples", "yse_pulse_c4.wav"), samples
    )
    sfz = (
        "// yse_pulse.sfz — original CC0 instrument (issue #179).\n"
        "// Uses only the SFZ v1 opcode subset from docs/design/sfz_opcode_subset.md.\n"
        "<region>\n"
        "sample=samples/yse_pulse_c4.wav\n"
        "lokey=0 hikey=127 pitch_keycenter=60\n"
        "ampeg_attack=0.001 ampeg_release=0.2\n"
    )
    path = os.path.join(CONTENT, "sfz", "yse_pulse.sfz")
    with open(path, "w", newline="\n") as f:
        f.write(sfz)
    print("wrote", os.path.relpath(path, CONTENT))


# ── original DX7 bank (32 voices) ─────────────────────────────────────────────
def _pack_voice(ops, glob, name):
    """Pack one voice into its 128-byte DX7 packed representation.

    Layout mirrors YseEngine/dsp/fm/dx7Sysex.cpp::unpackPacked (op*17 bytes per
    operator, then a 26-byte global block at offset 102).
    """
    out = bytearray(128)
    for op in range(6):
        o = ops[op]
        d = op * 17
        for i in range(4):
            out[d + i] = o["egRate"][i]
            out[d + 4 + i] = o["egLevel"][i]
        out[d + 8] = o["lsBreak"]
        out[d + 9] = o["lsLeftDepth"]
        out[d + 10] = o["lsRightDepth"]
        out[d + 11] = (o["lsLeftCurve"] & 0x03) | ((o["lsRightCurve"] & 0x03) << 2)
        out[d + 12] = (o["rateScaling"] & 0x07) | ((o["detune"] & 0x0F) << 3)
        out[d + 13] = (o["ampModSens"] & 0x03) | ((o["keyVelSens"] & 0x07) << 2)
        out[d + 14] = o["outputLevel"]
        out[d + 15] = (o["oscMode"] & 0x01) | ((o["freqCoarse"] & 0x1F) << 1)
        out[d + 16] = o["freqFine"]
    g = 102
    for i in range(4):
        out[g + i] = glob["pitchEgRate"][i]
        out[g + 4 + i] = glob["pitchEgLevel"][i]
    out[g + 8] = glob["algorithm"] & 0x1F
    out[g + 9] = (glob["feedback"] & 0x07) | ((glob["oscKeySync"] & 0x01) << 3)
    out[g + 10] = glob["lfoSpeed"]
    out[g + 11] = glob["lfoDelay"]
    out[g + 12] = glob["lfoPitchModDepth"]
    out[g + 13] = glob["lfoAmpModDepth"]
    out[g + 14] = (
        (glob["lfoSync"] & 0x01)
        | ((glob["lfoWaveform"] & 0x07) << 1)
        | ((glob["pitchModSens"] & 0x07) << 4)
    )
    out[g + 15] = glob["transpose"]
    nm = (name + " " * 10)[:10]
    for i in range(10):
        out[g + 16 + i] = ord(nm[i]) & 0x7F
    return out


def _op(output_level, coarse=1, egr=(99, 50, 35, 60), egl=(99, 90, 70, 0)):
    return {
        "egRate": list(egr),
        "egLevel": list(egl),
        "lsBreak": 39,
        "lsLeftDepth": 0,
        "lsRightDepth": 0,
        "lsLeftCurve": 0,
        "lsRightCurve": 0,
        "rateScaling": 0,
        "detune": 7,
        "ampModSens": 0,
        "keyVelSens": 2,
        "outputLevel": output_level,
        "oscMode": 0,
        "freqCoarse": coarse,
        "freqFine": 0,
    }


def _glob(algorithm, feedback=0, name_transpose=24):
    return {
        "pitchEgRate": [99, 99, 99, 99],
        "pitchEgLevel": [50, 50, 50, 50],
        "algorithm": algorithm,
        "feedback": feedback,
        "oscKeySync": 1,
        "lfoSpeed": 35,
        "lfoDelay": 0,
        "lfoPitchModDepth": 0,
        "lfoAmpModDepth": 0,
        "lfoSync": 1,
        "lfoWaveform": 0,
        "pitchModSens": 3,
        "transpose": name_transpose,
    }


def _voice_templates():
    # Four hand-authored timbres; the bank cycles through them with small
    # variations so all 32 slots carry a distinct, valid, named voice.
    templates = []
    # 0: pure carrier (algorithm 32 == index 31)
    templates.append((
        [_op(99)] + [_op(0) for _ in range(5)],
        _glob(31), "YSE SINE",
    ))
    # 1: 2-op FM (algorithm 5 == index 4): op1 carrier, op2 modulator
    templates.append((
        [_op(99), _op(75, coarse=2)] + [_op(0) for _ in range(4)],
        _glob(4, feedback=3), "YSE FM2OP",
    ))
    # 2: bell-ish (higher modulator ratio, faster decay)
    templates.append((
        [_op(99), _op(82, coarse=7, egl=(99, 60, 20, 0))] + [_op(0) for _ in range(4)],
        _glob(4, feedback=5), "YSE BELL",
    ))
    # 3: fuller stack using all six operators
    templates.append((
        [_op(96, coarse=1), _op(70, coarse=1), _op(60, coarse=2),
         _op(55, coarse=3), _op(50, coarse=1), _op(45, coarse=2)],
        _glob(21, feedback=6), "YSE BRASS",
    ))
    return templates


def gen_dx7_bank():
    templates = _voice_templates()
    payload = bytearray()
    for v in range(32):
        ops, glob, base = templates[v % len(templates)]
        name = f"{base} {v + 1:02d}"
        payload += _pack_voice(ops, glob, name)
    assert len(payload) == 4096, len(payload)

    # 7-bit two's-complement checksum over the payload (dx7Sysex.cpp).
    total = sum(payload) & 0x7F
    checksum = (0x80 - total) & 0x7F

    syx = bytearray()
    syx += bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00])  # 32-voice bulk header
    syx += payload
    syx += bytes([checksum, 0xF7])

    path = os.path.join(CONTENT, "fm", "original", "yse_originals.syx")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(syx)
    print("wrote", os.path.relpath(path, CONTENT), f"({len(syx)} bytes)")


if __name__ == "__main__":
    gen_wavetables()
    gen_sfz()
    gen_dx7_bank()
    print("done.")
