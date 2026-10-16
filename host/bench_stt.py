#!/usr/bin/env python3
"""STT benchmark for the audio shim's faster-whisper pipeline.

Generates a synthetic household-assistant clip corpus with the local Paradee
TTS (host/local_tts.py), then measures word error rate and per-clip latency
for candidate whisper models using the shim's decode settings
(compute_type int8, beam_size 1, vad_filter True, language en).

  # 1. build the corpus (needs ffmpeg; Paradee weights from host/models
  #    next to local_tts.py, or PARADEE_MODEL_DIR if that dir is absent)
  host/.venv-tts/bin/python host/bench_stt.py --make-clips /tmp/stt-clips

  # 2. benchmark (models are fetched into the default HF cache first)
  nice -n 19 host/.venv-tts/bin/python host/bench_stt.py \
      --clips /tmp/stt-clips --json /tmp/stt-bench.json

Run with the host/.venv-tts interpreter (faster-whisper, numpy). Each model
runs in a fresh child process so load time and RSS are measured cold.
"""

import argparse
import json
import math
import os
import statistics
import subprocess
import sys
import tempfile
import time
import wave

_HERE = os.path.dirname(os.path.abspath(__file__))

# Household-assistant utterances. Buckets are assigned from the measured
# clip duration (<= 5 s short, otherwise medium), not from this grouping.
SENTENCES = [
    # short prompts
    "What time is it right now?",
    "Turn it up a little.",
    "Turn the volume down.",
    "Stop talking please.",
    "What's the weather like today?",
    "Is it going to rain later?",
    "Set a timer for five minutes.",
    "How's the battery doing?",
    "Go to sleep now.",
    "Start a new conversation.",
    "Read that last part again.",
    "Turn on the kitchen lights.",
    "What day is it tomorrow?",
    "Play something quiet.",
    "Skip to the next one.",
    "Mute the microphone.",
    "What's on the shopping list?",
    "Remind me in twenty minutes.",
    # medium prompts
    "What's the weather forecast for tomorrow in the city, and should I "
    "expect rain in the afternoon or will it stay dry all day?",
    "Add milk, eggs, and bread to the shopping list, and while you're at "
    "it, also add coffee filters and a bag of oranges.",
    "Remind me to call the dentist tomorrow morning at ten, and then "
    "remind me again in the afternoon in case I forget.",
    "How long does it take to get to the train station by bike if I "
    "leave right after lunch and traffic is normal?",
    "What's on my calendar for this afternoon, and are there any meetings "
    "I should prepare for before they start?",
    "Turn the thermostat down two degrees for the night, and turn the "
    "hallway lights off when you're done.",
    "Tell me a short bedtime story about a small robot who gets lost in "
    "a library and finds its way home.",
    "What time does the hardware store close on Sundays, and do they "
    "open earlier on weekday mornings?",
    "Set an alarm for seven in the morning on weekdays, and a second one "
    "for half past eight on Saturday.",
    "How much data have I used this month, and how does that compare to "
    "the last three months?",
    "Translate good evening into French for me, and then tell me how to "
    "say thank you very much as well.",
    "Summarize the last message you received from her, and tell me "
    "whether she needs an answer before dinner.",
]

VARIANTS = ("clean", "noisy", "quiet")
SHORT_MAX_S = 5.0
NOISE_SNR_DB = 20.0     # "noisy": white noise at this SNR below the speech
QUIET_DB = 12.0         # "quiet": gain reduction applied to the clip


# ---------------------------------------------------------------- corpus

def _wav_read(path):
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        assert w.getnchannels() == 1 and w.getsampwidth() == 2, path
        pcm = w.readframes(w.getnframes())
    import numpy as np
    return rate, np.frombuffer(pcm, dtype="<i2").astype(np.float64) / 32768.0


def _wav_write(path, rate, samples):
    import numpy as np
    clipped = np.clip(samples, -1.0, 1.0)
    pcm = (clipped * 32767.0).astype("<i2").tobytes()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)


def _slug(text, limit=24):
    keep = "".join(c if c.isalnum() else "-" for c in text.lower())
    while "--" in keep:
        keep = keep.replace("--", "-")
    return keep.strip("-")[:limit].rstrip("-")


def make_clips(out_dir, tts_script, snr_db, quiet_db):
    import numpy as np

    os.makedirs(out_dir, exist_ok=True)
    env = dict(os.environ)
    if not os.environ.get("PARADEE_MODEL_DIR"):
        beside = os.path.join(_HERE, "models")
        if os.path.isdir(beside):
            env["PARADEE_MODEL_DIR"] = beside
    if not os.path.isdir(env.get("PARADEE_MODEL_DIR", "")):
        sys.exit("error: Paradee weights not found; set PARADEE_MODEL_DIR")

    clips = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, text in enumerate(SENTENCES):
            stem = f"{i + 1:02d}_{_slug(text)}"
            mp3 = os.path.join(tmp, stem + ".mp3")
            clean = os.path.join(out_dir, stem + "_clean.wav")
            txt = os.path.join(tmp, stem + ".txt")
            with open(txt, "w") as fh:
                fh.write(text)
            subprocess.run([sys.executable, tts_script, "--text-file", txt,
                            "--out", mp3], check=True, env=env)
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", mp3, "-ar",
                            "16000", "-ac", "1", "-sample_fmt", "s16", clean],
                           check=True)

            rate, x = _wav_read(clean)
            rms = float(np.sqrt(np.mean(x * x))) or 1e-9
            rng = np.random.default_rng(1000 + i)   # fixed seed: reproducible
            variants = {
                "noisy": x + rng.standard_normal(len(x)) * rms * 10 ** (-snr_db / 20),
                "quiet": x * 10 ** (-quiet_db / 20),
            }
            bucket = "short" if len(x) / rate <= SHORT_MAX_S else "medium"
            clips.append({"file": os.path.basename(clean), "text": text,
                          "variant": "clean", "bucket": bucket,
                          "seconds": round(len(x) / rate, 2)})
            for name, samples in variants.items():
                path = os.path.join(out_dir, f"{stem}_{name}.wav")
                _wav_write(path, rate, samples)
                clips.append({"file": os.path.basename(path), "text": text,
                              "variant": name, "bucket": bucket,
                              "seconds": round(len(x) / rate, 2)})
            print(f"  {stem}: {len(x) / rate:4.1f}s {bucket}")

    with open(os.path.join(out_dir, "manifest.json"), "w") as fh:
        json.dump({"clips": clips}, fh, indent=1)
    print(f"==> {len(clips)} clips in {out_dir} (manifest.json)")


def load_manifest(clips_dir):
    path = os.path.join(clips_dir, "manifest.json")
    if not os.path.isfile(path):
        sys.exit(f"error: {path} missing; run --make-clips {clips_dir!r} first")
    with open(path) as fh:
        return json.load(fh)["clips"]


# ---------------------------------------------------------------- metrics

def norm_words(text):
    import re
    text = text.lower().replace("\u2019", "'")
    text = re.sub(r"[^a-z0-9' ]+", " ", text)
    return re.sub(r"\s+", " ", text).strip().split()


def word_edits(ref, hyp):
    """(edit distance, reference word count) between two utterances."""
    r, h = norm_words(ref), norm_words(hyp)
    if not r:
        return len(h), 0
    prev = list(range(len(h) + 1))
    for i, rw in enumerate(r, 1):
        cur = [i] + [0] * len(h)
        for j, hw in enumerate(h, 1):
            cur[j] = min(prev[j] + 1, cur[j - 1] + 1,
                         prev[j - 1] + (rw != hw))
        prev = cur
    return prev[-1], len(r)


def pct(vals, p):
    s = sorted(vals)
    return s[max(0, math.ceil(len(s) * p / 100) - 1)]


# ------------------------------------------------------------- benchmark

def _rss_mb(field):
    with open("/proc/self/status") as fh:
        for line in fh:
            if line.startswith(field):
                return round(int(line.split()[1]) / 1024)
    return None


def _renice():
    try:
        cur = os.nice(0)
        if cur < 19:
            os.nice(19 - cur)
    except OSError:
        pass


def bench_child(model_name, clips, clips_dir, args, queue):
    """Fresh process per model: cold load time, clean RSS, per-clip timing."""
    _renice()
    try:
        from faster_whisper import WhisperModel
        t0 = time.perf_counter()
        model = WhisperModel(model_name, device="cpu",
                             compute_type=args.compute_type,
                             cpu_threads=args.threads)
        load_s = time.perf_counter() - t0
        rss_after_load = _rss_mb("VmRSS:")

        # warm-up decode: first call initializes lazy decode state
        model.transcribe(os.path.join(clips_dir, clips[0]["file"]),
                         language="en", beam_size=args.beam_size,
                         vad_filter=True)

        rows = []
        for c in clips:
            path = os.path.join(clips_dir, c["file"])
            t0 = time.perf_counter()
            segments, _info = model.transcribe(
                path, language="en", beam_size=args.beam_size,
                vad_filter=True)
            text = "".join(s.text for s in segments).strip()
            rows.append({"file": c["file"], "variant": c["variant"],
                         "bucket": c["bucket"], "seconds": c["seconds"],
                         "latency_s": round(time.perf_counter() - t0, 3),
                         "text": text})
        queue.put({"model": model_name, "load_s": round(load_s, 2),
                   "rss_after_load_mb": rss_after_load,
                   "rss_peak_mb": _rss_mb("VmHWM:"), "rows": rows,
                   "error": None})
    except Exception as exc:  # download failure, OOM, ...
        queue.put({"model": model_name, "error": f"{type(exc).__name__}: {exc}"})


def preload_child(model_name, compute_type, threads, queue):
    """Fetch the model into the default HF cache; nothing is timed."""
    _renice()
    try:
        from faster_whisper import WhisperModel
        WhisperModel(model_name, device="cpu", compute_type=compute_type,
                     cpu_threads=threads)
        queue.put({"model": model_name, "error": None})
    except Exception as exc:
        queue.put({"model": model_name, "error": f"{type(exc).__name__}: {exc}"})


def _run_child(target, *a):
    import multiprocessing as mp
    ctx = mp.get_context("fork")
    queue = ctx.Queue()
    proc = ctx.Process(target=target, args=a + (queue,))
    proc.start()
    result = queue.get(timeout=7200)
    proc.join(timeout=120)
    return result


def aggregate(model_name, result):
    """Per-variant WER, per-bucket clean-clip latency stats."""
    out = {"model": model_name, "error": result.get("error")}
    if result.get("error"):
        return out
    rows = result["rows"]
    wers = {}
    for variant in VARIANTS:
        edits = refs = 0
        for r in (r for r in rows if r["variant"] == variant):
            e, n = word_edits(REF_TEXT[r["file"]], r["text"])
            edits += e
            refs += n
        wers[variant] = round(100.0 * edits / refs, 1) if refs else None
    out["wer_pct"] = wers
    lat = {}
    for bucket in ("short", "medium"):
        vals = [r["latency_s"] for r in rows
                if r["bucket"] == bucket and r["variant"] == "clean"]
        if vals:
            lat[bucket] = {"median_s": round(statistics.median(vals), 2),
                           "p90_s": round(pct(vals, 90), 2)}
    out["latency_clean"] = lat
    out["load_s"] = result["load_s"]
    out["rss_after_load_mb"] = result["rss_after_load_mb"]
    out["rss_peak_mb"] = result["rss_peak_mb"]
    return out


def print_table(summaries):
    name_w = max(len(s["model"]) for s in summaries)
    hdr = (f"{'model':{name_w}}  {'load_s':>6} {'rss_mb':>6}  "
           "WER clean/noisy/quiet %       short med/p90 s    medium med/p90 s")
    print(hdr)
    print("-" * len(hdr))
    for s in summaries:
        if s["error"]:
            print(f"{s['model']:{name_w}}  ERROR: {s['error']}")
            continue
        w = s["wer_pct"]
        lat = s["latency_clean"]

        def fmt(b):
            return f"{lat[b]['median_s']:5.2f}/{lat[b]['p90_s']:5.2f}" if b in lat else "  -  /  -  "
        print(f"{s['model']:{name_w}}  {s['load_s']:6.1f} "
              f"{s['rss_after_load_mb']:6d}  "
              f"{w['clean']:5.1f} / {w['noisy']:5.1f} / {w['quiet']:5.1f}      "
              f"{fmt('short')}        {fmt('medium')}")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--make-clips", metavar="DIR",
                    help="synthesize the corpus into DIR and exit")
    ap.add_argument("--clips", metavar="DIR", default="/tmp/stt-clips",
                    help="corpus dir with manifest.json (default %(default)s)")
    ap.add_argument("--models", default="base,base.en,small.en,distil-small.en",
                    help="comma-separated faster-whisper model names")
    ap.add_argument("--tts", default=os.path.join(_HERE, "local_tts.py"),
                    help="local_tts.py used by --make-clips")
    ap.add_argument("--threads", type=int, default=2,
                    help="cpu_threads per model (default %(default)s; kept "
                         "low so a concurrent bench on this host is not "
                         "skewed)")
    ap.add_argument("--compute-type", default="int8",
                    help="shim compute_type (default %(default)s)")
    ap.add_argument("--beam-size", type=int, default=1,
                    help="shim beam size (default %(default)s)")
    ap.add_argument("--snr-db", type=float, default=NOISE_SNR_DB,
                    help="noisy variant SNR (default %(default)s)")
    ap.add_argument("--quiet-db", type=float, default=QUIET_DB,
                    help="quiet variant gain cut (default %(default)s)")
    ap.add_argument("--skip-preload", action="store_true",
                    help="do not fetch models into the HF cache first")
    ap.add_argument("--json", metavar="OUT",
                    help="dump full per-clip results to OUT")
    args = ap.parse_args(argv)

    if args.make_clips:
        make_clips(args.make_clips, args.tts, args.snr_db, args.quiet_db)
        return 0

    clips = load_manifest(args.clips)
    global REF_TEXT
    REF_TEXT = {c["file"]: c["text"] for c in clips}
    models = [m.strip() for m in args.models.split(",") if m.strip()]

    if not args.skip_preload:
        print("==> fetching models into the HF cache (not timed)")
        for m in models:
            r = _run_child(preload_child, m, args.compute_type, args.threads)
            if r["error"]:
                print(f"  {m}: unavailable ({r['error']})")
            else:
                print(f"  {m}: cached")

    print(f"==> benchmarking {len(clips)} clips x {len(models)} models "
          f"(int settings: compute_type={args.compute_type}, "
          f"beam_size={args.beam_size}, threads={args.threads})")
    summaries, raw = [], []
    for m in models:
        print(f"  {m} ...", flush=True)
        result = _run_child(bench_child, m, clips, args.clips, args)
        raw.append(result)
        summaries.append(aggregate(m, result))

    print_table(summaries)
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"summaries": summaries, "raw": raw}, fh, indent=1)
        print(f"==> full results: {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
