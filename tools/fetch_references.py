#!/usr/bin/env python3
"""Fetch the original score and the pinned one-shot references for local analysis."""

import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.parse import quote
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parents[1] / "work/references"
MANUAL = "https://cdn.roland.com/assets/media/pdf/TR-66_OM.pdf"
REPO = "smpldsnds/drum-abuse-vol1"
REVISION = "58a9b6246d232d862fad15e06c97a4a030655017"
PREFIX = "samples/roland-tr-66/"


def fetch(url):
    with urlopen(url, timeout=30) as response:
        return response.read()


def blob_id(data):
    header = f"blob {len(data)}\0".encode()
    return hashlib.sha1(header + data).hexdigest()


def fetch_sample(entry):
    path = ROOT / "drum-abuse" / entry["path"].removeprefix(PREFIX)
    if path.exists() and blob_id(path.read_bytes()) == entry["sha"]:
        return
    data = fetch(f"https://raw.githubusercontent.com/{REPO}/{REVISION}/{quote(entry['path'])}")
    if len(data) != entry["size"] or blob_id(data) != entry["sha"]:
        raise RuntimeError(f"Unexpected content for {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    manual = ROOT / "TR-66_OM.pdf"
    if not manual.exists():
        data = fetch(MANUAL)
        if not data.startswith(b"%PDF-"):
            raise RuntimeError("Roland URL did not return a PDF")
        manual.write_bytes(data)

    tree = json.loads(fetch(f"https://api.github.com/repos/{REPO}/git/trees/{REVISION}?recursive=1"))
    samples = [entry for entry in tree["tree"] if
               entry["type"] == "blob" and entry["path"].startswith(PREFIX)
               and entry["path"].lower().endswith(".wav")]
    if tree["truncated"] or len(samples) != 184:
        raise RuntimeError(f"Expected 184 TR-66 WAV files, found {len(samples)}")
    with ThreadPoolExecutor(max_workers=6) as pool:
        for _ in pool.map(fetch_sample, samples):
            pass
    print(f"Verified {len(samples)} one-shots under {ROOT}")


if __name__ == "__main__":
    main()
