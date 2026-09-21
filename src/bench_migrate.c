/* bench_migrate.c — descompone el coste de darken_entity_migrate().
 *
 * baseline_XX      : spawn + delete.
 * inline_copy_XX   : spawn en a + spawn en b + copia manual + delete en b + delete en a.
 * migrate_XX       : spawn en a + darken_entity_migrate(a->b) + delete en b.
 * migrate_only_XX  : pool a lleno, migra a->b en bucle. Aísla migrate de spawn/delete.
 *
 * La copia de inline_copy se materializa con una barrera de compilador (__asm__ volatile con
 * clobber "memory") para que GCC no la elimine como dead store. Todas las entidades pasan por
 * destroy=NULL antes de borrarse.
 */

#include <genesis.h>
#include <stdint.h>
#include "darken-1.3.0_dev.h"




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




#define BENCH_CAP    8
#define BENCH_TICKS  120
#define BENCH_INNER  64

typedef struct { u32 w;          } p04_t;
typedef struct { u32 w, x;       } p08_t;
typedef struct { u32 w, x, y, z; } p16_t;

static DARKEN_DECLARE(sa04, BENCH_CAP, sizeof(p04_t));
static DARKEN_DECLARE(sb04, BENCH_CAP, sizeof(p04_t));
static DARKEN_DECLARE(sa08, BENCH_CAP, sizeof(p08_t));
static DARKEN_DECLARE(sb08, BENCH_CAP, sizeof(p08_t));
static DARKEN_DECLARE(sa16, BENCH_CAP, sizeof(p16_t));
static DARKEN_DECLARE(sb16, BENCH_CAP, sizeof(p16_t));

static volatile u32 g_sink;

static void report(const char *name, u32 ops, u32 ticks)
{
    u32 rate_x100 = ticks ? (ops * 100u) / ticks : 0;
    kprintf("%s: %d ops, %d tk, %d.%02d op/tk",
            name, (int)ops, (int)ticks,
            (int)(rate_x100 / 100u), (int)(rate_x100 % 100u));
}

#define DEFINE_BASELINE(NAME, SA)                      \
static void NAME(void)                                 \
{                                                      \
    darken_t a = DARKEN_BIND(SA);                      \
    darken_init(&a);                                   \
    u32 t0 = getTick(), n = 0;                         \
    u32 end = t0 + BENCH_TICKS;                        \
    u32 acc = 0;                                       \
    while (getTick() < end) {                          \
        for (u16 k = 0; k < BENCH_INNER; k++) {        \
            darken_entity_t e = DARKEN_SPAWN(&a);      \
            if (e) {                                   \
                e->update  = NULL;                     \
                e->destroy = NULL;                     \
                acc += (u32)e->slot;                   \
                darken_entity_delete(e);               \
            }                                          \
        }                                              \
        n++;                                           \
    }                                                  \
    g_sink = acc;                                      \
    report(#NAME, n * BENCH_INNER, getTick() - t0);    \
}

DEFINE_BASELINE(baseline_04, sa04)
DEFINE_BASELINE(baseline_08, sa08)
DEFINE_BASELINE(baseline_16, sa16)

#define DEFINE_INLINE_COPY(NAME, SA, SB)               \
static void NAME(void)                                 \
{                                                      \
    darken_t a = DARKEN_BIND(SA);                      \
    darken_t b = DARKEN_BIND(SB);                      \
    darken_init(&a);                                   \
    darken_init(&b);                                   \
    u32 t0 = getTick(), n = 0;                         \
    u32 end = t0 + BENCH_TICKS;                        \
    u32 acc = 0;                                       \
    while (getTick() < end) {                          \
        for (u16 k = 0; k < BENCH_INNER; k++) {        \
            darken_entity_t e = DARKEN_SPAWN(&a);      \
            darken_entity_t m = DARKEN_SPAWN(&b);      \
            if (e && m) {                              \
                m->update  = NULL;                     \
                m->destroy = NULL;                     \
                uint16_t bytes = a.stride < b.stride ? \
                                 a.stride : b.stride;  \
                uint16_t words = bytes >> 2;           \
                uint32_t *s = (uint32_t *)e;           \
                uint32_t *d = (uint32_t *)m;           \
                while (words--) *d++ = *s++;           \
                __asm__ volatile("" ::: "memory");     \
                acc += ((u32 *)m)[0];                  \
                darken_entity_delete(m);               \
            }                                          \
            if (e) darken_entity_delete(e);            \
        }                                              \
        n++;                                           \
    }                                                  \
    g_sink = acc;                                      \
    report(#NAME, n * BENCH_INNER, getTick() - t0);    \
}

DEFINE_INLINE_COPY(inline_copy_04, sa04, sb04)
DEFINE_INLINE_COPY(inline_copy_08, sa08, sb08)
DEFINE_INLINE_COPY(inline_copy_16, sa16, sb16)

#define DEFINE_MIGRATE(NAME, SA, SB)                   \
static void NAME(void)                                 \
{                                                      \
    darken_t a = DARKEN_BIND(SA);                      \
    darken_t b = DARKEN_BIND(SB);                      \
    darken_init(&a);                                   \
    darken_init(&b);                                   \
    u32 t0 = getTick(), n = 0;                         \
    u32 end = t0 + BENCH_TICKS;                        \
    u32 acc = 0;                                       \
    while (getTick() < end) {                          \
        for (u16 k = 0; k < BENCH_INNER; k++) {        \
            darken_entity_t e = DARKEN_SPAWN(&a);      \
            if (e) {                                   \
                e->update  = NULL;                     \
                e->destroy = NULL;                     \
                darken_entity_t m =                    \
                    darken_entity_migrate(e, &b);      \
                if (m) {                               \
                    __asm__ volatile("" ::: "memory"); \
                    acc += ((u32 *)m)[0];              \
                    darken_entity_delete(m);           \
                }                                      \
            }                                          \
        }                                              \
        n++;                                           \
    }                                                  \
    g_sink = acc;                                      \
    report(#NAME, n * BENCH_INNER, getTick() - t0);    \
}

DEFINE_MIGRATE(migrate_04, sa04, sb04)
DEFINE_MIGRATE(migrate_08, sa08, sb08)
DEFINE_MIGRATE(migrate_16, sa16, sb16)

/* migrate_only: llena `a`, migra todas a `b`, mide el bucle interior.
 * Sin spawn ni delete dentro del bucle medido. Cuando `a` se vacía, se vuelve a llenar.
 * Cada iteración del while interior es UNA llamada a darken_entity_migrate().
 * Migra desde el final del pool (a.pool[a.size - 1]), así el darken_swap final es no-op
 * y lo que mide es el cuerpo + copia, sin ruido de swaps reales. */
#define DEFINE_MIGRATE_ONLY(NAME, SA, SB)                        \
static void NAME(void)                                           \
{                                                                \
    darken_t a = DARKEN_BIND(SA);                                \
    darken_t b = DARKEN_BIND(SB);                                \
    darken_init(&a);                                             \
    darken_init(&b);                                             \
    for (u16 i = 0; i < BENCH_CAP; i++) {                        \
        darken_entity_t e = DARKEN_SPAWN(&a);                    \
        e->update  = NULL;                                       \
        e->destroy = NULL;                                       \
    }                                                            \
    u32 t0 = getTick(), n = 0;                                   \
    u32 end = t0 + BENCH_TICKS;                                  \
    while (getTick() < end) {                                    \
        while (a.size > 0 && b.size < b.capacity) {              \
            darken_entity_migrate(a.pool[a.size - 1], &b);       \
            n++;                                                 \
        }                                                        \
        while (b.size > 0) {                                     \
            darken_entity_delete(b.pool[b.size - 1]);            \
        }                                                        \
        for (u16 i = 0; i < BENCH_CAP; i++) {                    \
            darken_entity_t e = DARKEN_SPAWN(&a);                \
            e->update  = NULL;                                   \
            e->destroy = NULL;                                   \
        }                                                        \
    }                                                            \
    report(#NAME, n, getTick() - t0);                            \
}

DEFINE_MIGRATE_ONLY(migrate_only_04, sa04, sb04)
DEFINE_MIGRATE_ONLY(migrate_only_08, sa08, sb08)
DEFINE_MIGRATE_ONLY(migrate_only_16, sa16, sb16)

void bench_migrate(void)
{
    kprintf("=== migrate breakdown ===");
    kprintf("stride_04: %d", (int)sa04.stride);
    kprintf("stride_08: %d", (int)sa08.stride);
    kprintf("stride_16: %d", (int)sa16.stride);

    baseline_04();   inline_copy_04();   migrate_04();   migrate_only_04();
    baseline_08();   inline_copy_08();   migrate_08();   migrate_only_08();
    baseline_16();   inline_copy_16();   migrate_16();   migrate_only_16();

    kprintf("=== end migrate ===");
}