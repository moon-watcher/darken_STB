# resources.res — Darken Tactics
#
# Compiled by SGDK's rescomp into resources.h / resources.c, which main.c
# includes to get the `battlefield`, `hero_spr`, `enemy_spr` and
# `cursor_spr` symbols.
#
# All art here is placeholder (simple flat-colour blobs) — swap the PNGs
# for real art without touching main.c, as long as the frame size (2x2
# tiles = 16x16px) stays the same.

# Static battlefield background, drawn once on BG_B at startup.
IMAGE battlefield "battlefield.png" NONE

# Unit sprites: 2x2 tiles (16x16px), single frame, no compression.
SPRITE hero_spr   "hero.png"   2 2 NONE
SPRITE enemy_spr  "enemy.png"  2 2 NONE
SPRITE cursor_spr "cursor.png" 2 2 NONE
