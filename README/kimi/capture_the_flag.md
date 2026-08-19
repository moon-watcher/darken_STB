# Capture The Flag — Juego de ejemplo con Darken 2.0

Un juego completo de **Capture The Flag** en terminal con dos jugadores humanos, tres tipos de enemigos con IA distinta, y el engine Darken 2.0 gestionando todo.

---

## 🎮 Cómo jugar

Compilá esto junto al `darken.h` que ya tenés:

```bash
gcc -std=gnu99 capture.c -o ctf -lm
./ctf
```

**Controles:**
- **P1** (símbolo `1`): `W A S D`
- **P2** (símbolo `2`): `I J K L`
- **Salir**: `Q`

El primero que toque la `$` gana. Si te toca un enemigo, perdés 1 de 3 vidas y volvés al spawn. Si se te acaban, gana el otro player.

---

## 🧠 Los tres tipos de IA

| Enemigo | Símbolo | Comportamiento |
|---------|---------|----------------|
| **Chaser** | `C` | Persigue al player más cercano sin piedad. |
| **Patroller** | `P` | Cambia de dirección cada ~1.5 segundos, patrullando como poli de barrio. |
| **Blocker** | `B` | Se interpone a mitad de camino entre el player más cercano y la bandera. |

---

## 🎯 Cómo está usando Darken

| Feature | Dónde |
|---------|-------|
| **`de_manager_update()`** | En cada frame del loop principal. Es el "heartbeat" que ejecuta los estados de todos. |
| **`de_state` (callbacks)** | Cada entidad tiene `game_state` como callback. Ahí adentro vive el movimiento, la fricción y la llamada a la IA. |
| **`de_system` (render)** | `render_sys` es un array plano de punteros `(x, y, symbol)`. Se actualiza automáticamente porque apunta a los campos de las entidades vivas. |
| **`DE_MANAGER_FOREACH`** | Usado en `check_collisions` para recorrer players y enemigos sin preocuparse por quién murió o quién se creó recién. |
| **`DE_SYSTEM_ADD`** | Cada vez que creamos una entidad, la registramos en el sistema de renderizado con sus 3 punteros. |

---

## 📁 Estructura del código

```
capture.c
├── Input sin bloqueo (kbhit, getch_noblock)
├── EntityData (payload de cada entidad)
├── IA de enemigos
│   ├── ai_chaser()      → persigue al más cercano
│   ├── ai_patroller()   → cambia de dirección cada 50 frames
│   └── ai_blocker()     → bloquea el camino a la bandera
├── game_state()         → de_state ejecutado por de_manager_update()
├── create_entity()      → fábrica usando de_manager_new + DE_SYSTEM_ADD
├── check_collisions()   → doble DE_MANAGER_FOREACH (players vs enemigos)
├── render_frame()       → dibuja vía DE_SYSTEM_FOREACH_3
└── main()               → loop de juego (~30 FPS)
```

---

## 🔧 Notas técnicas

- Los players tienen **fricción** (`vx *= 0.85f`) para que no se deslicen eternamente.
- **Invulnerabilidad** de 45 frames (~0.75s) tras ser golpeado para evitar spam de daño.
- Los enemigos usan **coordenadas globales** (`g_p1_x`, `g_p2_x`) para calcular distancias sin necesidad de buscar en el manager.
- El renderizado usa **ANSI escape codes** para limpiar pantalla y posicionar el cursor.

---

## 🚀 Ideas para extender

- **Power-ups:** Items que congelen enemigos (usar `de_entity_pause`) o den velocidad extra.
- **Oleadas:** Spawnear más enemigos progresivamente usando `de_manager_new` dinámicamente.
- **Destructores:** Agregar `e->destructor` que suene un efecto o spawnee partículas al morir.
- **Más systems:** Un `de_system` para sonido, otro para networking, otro para persistencia.

---

*Hecho con Darken 2.0. Sin malloc, sin drama.* 🏴
