#!/usr/bin/env python3
"""Synthesises a one-bar 4/4 drum loop (kick, snare, hats) as a 24-bit stereo WAV.
Stdlib only. Handy for trying the chopper without real audio:

    python3 tools/make_demo_loop.py demo_loop.wav --bpm 92
    ./build/cli/chopper --in demo_loop.wav --dry-run
"""
import argparse, math, random, struct, wave

def kick(sr, t):      # pitch-sweeping sine
    f = 40 + 110 * math.exp(-t * 40)
    return math.sin(2 * math.pi * f * t) * math.exp(-t * 9) * 0.9

def snare(sr, t, rng):
    tone = math.sin(2 * math.pi * 190 * t) * math.exp(-t * 25) * 0.4
    noise = rng.uniform(-1, 1) * math.exp(-t * 18) * 0.5
    return tone + noise

def hat(sr, t, rng, open_=False):
    return rng.uniform(-1, 1) * math.exp(-t * (6 if open_ else 60)) * 0.25

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--bpm", type=float, default=92.0)
    ap.add_argument("--sr", type=int, default=44100)
    ap.add_argument("--bars", type=int, default=1)
    args = ap.parse_args()
    sr, bpm = args.sr, args.bpm
    beat = 60.0 / bpm
    total = int(round(sr * beat * 4 * args.bars))
    rng = random.Random(1)
    left = [0.0] * total; right = [0.0] * total

    # 16th-note grid; k = kick, s = snare, h = hat, o = open hat, . = rest (per 16th)
    pattern = "k.hhs.h.k.hkS.hO"
    hits = []
    for bar in range(args.bars):
        for i, ch in enumerate(pattern):
            t0 = (bar * 16 + i) * beat / 4
            if ch != ".": hits.append((t0, ch))
    hp_prev = 0.0
    for t0, ch in hits:
        start = int(t0 * sr)
        length = int(sr * 0.6)
        for n in range(length):
            i = start + n
            if i >= total: break
            t = n / sr
            if ch == "k": v = kick(sr, t); pan = 0.0
            elif ch in "sS": v = snare(sr, t, rng) * (0.8 if ch == "s" else 1.0); pan = 0.1
            elif ch == "h": v = hat(sr, t, rng); pan = -0.3
            else: v = hat(sr, t, rng, True); pan = -0.3
            if t > 0.6: break
            left[i] += v * (1 - max(0, pan)); right[i] += v * (1 + min(0, pan))
    peak = max(max(map(abs, left)), max(map(abs, right)), 1e-9)
    g = 0.89 / peak
    with wave.open(args.out, "wb") as w:
        w.setnchannels(2); w.setsampwidth(3); w.setframerate(sr)
        frames = bytearray()
        for l, r in zip(left, right):
            for v in (l * g, r * g):
                s = max(-8388608, min(8388607, int(round(v * 8388607))))
                frames += struct.pack("<i", s)[:3]
        w.writeframes(bytes(frames))
    print(f"wrote {args.out}: {args.bars} bar(s) at {bpm} bpm, {len(hits)} hits, {total / sr:.3f} s")

if __name__ == "__main__":
    main()
