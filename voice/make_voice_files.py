"""
Make the voice prompt files for the Smart Medication device with an AI voice.

Where the files go
------------------
Put this file and voice_script.csv in the same folder, for example
Documents\\SmartMed\\voice\\. Running it creates a folder called "mp3" next to
them, holding 0001.mp3 ... 0159.mp3. Copy that whole "mp3" folder to the root
of the FAT32 microSD card, so the card has \\mp3\\0001.mp3 and so on.

How to run (needs internet; no account or API key)
---------------------------------------------------
    pip install edge-tts
    python make_voice_files.py

Useful options
--------------
    python make_voice_files.py --list-voices          show Indian English, Telugu and Hindi voices
    python make_voice_files.py --voice en-IN-PrabhatNeural   use a male voice
    python make_voice_files.py --only 1-10 --force    remake just the container names
    python make_voice_files.py --rate -15%            speak a little slower

How it works
------------
1. Reads voice_script.csv: one row per file (file number, the exact words to say).
2. Sends each line to Microsoft Edge's free online neural text-to-speech
   service through the edge-tts package and saves the MP3 it returns.
3. If ffmpeg is installed, trims the silence at both ends (so chained clips
   such as "seven" + "oh" + "five" don't have long gaps) and re-encodes to
   44.1 kHz mono MP3 with no tags, the safest format for DFPlayer clones.
4. Skips files that already exist, so you can stop and re-run it; use --force
   to remake files after you change their text.

Editing the script: open voice_script.csv in Excel or Google Sheets, change the
"text" column, then save as "CSV UTF-8". The script re-adds the leading zeros
if Excel removes them (1 becomes 0001).
"""
import argparse
import asyncio
import csv
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_VOICE = "en-IN-NeerjaNeural"  # Indian English, female
DEFAULT_RATE = "-5%"  # a touch slower than normal, easier for elderly listeners

# ffmpeg filter: trim leading silence, reverse, trim (old) trailing silence, reverse back,
# then add 60 ms of quiet at the end so the next clip doesn't start too abruptly.
TRIM = ("silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.03,"
        "areverse,"
        "silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.03,"
        "areverse,"
        "apad=pad_dur=0.06")


def read_script(path):
    """Return a list of (file_number, text) pairs from the CSV, e.g. ("0016", "The time is")."""
    if not path.exists():
        sys.exit(f"Can't find {path.name}. Put it in the same folder as this script.")
    rows = []
    # utf-8-sig also accepts the invisible marker Excel adds at the start of "CSV UTF-8" files.
    with open(path, newline="", encoding="utf-8-sig") as f:
        for line_no, row in enumerate(csv.DictReader(f), start=2):
            raw_num = (row.get("file") or "").strip()
            text = (row.get("text") or "").strip()
            if not raw_num and not text:
                continue  # blank row
            if not raw_num.isdigit():
                sys.exit(f"Line {line_no}: the file number {raw_num!r} must be a number like 0007.")
            if not text:
                sys.exit(f"Line {line_no}: file {raw_num} has no text to speak.")
            rows.append((f"{int(raw_num):04d}", text))
    numbers = [n for n, _ in rows]
    dupes = sorted({n for n in numbers if numbers.count(n) > 1})
    if dupes:
        sys.exit(f"These file numbers appear twice in the CSV: {', '.join(dupes)}")
    return rows


def parse_only(spec):
    """Turn "1-10,16,100-105" into a set of file numbers like {"0001", ..., "0105"}."""
    wanted = set()
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            lo, hi = (int(x) for x in part.split("-", 1))
            wanted.update(f"{n:04d}" for n in range(lo, hi + 1))
        else:
            wanted.add(f"{int(part):04d}")
    return wanted


def tidy(src, dst):
    """Trim silence and re-encode with ffmpeg (44.1 kHz, mono, 128 kbps, no ID3 tags)."""
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(src), "-af", TRIM,
                    "-ar", "44100", "-ac", "1", "-b:a", "128k", "-map_metadata", "-1",
                    "-id3v2_version", "0", str(dst)], check=True)


async def speak(edge_tts, text, voice, rate, out_path):
    """Ask the online neural voice to say `text` and save the MP3 to out_path (3 tries)."""
    for attempt in range(1, 4):
        try:
            await edge_tts.Communicate(text, voice, rate=rate).save(str(out_path))
            if out_path.exists() and out_path.stat().st_size > 0:
                return
            raise RuntimeError("empty audio returned")
        except Exception as err:  # network hiccups are the usual cause
            if attempt == 3:
                raise SystemExit(f"Could not make {out_path.name}: {err}\n"
                                 "Check the internet connection and the voice name, then run again.")
            await asyncio.sleep(2 * attempt)


async def list_voices(edge_tts):
    voices = await edge_tts.list_voices()
    for v in sorted(voices, key=lambda v: v["ShortName"]):
        if v["Locale"] in ("en-IN", "te-IN", "hi-IN"):
            print(f'{v["ShortName"]:28} {v["Locale"]}  {v["Gender"]}')


async def main():
    parser = argparse.ArgumentParser(description="Make the DFPlayer voice files with an AI voice.")
    parser.add_argument("--csv", default="voice_script.csv", help="script file (default: voice_script.csv)")
    parser.add_argument("--voice", default=DEFAULT_VOICE, help=f"voice name (default: {DEFAULT_VOICE})")
    parser.add_argument("--rate", default=DEFAULT_RATE, help=f"speed, e.g. -15%% or +5%% (default: {DEFAULT_RATE})")
    parser.add_argument("--only", help='only these files, e.g. "1-10,16,100-105"')
    parser.add_argument("--force", action="store_true", help="remake files that already exist")
    parser.add_argument("--no-tidy", action="store_true", help="skip the ffmpeg trim and re-encode step")
    parser.add_argument("--list-voices", action="store_true", help="list Indian English, Telugu and Hindi voices")
    args = parser.parse_args()

    # Windows consoles may not print Telugu or Hindi text without this.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    try:
        import edge_tts
    except ImportError:
        sys.exit("The edge-tts package is missing. Install it with:  pip install edge-tts")

    if args.list_voices:
        await list_voices(edge_tts)
        return

    rows = read_script(HERE / args.csv)
    if args.only:
        wanted = parse_only(args.only)
        rows = [r for r in rows if r[0] in wanted]

    out_dir = HERE / "mp3"
    out_dir.mkdir(exist_ok=True)
    use_ffmpeg = not args.no_tidy and shutil.which("ffmpeg") is not None
    if not args.no_tidy and not use_ffmpeg:
        print("Note: ffmpeg not found, so silence is not trimmed. The files still work on the DFPlayer.")

    made = skipped = 0
    for num, text in rows:
        final = out_dir / f"{num}.mp3"
        if final.exists() and not args.force:
            skipped += 1
            continue
        raw = out_dir / f"{num}.raw.mp3" if use_ffmpeg else final
        await speak(edge_tts, text, args.voice, args.rate, raw)
        if use_ffmpeg:
            tidy(raw, final)
            raw.unlink()
        made += 1
        print(f"{final.name}  {text}")

    print(f"\nDone: {made} made, {skipped} already there. Files are in: {out_dir}")
    print("Copy the whole 'mp3' folder to the root of the FAT32 microSD card.")


if __name__ == "__main__":
    asyncio.run(main())
