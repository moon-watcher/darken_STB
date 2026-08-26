#!/usr/bin/env python3
"""
Darken SHMUP — Versión terminal jugable (curses)
================================================

Un shmup vertical completo que emula la arquitectura de Darken 2.0
en Python puro, jugable en cualquier terminal.

Controles:
    A/D o ←/→   — Mover nave
    W/S o ↑/↓   — Mover nave (vertical)
    ESPACIO     — Disparar
    B           — Bomba (limpia balas enemigas)
    P           — Pausa
    R           — Reiniciar (Game Over)
    Q / ESC     — Salir

Ejecutar:
    python3 darken_shmup_terminal.py
"""

import curses
import math
import random
import time
from collections import namedtuple

# =============================================================================
# CONSTANTES
# =============================================================================

SCREEN_W = 60
SCREEN_H = 40
FPS = 30

TAG_PLAYER = 1
TAG_PLAYER_BULLET = 2
TAG_ENEMY = 3
TAG_ENEMY_BULLET = 4
TAG_POWERUP = 5
TAG_PARTICLE = 6
TAG_BOSS = 7
TAG_BOSS_SHIELD = 8

PWR_SPREAD = 1
PWR_BOMB = 2
PWR_SHIELD = 3

# Sprites ASCII
SPR_PLAYER = "^"
SPR_BULLET = "|"
SPR_ENEMY = "v"
SPR_SHOOTER = "V"
SPR_BOSS = "[#]"
SPR_SHIELD = "O"
SPR_POWERUP = "*"
SPR_PARTICLE = "."
SPR_ENEMY_BULLET = "o"

# =============================================================================
# ENTIDADES (emulación de darken.h)
# =============================================================================

class Entity:
    __slots__ = ("data", "state", "destructor", "tag", "usr", "slot", "_owner")
    def __init__(self):
        self.data = None
        self.state = None
        self.destructor = None
        self.tag = 0
        self.usr = 0
        self.slot = 0
        self._owner = None

class Manager:
    __slots__ = ("pool", "capacity", "size", "paused")
    def __init__(self, capacity):
        self.capacity = capacity
        self.size = 0
        self.paused = capacity
        self.pool = [Entity() for _ in range(capacity)]
        for i, e in enumerate(self.pool):
            e.slot = i
            e._owner = self

    def spawn(self):
        if self.size >= self.paused:
            return None
        e = self.pool[self.size]
        e.data = {}
        e.state = None
        e.destructor = None
        e.tag = 0
        e.usr = 0
        self.size += 1
        return e

    def _swap(self, i, j):
        if i == j:
            return
        self.pool[i], self.pool[j] = self.pool[j], self.pool[i]
        self.pool[i].slot = i
        self.pool[j].slot = j

    def update(self):
        i = self.size
        while i > 0:
            i -= 1
            e = self.pool[i]
            if e.state is None:
                continue
            result = e.state(e)
            if result == "DELETE":
                if e.destructor:
                    e.destructor(e)
                self.size -= 1
                self._swap(i, self.size)
            elif result == "PAUSE":
                self.size -= 1
                self.paused -= 1
                self._swap(i, self.size)
                self._swap(self.size, self.paused)
            elif result == "LOOP":
                pass
            else:
                e.state = result

    def reset(self):
        for i in range(self.size):
            e = self.pool[i]
            if e.destructor:
                e.destructor(e)
        self.size = 0
        self.paused = self.capacity

    def pause_entity(self, e):
        if e.slot >= self.size:
            return
        self.size -= 1
        self.paused -= 1
        self._swap(e.slot, self.size)
        self._swap(self.size, self.paused)

    def resume_entity(self, e):
        if e.slot < self.paused:
            return
        self._swap(e.slot, self.paused)
        self._swap(self.paused, self.size)
        self.paused += 1
        self.size += 1

    def delete_entity(self, e):
        if e.destructor:
            e.destructor(e)
        if e.slot < self.size:
            self.size -= 1
            self._swap(e.slot, self.size)
        elif e.slot >= self.paused:
            self._swap(e.slot, self.paused)
            self.paused += 1

    def foreach_active(self, fn):
        for i in range(self.size - 1, -1, -1):
            fn(self.pool[i])

    def entity_in_active(self, e):
        return e.slot < self.size

    def entity_in_paused(self, e):
        return e.slot >= self.paused

    def entity_in_used(self, e):
        return self.entity_in_active(e) or self.entity_in_paused(e)

# =============================================================================
# ESTADO GLOBAL
# =============================================================================

class GameState:
    def __init__(self):
        self.score = 0
        self.wave = 1
        self.wave_timer = 60
        self.boss_spawned = False
        self.game_over = False
        self.paused = False
        self.shake = 0
        self.keys = set()
        self.player = None
        self.pending_explosions = []

G = GameState()
world = Manager(128)
fx_world = Manager(128)

# =============================================================================
# ESTADOS
# =============================================================================

def state_player_alive(e):
    d = e.data
    dx = dy = 0
    if "a" in G.keys or curses.KEY_LEFT in G.keys:  dx = -1
    if "d" in G.keys or curses.KEY_RIGHT in G.keys: dx = 1
    if "w" in G.keys or curses.KEY_UP in G.keys:    dy = -1
    if "s" in G.keys or curses.KEY_DOWN in G.keys:  dy = 1
    d["x"] = max(1, min(SCREEN_W - 2, d["x"] + dx))
    d["y"] = max(1, min(SCREEN_H - 2, d["y"] + dy))

    if " " in G.keys and e.usr == 0:
        pw = d.get("power", 1)
        if pw == 1:
            spawn_bullet(d["x"], d["y"] - 1, 0, -1, TAG_PLAYER_BULLET)
        elif pw == 2:
            spawn_bullet(d["x"] - 1, d["y"], 0, -1, TAG_PLAYER_BULLET)
            spawn_bullet(d["x"] + 1, d["y"], 0, -1, TAG_PLAYER_BULLET)
        else:
            spawn_bullet(d["x"], d["y"] - 1, 0, -1, TAG_PLAYER_BULLET)
            spawn_bullet(d["x"] - 1, d["y"], -1, -1, TAG_PLAYER_BULLET)
            spawn_bullet(d["x"] + 1, d["y"], 1, -1, TAG_PLAYER_BULLET)
        e.usr = 5
    if e.usr > 0:
        e.usr -= 1

    if "b" in G.keys and d.get("bombs", 0) > 0:
        d["bombs"] -= 1
        G.shake = 5
        to_del = []
        world.foreach_active(lambda ent: to_del.append(ent) if ent.tag == TAG_ENEMY_BULLET else None)
        for b in to_del:
            world.delete_entity(b)

    if d.get("shield_timer", 0) > 0:
        d["shield_timer"] -= 1

    return "LOOP"

def state_player_invulnerable(e):
    d = e.data
    d["x"] = max(1, min(SCREEN_W - 2, d["x"]))
    d["y"] = max(1, min(SCREEN_H - 2, d["y"]))
    e.usr -= 1
    if e.usr <= 0:
        return state_player_alive
    return "LOOP"

def state_player_dead(e):
    G.game_over = True
    return "LOOP"

def state_bullet_fly(e):
    d = e.data
    d["x"] += d.get("vx", 0)
    d["y"] += d.get("vy", 0)
    if d["x"] < 0 or d["x"] >= SCREEN_W or d["y"] < 0 or d["y"] >= SCREEN_H:
        return "DELETE"
    return "LOOP"

def state_enemy_sine(e):
    d = e.data
    d["t"] = d.get("t", 0) + 1
    d["x"] = d["origin_x"] + int(math.sin(d["t"] * 0.1) * 10)
    d["y"] += 1
    if d["y"] >= SCREEN_H - 1:
        return "DELETE"
    return "LOOP"

def state_enemy_straight(e):
    d = e.data
    d["y"] += 1
    if d["y"] >= SCREEN_H - 1:
        return "DELETE"
    return "LOOP"

def state_enemy_shooter(e):
    d = e.data
    d["y"] += 1
    d["shoot_timer"] = d.get("shoot_timer", 0) + 1
    if d["shoot_timer"] > 40 and G.player and world.entity_in_active(G.player):
        pd = G.player.data
        dx = pd["x"] - d["x"]
        dy = pd["y"] - d["y"]
        dist = math.hypot(dx, dy)
        if dist > 0:
            vx = int(dx * 1.5 / dist)
            vy = int(dy * 1.5 / dist)
            spawn_bullet(d["x"], d["y"], vx, vy, TAG_ENEMY_BULLET)
        d["shoot_timer"] = 0
    if d["y"] >= SCREEN_H - 1:
        return "DELETE"
    return "LOOP"

def state_boss_shield(e):
    return "LOOP"

def state_boss_enter(e):
    d = e.data
    d["y"] += 1
    if d["y"] > 5:
        return state_boss_attack
    return "LOOP"

def state_boss_attack(e):
    d = e.data
    d["t"] = d.get("t", 0) + 1
    d["x"] = SCREEN_W // 2 + int(math.sin(d["t"] * 0.05) * 20)
    d["timer"] = d.get("timer", 0) + 1
    if d["timer"] > 30:
        d["timer"] = 0
        for i in range(8):
            a = i * math.pi / 4
            spawn_bullet(d["x"], d["y"] + 1, int(math.cos(a)*2), int(math.sin(a)*2), TAG_ENEMY_BULLET)

    if d["shield_left"] and d["shield_left"].state == state_boss_shield:
        d["shield_left"].data["x"] = d["x"] - 3
        d["shield_left"].data["y"] = d["y"]
    if d["shield_right"] and d["shield_right"].state == state_boss_shield:
        d["shield_right"].data["x"] = d["x"] + 3
        d["shield_right"].data["y"] = d["y"]

    if d["hp"] <= 0:
        return state_boss_defeated
    return "LOOP"

def state_boss_defeated(e):
    d = e.data
    d["y"] += 1
    if random.random() < 0.3:
        spawn_particle(d["x"] + random.randint(-2, 2), d["y"], 1)
    if d["y"] > SCREEN_H:
        return "DELETE"
    return "LOOP"

def state_powerup_fall(e):
    d = e.data
    d["y"] += 1
    d["blink"] = d.get("blink", 0) + 1
    if d["y"] >= SCREEN_H - 1:
        return "DELETE"
    return "LOOP"

def state_particle_fade(e):
    d = e.data
    d["x"] += d.get("vx", 0)
    d["y"] += d.get("vy", 0)
    d["life"] -= 1
    if d["life"] <= 0:
        return "DELETE"
    return "LOOP"

# =============================================================================
# SPAWNER
# =============================================================================

def spawn_bullet(x, y, vx, vy, tag):
    e = world.spawn()
    if not e:
        return
    e.data = {"x": int(x), "y": int(y), "vx": vx, "vy": vy}
    e.state = state_bullet_fly
    e.tag = tag

def spawn_enemy(etype, x, y):
    e = world.spawn()
    if not e:
        return
    hp = 3 if etype == 2 else 1
    e.data = {"x": int(x), "y": int(y), "origin_x": int(x), "t": 0, "hp": hp, "type": etype, "shoot_timer": 0}
    e.tag = TAG_ENEMY
    e.destructor = lambda ent: enemy_destructor(ent)
    if etype == 0:
        e.state = state_enemy_sine
    elif etype == 1:
        e.state = state_enemy_straight
    else:
        e.state = state_enemy_shooter

def enemy_destructor(e):
    d = e.data
    G.pending_explosions.append((d["x"], d["y"]))
    G.score += 100
    if random.random() < 0.15:
        spawn_powerup(d["x"], d["y"])

def spawn_powerup(x, y):
    e = world.spawn()
    if not e:
        return
    e.data = {"x": int(x), "y": int(y), "vy": 1, "kind": random.randint(1, 3), "blink": 0}
    e.state = state_powerup_fall
    e.tag = TAG_POWERUP

def spawn_particle(x, y, color):
    e = fx_world.spawn()
    if not e:
        return
    e.data = {
        "x": int(x), "y": int(y),
        "vx": random.randint(-1, 1),
        "vy": random.randint(-1, 1),
        "life": random.randint(5, 15),
        "color": color
    }
    e.state = state_particle_fade
    e.tag = TAG_PARTICLE

def spawn_boss():
    e = world.spawn()
    if not e:
        return
    e.data = {"x": SCREEN_W // 2, "y": -2, "hp": 50, "max_hp": 50, "t": 0, "timer": 0,
              "shield_left": None, "shield_right": None}
    e.state = state_boss_enter
    e.tag = TAG_BOSS
    e.destructor = lambda ent: boss_destructor(ent)

    sl = world.spawn()
    if sl:
        sl.data = {"x": e.data["x"] - 3, "y": e.data["y"], "hp": 10}
        sl.state = state_boss_shield
        sl.tag = TAG_BOSS_SHIELD
        e.data["shield_left"] = sl

    sr = world.spawn()
    if sr:
        sr.data = {"x": e.data["x"] + 3, "y": e.data["y"], "hp": 10}
        sr.state = state_boss_shield
        sr.tag = TAG_BOSS_SHIELD
        e.data["shield_right"] = sr

    G.boss_spawned = True

def boss_destructor(e):
    G.score += 5000
    G.shake = 10
    G.boss_spawned = False
    G.wave_timer = 60

# =============================================================================
# COLISIONES
# =============================================================================

def check_collisions():
    # Balas del jugador vs enemigos/jefe
    to_remove_bullets = []
    to_remove_targets = []

    for i in range(world.size - 1, -1, -1):
        b = world.pool[i]
        if b.tag != TAG_PLAYER_BULLET:
            continue
        bd = b.data
        for j in range(world.size - 1, -1, -1):
            t = world.pool[j]
            if t.tag not in (TAG_ENEMY, TAG_BOSS, TAG_BOSS_SHIELD):
                continue
            td = t.data
            hit = False
            if t.tag == TAG_ENEMY:
                if abs(bd["x"] - td["x"]) <= 1 and abs(bd["y"] - td["y"]) <= 1:
                    td["hp"] -= 1
                    if td["hp"] <= 0:
                        to_remove_targets.append(t)
                    hit = True
            elif t.tag == TAG_BOSS:
                if abs(bd["x"] - td["x"]) <= 2 and abs(bd["y"] - td["y"]) <= 1:
                    td["hp"] -= 1
                    hit = True
            elif t.tag == TAG_BOSS_SHIELD:
                if abs(bd["x"] - td["x"]) <= 1 and abs(bd["y"] - td["y"]) <= 1:
                    td["hp"] -= 1
                    if td["hp"] <= 0:
                        to_remove_targets.append(t)
                    hit = True
            if hit:
                to_remove_bullets.append(b)
                break

    for b in to_remove_bullets:
        world.delete_entity(b)
    for t in to_remove_targets:
        world.delete_entity(t)

    # Enemigos/balas enemigas vs jugador
    if not G.player or not world.entity_in_active(G.player):
        return
    pd = G.player.data
    if pd.get("inv_timer", 0) > 0 or pd.get("shield_timer", 0) > 0:
        return

    px, py = pd["x"], pd["y"]
    hit_something = None

    for i in range(world.size - 1, -1, -1):
        e = world.pool[i]
        ed = e.data
        hit = False
        if e.tag == TAG_ENEMY_BULLET:
            if abs(ed["x"] - px) <= 1 and abs(ed["y"] - py) <= 1:
                hit = True
        elif e.tag == TAG_ENEMY:
            if abs(ed["x"] - px) <= 1 and abs(ed["y"] - py) <= 1:
                hit = True
        elif e.tag == TAG_BOSS:
            if abs(ed["x"] - px) <= 2 and abs(ed["y"] - py) <= 1:
                hit = True
        elif e.tag == TAG_POWERUP:
            if abs(ed["x"] - px) <= 1 and abs(ed["y"] - py) <= 1:
                if ed["kind"] == PWR_SPREAD and pd["power"] < 3:
                    pd["power"] += 1
                elif ed["kind"] == PWR_BOMB and pd.get("bombs", 0) < 5:
                    pd["bombs"] = pd.get("bombs", 0) + 1
                elif ed["kind"] == PWR_SHIELD:
                    pd["shield_timer"] = 90
                G.score += 50
                world.delete_entity(e)
                continue

        if hit:
            hit_something = e
            break

    if hit_something:
        pd["hp"] = pd.get("hp", 1) - 1
        pd["inv_timer"] = 60
        G.shake = 5
        for _ in range(8):
            spawn_particle(px, py, 0)
        if hit_something.tag == TAG_ENEMY_BULLET:
            world.delete_entity(hit_something)
        if pd["hp"] <= 0:
            G.player.state = state_player_dead
        else:
            G.player.state = state_player_invulnerable

# =============================================================================
# OLEADAS
# =============================================================================

def update_wave():
    if G.game_over or G.paused or G.boss_spawned:
        return
    if G.wave_timer > 0:
        G.wave_timer -= 1
        return

    if not hasattr(update_wave, "spawned"):
        update_wave.spawned = 0
        update_wave.to_spawn = 15
        update_wave.spawn_timer = 0

    update_wave.spawn_timer += 1
    if update_wave.spawn_timer < 20:
        return
    update_wave.spawn_timer = 0

    if update_wave.spawned < update_wave.to_spawn:
        etype = 2 if (G.wave % 3 == 2 and update_wave.spawned % 5 == 0) else (update_wave.spawned % 2)
        x = 3 + random.randint(0, SCREEN_W - 6)
        spawn_enemy(etype, x, 1)
        update_wave.spawned += 1
    else:
        alive = sum(1 for i in range(world.size) if world.pool[i].tag == TAG_ENEMY)
        if alive == 0:
            spawn_boss()
            update_wave.spawned = 0
            update_wave.to_spawn = 15 + G.wave * 3
            G.wave += 1

# =============================================================================
# RENDERIZADO
# =============================================================================

def draw(stdscr, frame):
    stdscr.clear()

    # Fondo
    for y in range(SCREEN_H):
        for x in range(SCREEN_W):
            if (x + y + frame) % 17 == 0:
                try:
                    stdscr.addch(y, x, '.', curses.color_pair(4))
                except:
                    pass

    # Shake
    ox = random.randint(-1, 1) if G.shake > 0 else 0
    oy = random.randint(-1, 1) if G.shake > 0 else 0
    if G.shake > 0:
        G.shake -= 1

    # Entidades del mundo
    for i in range(world.size):
        e = world.pool[i]
        d = e.data
        x = d.get("x", 0) + ox
        y = d.get("y", 0) + oy
        if x < 0 or x >= SCREEN_W or y < 0 or y >= SCREEN_H:
            continue
        try:
            if e.tag == TAG_PLAYER:
                if d.get("inv_timer", 0) > 0 and frame % 4 < 2:
                    continue
                color = 3 if d.get("shield_timer", 0) > 0 else 2
                stdscr.addch(y, x, SPR_PLAYER, curses.color_pair(color) | curses.A_BOLD)
            elif e.tag == TAG_PLAYER_BULLET:
                stdscr.addch(y, x, SPR_BULLET, curses.color_pair(3) | curses.A_BOLD)
            elif e.tag == TAG_ENEMY:
                c = 1 if d.get("type", 0) != 2 else 5
                spr = SPR_SHOOTER if d.get("type", 0) == 2 else SPR_ENEMY
                stdscr.addch(y, x, spr, curses.color_pair(c))
            elif e.tag == TAG_ENEMY_BULLET:
                stdscr.addch(y, x, SPR_ENEMY_BULLET, curses.color_pair(5))
            elif e.tag == TAG_POWERUP:
                if d.get("blink", 0) % 8 < 4:
                    color = {PWR_SPREAD: 3, PWR_BOMB: 5, PWR_SHIELD: 6}.get(d.get("kind", 1), 3)
                    stdscr.addch(y, x, SPR_POWERUP, curses.color_pair(color) | curses.A_BOLD)
            elif e.tag == TAG_BOSS:
                for bx in range(-1, 2):
                    try:
                        stdscr.addch(y, x + bx, SPR_BOSS[bx + 1], curses.color_pair(5) | curses.A_BOLD)
                    except:
                        pass
                # Barra de vida
                hp_bar = int((d.get("hp", 1) / d.get("max_hp", 1)) * 5)
                try:
                    stdscr.addstr(y - 1, x - 2, "[" + "=" * hp_bar + " " * (5 - hp_bar) + "]", curses.color_pair(1))
                except:
                    pass
            elif e.tag == TAG_BOSS_SHIELD:
                stdscr.addch(y, x, SPR_SHIELD, curses.color_pair(5))
        except:
            pass

    # Partículas
    for i in range(fx_world.size):
        e = fx_world.pool[i]
        d = e.data
        x = d.get("x", 0) + ox
        y = d.get("y", 0) + oy
        if 0 <= x < SCREEN_W and 0 <= y < SCREEN_H:
            try:
                color = {0: 2, 1: 1, 2: 6}.get(d.get("color", 0), 7)
                stdscr.addch(y, x, SPR_PARTICLE, curses.color_pair(color))
            except:
                pass

    # HUD
    try:
        if G.player and world.entity_in_used(G.player):
            pd = G.player.data
            lives = pd.get("hp", 0)
            bombs = pd.get("bombs", 0)
            stdscr.addstr(0, 0, f"VIDAS: {'♥' * lives}  BOMBAS: {'●' * bombs}  SCORE: {G.score}  WAVE: {G.wave}", curses.color_pair(7) | curses.A_BOLD)
        else:
            stdscr.addstr(0, 0, f"SCORE: {G.score}  WAVE: {G.wave}", curses.color_pair(7) | curses.A_BOLD)
    except:
        pass

    # Pausa / Game Over
    if G.paused:
        try:
            stdscr.addstr(SCREEN_H // 2, SCREEN_W // 2 - 3, "PAUSA", curses.color_pair(7) | curses.A_BOLD | curses.A_BLINK)
        except:
            pass
    if G.game_over:
        try:
            stdscr.addstr(SCREEN_H // 2 - 1, SCREEN_W // 2 - 5, "GAME OVER", curses.color_pair(1) | curses.A_BOLD)
            stdscr.addstr(SCREEN_H // 2 + 1, SCREEN_W // 2 - 8, "Pulsa R para reiniciar", curses.color_pair(7))
        except:
            pass

    stdscr.refresh()

# =============================================================================
# BUCLE PRINCIPAL
# =============================================================================

def reset_game():
    world.reset()
    fx_world.reset()
    G.score = 0
    G.wave = 1
    G.wave_timer = 60
    G.boss_spawned = False
    G.game_over = False
    G.paused = False
    G.shake = 0
    G.pending_explosions = []
    update_wave.spawned = 0
    update_wave.to_spawn = 15
    update_wave.spawn_timer = 0

    G.player = world.spawn()
    G.player.data = {"x": SCREEN_W // 2, "y": SCREEN_H - 5, "hp": 3, "power": 1, "bombs": 2, "inv_timer": 0, "shield_timer": 0}
    G.player.state = state_player_alive
    G.player.tag = TAG_PLAYER

def main(stdscr):
    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.timeout(1000 // FPS)
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_RED, -1)
    curses.init_pair(2, curses.COLOR_GREEN, -1)
    curses.init_pair(3, curses.COLOR_YELLOW, -1)
    curses.init_pair(4, curses.COLOR_BLUE, -1)
    curses.init_pair(5, curses.COLOR_MAGENTA, -1)
    curses.init_pair(6, curses.COLOR_CYAN, -1)
    curses.init_pair(7, curses.COLOR_WHITE, -1)

    reset_game()
    frame = 0

    while True:
        # Input
        try:
            key = stdscr.getch()
        except:
            key = -1

        if key != -1:
            if key == 27 or key == ord('q'):  # ESC o Q
                break
            if key == ord('p') and not G.game_over:
                G.paused = not G.paused
                if G.paused:
                    world.foreach_active(lambda e: world.pause_entity(e) if e != G.player else None)
                    fx_world.foreach_active(lambda e: fx_world.pause_entity(e))
                else:
                    while world.paused < world.capacity:
                        world.resume_entity(world.pool[world.paused])
                    while fx_world.paused < fx_world.capacity:
                        fx_world.resume_entity(fx_world.pool[fx_world.paused])
            if key == ord('r') and G.game_over:
                reset_game()
            if key == ord(' '):
                G.keys.add(" ")
            if key == ord('b'):
                G.keys.add("b")
            if key == ord('a') or key == curses.KEY_LEFT:
                G.keys.add("a")
            if key == ord('d') or key == curses.KEY_RIGHT:
                G.keys.add("d")
            if key == ord('w') or key == curses.KEY_UP:
                G.keys.add("w")
            if key == ord('s') or key == curses.KEY_DOWN:
                G.keys.add("s")
        else:
            G.keys.clear()

        if not G.paused and not G.game_over:
            world.update()
            fx_world.update()

            # Explosiones pendientes
            for ex, ey in G.pending_explosions:
                for _ in range(6):
                    spawn_particle(ex, ey, 1)
            G.pending_explosions.clear()

            check_collisions()
            update_wave()

        draw(stdscr, frame)
        frame += 1
        time.sleep(1.0 / FPS)

if __name__ == "__main__":
    curses.wrapper(main)
