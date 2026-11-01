#!/usr/bin/env python3
"""Offroad sahnesindeki "Terrain" varlığına make_offroad.py'nin ürettiği TerrainComponent bloğunu ekler."""
import os
import sys

scene = sys.argv[1]
block = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "offroad_terrain.yaml"), encoding="utf-8").read()
lines = open(scene, encoding="utf-8").read().split("\n")
tag = next(i for i, l in enumerate(lines) if l.strip() == "Tag: Terrain")
end = next((i for i in range(tag, len(lines)) if lines[i].startswith("  - Entity:") or (lines[i] and not lines[i].startswith(" "))), len(lines))
if any("TerrainComponent:" in l for l in lines[tag:end]):
    sys.exit(0)
lines[end:end] = block.rstrip("\n").split("\n")
open(scene, "w", encoding="utf-8").write("\n".join(lines))
print("terrain inserted")
