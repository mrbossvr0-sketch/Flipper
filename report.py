#!/usr/bin/env python3
"""PrivateLab 3.0 offline report. Read-only, strict input filename.
No upload, no external libraries. Logs use Flipper uptime in seconds, not clock time.
"""
import argparse
import collections
import hashlib
import html
from pathlib import Path


def read_events(folder: Path):
    log = folder / "audit.tsv"
    if not log.is_file():
        return []
    out = []
    # Truncate in bounded fashion rather than displaying untrusted arbitrary files.
    with log.open("r", encoding="utf-8", errors="replace") as src:
        for line in src:
            fields = line.rstrip("\r\n").split("\t", 2)
            if len(fields) != 3:
                continue
            uptime, event, data = fields
            if not uptime.isdigit():
                continue
            if len(event) > 48 or len(data) > 512:
                continue
            out.append((int(uptime), event, data))
            if len(out) >= 10000:
                break
    return out


def render_report(folder: Path):
    events = read_events(folder)
    counts = collections.Counter(event for _, event, _ in events)
    log_path = folder / "audit.tsv"
    sha = hashlib.sha256(log_path.read_bytes()).hexdigest() if log_path.is_file() else "absent"
    lines = [
        "# PrivateLab 3.0 — Local Diagnostics Report", "",
        "**Data source:** Local SD-card evidence (not authenticated).", "",
        f"**Log SHA-256:** `{sha}`", "",
        "**Time note:** Event timestamps are *device uptime seconds*, not civil times.", "",
        f"**Validated event rows:** {len(events)}", "",
        "## Event counts", "",
        "| Event | Count |", "|---|---:|",
    ]
    lines.extend(f"| {html.escape(event)} | {count} |" for event, count in counts.most_common())
    lines.extend(["", "## Latest observations", "", "| Uptime (s) | Type | Value |", "|---:|---|---|"])
    for up, ev, data in events[-150:]:
        # Escape HTML and Markdown table-breaking pipes; no shell or script evaluation.
        clean_ev = html.escape(ev).replace("|", "\\|")
        clean_data = html.escape(data).replace("|", "\\|")
        lines.append(f"| {up} | {clean_ev} | {clean_data} |")
    lines.extend(["", "---", "",
        "*For authorized device testing only. RSSI readings are relative to the receiver's",
        "configuration and should not be treated as calibrated spectrum measurements.*", ""])
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("folder", type=Path, help="Copy of SD apps_data/privatelab3")
    ap.add_argument("-o", "--output", type=Path, default=Path("privatelab3_report.md"))
    args = ap.parse_args()
    args.output.write_text(render_report(args.folder), encoding="utf-8")
    print(f"Created {args.output}")


if __name__ == "__main__":
    main()
