"""Fetch the pinned vendor hardware references locally; do not commit PDF copies."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from urllib.parse import urlparse
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "docs/hardware/sources.json"


def valid_file(path, item):
    if not path.is_file():
        return False
    data = path.read_bytes()
    return (data.startswith(b"%PDF-") and len(data) == item["bytes"]
            and hashlib.sha256(data).hexdigest() == item["sha256"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="List official source URLs only")
    parser.add_argument("--verify-only", action="store_true", help="Check local files without network")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/hardware/originals")
    args = parser.parse_args()
    items = json.loads(CATALOG.read_text(encoding="utf-8"))["documents"]
    for item in items:
        source = urlparse(item["source_url"])
        name = Path(item["file"]).name
        if (source.scheme != "https" or source.hostname != "dl.espressif.com"
                or not name.endswith(".pdf") or not re.fullmatch(r"[a-f0-9]{64}", item["sha256"])):
            raise ValueError("Invalid hardware source entry")
        if args.list:
            print(f"{name} ({item['pages']} pages): {item['source_url']}")
            continue
        target = args.output / name
        if valid_file(target, item):
            print("Verified:", name)
            continue
        if args.verify_only or target.exists():
            raise ValueError("Missing or mismatched PDF: " + str(target))
        with urlopen(item["source_url"], timeout=45) as response:
            if urlparse(response.url).scheme != "https":
                raise ValueError("Hardware download redirected away from HTTPS")
            data = response.read(item["bytes"] + 1)
        if (not data.startswith(b"%PDF-") or len(data) != item["bytes"]
                or hashlib.sha256(data).hexdigest() != item["sha256"]):
            raise ValueError("Downloaded PDF failed integrity check: " + name)
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_suffix(".pdf.part")
        temporary.write_bytes(data)
        temporary.replace(target)
        print("Downloaded and verified:", name)


if __name__ == "__main__":
    main()
