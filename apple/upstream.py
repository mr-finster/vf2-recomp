#!/usr/bin/env python3
"""Report upstream integration points, or record an upstream commit after a completed merge."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
METADATA = ROOT / "apple/upstream.json"


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", default="upstream/main", help="Fetched upstream ref to compare")
    parser.add_argument("--record-base", metavar="REF", help="Record a commit already merged into HEAD")
    args = parser.parse_args()
    metadata = json.loads(METADATA.read_text())
    if args.record_base:
        commit = git("rev-parse", "--verify", args.record_base + "^{commit}")
        if subprocess.run(["git", "merge-base", "--is-ancestor", commit, "HEAD"], cwd=ROOT).returncode:
            parser.error("Complete the upstream merge before recording its base commit.")
        metadata["base_commit"] = commit
        METADATA.write_text(json.dumps(metadata, indent=2) + "\n")
        print("Recorded upstream base:", commit)
        return

    base = metadata["base_commit"]
    tracked = set(git("ls-tree", "-r", "--name-only", base).splitlines())
    changed = set(git("diff", "--name-only", base, "--").splitlines())
    integration = sorted(changed & tracked)
    print("Recorded upstream base:", base)
    print("Upstream files modified by this port:")
    for path in integration:
        print("  " + path)
    manifest = json.loads((ROOT / "apple/sources.json").read_text())
    missing = [path for paths in manifest.values() for path in paths if not (ROOT / path).is_file()]
    if missing:
        parser.error("Update apple/sources.json; missing sources: " + ", ".join(missing))
    print("Apple source manifest: all files present")
    available = subprocess.run(["git", "rev-parse", "--verify", args.upstream + "^{commit}"],
                               cwd=ROOT, capture_output=True).returncode == 0
    if not available:
        print("Fetch the upstream remote to compare incoming changes; see apple/UPSTREAM.md.")
        return
    incoming = set(git("diff", "--name-only", base, args.upstream, "--").splitlines())
    overlap = sorted(incoming & set(integration))
    print("Incoming upstream files also modified by the port:")
    for path in overlap:
        print("  " + path)
    if not overlap:
        print("  None")
    print("This reports file overlap, not a guarantee that a merge will be conflict-free.")


if __name__ == "__main__":
    main()
