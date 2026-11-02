"""Fetch public reference images for local testing only; no training license is implied."""
from pathlib import Path
import hashlib
import http.client
import json
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
ATTEMPTS = 4


def fetch_reference(row, folder):
    target = folder / (row['id'] + '.jpg')
    expected = row['sha256']
    if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == expected:
        return target

    for attempt in range(1, ATTEMPTS + 1):
        try:
            with urllib.request.urlopen(row['url'], timeout=40) as response:
                data = response.read()
        except (OSError, urllib.error.URLError, http.client.HTTPException) as error:
            if isinstance(error, urllib.error.HTTPError) and error.code not in (408, 429, 500, 502, 503, 504):
                raise
            if attempt == ATTEMPTS:
                raise RuntimeError(f"{row['id']}: download failed after {ATTEMPTS} attempts") from error
            delay = 2 ** attempt
            print(f"{row['id']}: download attempt {attempt} failed ({error}); retrying in {delay}s", flush=True)
            time.sleep(delay)
            continue

        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError(f"{row['id']}: upstream reference checksum mismatch")
        temporary = target.with_suffix('.jpg.part')
        try:
            temporary.write_bytes(data)
            temporary.replace(target)
        finally:
            temporary.unlink(missing_ok=True)
        return target


def main():
    folder = ROOT / 'out/testdata'
    folder.mkdir(parents=True, exist_ok=True)
    for row in json.loads((ROOT / 'tests/reference-sources.json').read_text(encoding='utf-8')):
        fetch_reference(row, folder)
        print(row['id'] + ': verified', flush=True)


if __name__ == '__main__':
    main()
