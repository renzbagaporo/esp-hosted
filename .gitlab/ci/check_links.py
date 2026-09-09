#!/usr/bin/env python3
"""Every link and image in the docs points inside this repository, so a broken
path is the only way these pages can regress."""
import glob
import os
import re
import sys

PATTERN = re.compile(r'\]\(([^)\s]+)\)|<img[^>]*src="([^"]+)"')
SKIP = re.compile(r'^(https?:|mailto:|#)')

missing = []
for page in ["README.md"] + sorted(glob.glob("docs/*.md")):
    base = os.path.dirname(page)
    with open(page, encoding="utf-8") as handle:
        text = handle.read()
    for md, html in PATTERN.findall(text):
        target = (md or html).split("#")[0]
        if not target or SKIP.match(target):
            continue
        if not os.path.exists(os.path.join(base, target)):
            missing.append(f"{page}: {target}")

for line in missing:
    print(f"missing: {line}")
print(f"{len(missing)} broken link(s)")
sys.exit(1 if missing else 0)
