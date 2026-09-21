/* bench_migrate_variants.c — compara u8 / u16 / u32 sin violar la alineación.
 * Solo mide el bucle de copia de darken_entity_migrate; el resto de la función (spawn/delete) es igual.
 */

#include <genesis.h>
#include <stdint.h>
#include "darken-1.3.0_dev.h"

#define BENCH_CAP     64
#define BENCH_TICKS   120
#define TAIL_STRIDE   22  /* par, no múltiplo de 4: fuerza el tail del u32 (bytes & 3 == 2) */

typedef struct {
    s16 x, y;
    s16 vx, vy;
    u16 hp;
    u16 anim;
    u16 state;
    u16 timer;
} bench_payload;  /* 16 bytes */

static DARKEN_DECLARE(bench_storage_a, BENCH_CAP, sizeof(bench_payload));
static DARKEN_DECLARE(bench_storage_b, BENCH_CAP, sizeof(bench_payload));
static darken_t g_a, g_b;

/* Copias puras: solo el bucle, sin spawn/delete/checks. Medimos lo que queremos medir. */






/* Copia byte a byte. Referencia. Sin supuestos de alineación. */
static inline darken_entity_t migrate_u8(darken_entity_t entity, darken_t *dst)
{
    darken_t *src = entity->owner;

    if (src == dst || dst->size >= dst->capacity)
        return 0;

    uint16_t active = DARKEN_ENTITY_IS_ACTIVE(entity);
    uint16_t dst_slot = dst->size++;
    darken_entity_t moved = dst->pool[dst_slot];

    uint8_t *s = (uint8_t *)entity;
    uint8_t *d = (uint8_t *)moved;
    uint16_t i = src->stride < dst->stride ? src->stride : dst->stride;

    while (i--)
        d[i] = s[i];

    moved->slot = dst_slot;
    moved->owner = dst;

    if (active)
        darken_swap(src, entity->slot, --src->size);

    return moved;
}

/* Copia palabra a palabra. Requiere alineación >= 2 en ambos punteros (garantizada por el header). */
static inline darken_entity_t migrate_u16(darken_entity_t entity, darken_t *dst)
{
    darken_t *src = entity->owner;

    if (src == dst || dst->size >= dst->capacity)
        return 0;

    uint16_t active = DARKEN_ENTITY_IS_ACTIVE(entity);
    uint16_t dst_slot = dst->size++;
    darken_entity_t moved = dst->pool[dst_slot];

    uint16_t bytes = src->stride < dst->stride ? src->stride : dst->stride;
    uint16_t words = bytes >> 1;

    uint16_t *s = (uint16_t *)entity;
    uint16_t *d = (uint16_t *)moved;

    while (words--)
        *d++ = *s++;

    if (bytes & 1)
        *(uint8_t *)d = *(uint8_t *)s;

    moved->slot = dst_slot;
    moved->owner = dst;

    if (active)
        darken_swap(src, entity->slot, --src->size);

    return moved;
}

/* Copia doble palabra a doble palabra. REQUIERE alineación >= 4. En 68000 con _DARKEN_ENTITY_ALIGN==2
   puede provocar address error. En 68020+ sin problema. */
static inline darken_entity_t migrate_u32(darken_entity_t entity, darken_t *dst)
{
    darken_t *src = entity->owner;

    if (src == dst || dst->size >= dst->capacity)
        return 0;

    uint16_t active = DARKEN_ENTITY_IS_ACTIVE(entity);
    uint16_t dst_slot = dst->size++;
    darken_entity_t moved = dst->pool[dst_slot];

    uint16_t bytes = src->stride < dst->stride ? src->stride : dst->stride;
    uint16_t words = bytes >> 2;
    uint16_t tail = bytes & 3;

    uint32_t *s = (uint32_t *)entity;
    uint32_t *d = (uint32_t *)moved;

    while (words--)
        *d++ = *s++;

    uint8_t *sb = (uint8_t *)s;
    uint8_t *db = (uint8_t *)d;
    while (tail--)
        *db++ = *sb++;

    moved->slot = dst_slot;
    moved->owner = dst;

    if (active)
        darken_swap(src, entity->slot, --src->size);

    return moved;
}

// Note: darken_entity_delete() only calls destroy() if the entity is active.
// destroy() must not mutate the ctx's pool zones (delete/spawn) while it runs -- see the big header comment
// above.
static inline void darken_entity_delete(darken_entity_t entity)
{
    if (DARKEN_ENTITY_IS_FREE(entity))
        return;

    if (entity->destroy)
        entity->destroy(_DARKEN_ARGS(entity));

    darken_swap(entity->owner, entity->slot, --entity->owner->size);
}


static void copy_u8(void *dst, const void *src, u16 bytes)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (bytes--)
        *d++ = *s++;
}

static void copy_u16(void *dst, const void *src, u16 bytes)
{
    u16 *d = (u16 *)dst;
    const u16 *s = (const u16 *)src;
    u16 n = bytes >> 1;
    while (n--)
        *d++ = *s++;
    if (bytes & 1)
        *(u8 *)d = *(const u8 *)s;
}

static void copy_u32(void *dst, const void *src, u16 bytes)
{
    u32 *d = (u32 *)dst;
    const u32 *s = (const u32 *)src;
    u16 n = bytes >> 2;
    while (n--)
        *d++ = *s++;
    u8 *db = (u8 *)d;
    const u8 *sb = (const u8 *)s;
    u16 tail = bytes & 3;
    while (tail--)
        *db++ = *sb++;
}

static void report(const char *name, u32 ops, u32 ticks)
{
    u32 rate_x100 = ticks ? (ops * 100u) / ticks : 0;
    kprintf("%s: %d ops, %d tk, %d.%02d op/tk",
            name, (int)ops, (int)ticks,
            (int)(rate_x100 / 100u), (int)(rate_x100 % 100u));
}

typedef void (*copy_fn)(void *, const void *, u16);

static void bench_copy(const char *name, copy_fn fn, u16 bytes)
{
    u32 t0 = getTick(), n = 0;
    u32 end = t0 + BENCH_TICKS;
    while (getTick() < end) {
        for (u16 i = 0; i < BENCH_CAP; i++)
            fn(bench_storage_b.data + i * 40, bench_storage_a.data + i * 40, bytes);
        n++;
    }
    report(name, n * BENCH_CAP, getTick() - t0);
}

void bench_migrate_variants(void)
{
    kprintf("=== copy variants ===");

    /* 40 bytes: natural stride de un darken_entity_t + 16B payload, alineado a 4.
       Múltiplo de 4: u16 y u32 van por el camino rápido, sin tail. */
    bench_copy("copy_u8_40",  copy_u8,  40);
    bench_copy("copy_u16_40", copy_u16, 40);
    bench_copy("copy_u32_40", copy_u32, 40);

    /* 38 bytes: mismo stride útil pero "recortado". 38 & 3 == 2, 38 & 1 == 0.
       Ejercita el tail del u32 (2 bytes) sin romper alineación. El u16 no tiene tail (bytes par). */
    bench_copy("copy_u8_38",  copy_u8,  38);
    bench_copy("copy_u16_38", copy_u16, 38);
    bench_copy("copy_u32_38", copy_u32, 38);

    /* 34 bytes: 34 & 3 == 2 también; útil como segundo punto de tail del u32. */
    bench_copy("copy_u32_34", copy_u32, 34);

    kprintf("=== end variants ===");
}