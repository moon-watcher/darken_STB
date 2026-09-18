// Prueba rápida de las macros de storage que la demo principal no ejercita:
// DARKEN_ALLOC/DARKEN_FREE (dinámico, sobre MEM_alloc/MEM_free de SGDK) y
// DARKEN_INIT (estático/global, constantes de compilación). DARKEN_DECLARE+
// DARKEN_BIND ya se prueban en user_demo.c.
#include <genesis.h>

typedef u32 uintptr_t; // SGDK no lo define; en m68k un puntero mide 32 bits

#define Z_A 0
#define Z_B 1
#define GAME_ZONES 2

#include "darken2.h"

struct payload
{
    s16 v;
};

static void *state_a(void *data)
{
    struct payload *p = data;
    p->v++;
    kprintf("  A slot=%d v=%d", DARKEN_ENTITY(data)->slot, p->v);
    return (void *)(uintptr_t)Z_A;
}

// --- Camino DARKEN_INIT: storage estático/global con constantes de compilación ---
static DARKEN_DECLARE(g_storage, 4, GAME_ZONES, sizeof(struct payload));
static darken_t g_ctx = DARKEN_INIT(g_storage, GAME_ZONES);

static void test_init_path(void)
{
    darken_init(&g_ctx);
    darken_entity_t e = DARKEN_SPAWN(&g_ctx, Z_A);
    e->update = state_a;
    ((struct payload *)e->data)->v = 0;

    kprintf("[INIT] capacity=%d stride=%d zones=%d", g_ctx.capacity, g_ctx.stride, g_ctx.zones);
    DARKEN_FOREACH(&g_ctx, Z_A, {
        darken_state_t next = _entity->update(_entity->data);
        darken_entity_set_zone(_entity->data, (int)(uintptr_t)next);
    });
}

// --- Camino DARKEN_ALLOC/DARKEN_FREE: asignación dinámica sobre MEM_alloc/MEM_free ---
static void test_alloc_path(void)
{
    darken_t m = DARKEN_ALLOC(MEM_alloc, 4, GAME_ZONES, sizeof(struct payload));
    if (!m.pool || !m.storage)
    {
        kprintf("[ALLOC] fallo de asignacion");
        return;
    }
    darken_init(&m);

    darken_entity_t e = DARKEN_SPAWN(&m, Z_B);
    e->update = state_a;
    ((struct payload *)e->data)->v = 10;

    kprintf("[ALLOC] capacity=%d stride=%d zones=%d free_zone=%d", m.capacity, m.stride, m.zones, darken_free_zone(&m));
    kprintf("[ALLOC] zona de e antes de mover: %d", darken_entity_zone(e));
    darken_entity_set_zone(e->data, darken_free_zone(&m));
    kprintf("[ALLOC] zona de e despues de mover a libre: %d", darken_entity_zone(e));

    DARKEN_FREE(MEM_free, &m);
    kprintf("[ALLOC] liberado sin problemas");
}

int darken2_storage_smoke_test_main(bool hardReset)
{
    test_init_path();
    test_alloc_path();

    return 0;
}