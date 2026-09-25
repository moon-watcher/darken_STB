#include <genesis.h>
#include "../darken-1.4.0_dev.h"

/*
 * Small standalone DIRECT-mode smoke test.
 *
 * Build this source instead of main.c.
 * DARKEN_DIRECT is relevant when a target cannot safely use the state-machine
 * sentinel convention. The Mega Drive / 68000 does not need this mode, but
 * the test keeps the second callback ABI covered.
 */

typedef struct direct_payload
{
    u16 value;
} direct_payload;

static u16 failures;
static u16 updates;
static u16 destroys;

static void check(u16 condition, const char *name)
{
    if (condition)
        kprintf("PASS: %s", name);
    else
    {
        kprintf("FAIL: %s", name);
        failures++;
    }
}

static void direct_update(darken_entity_t entity, direct_payload *data)
{
    updates++;
    data->value++;

    if (data->value == 3)
        entity->update = 0;
}

static void direct_destroy(darken_entity_t entity, direct_payload *data)
{
    (void)entity;
    destroys++;
    data->value = 0xD00D;
}

DARKEN_DECLARE(storage, 4, sizeof(direct_payload));

int darken_sgdk_tests_direct(bool hardReset)
{
    darken_t ctx;
    darken_entity_t entity;
    direct_payload *data;

    (void)hardReset;

    ctx = DARKEN_BIND(storage);
    darken_init(&ctx);

    entity = DARKEN_SPAWN(&ctx);
    data = (direct_payload *)entity->data;

    entity->update = (darken_state_t)direct_update;
    entity->destroy = (darken_state_t)direct_destroy;
    data->value = 1;

    darken_update(&ctx);
    check(updates == 1 && data->value == 2, "direct update");

    darken_entity_delete(entity);
    check(destroys == 1, "direct destroy");
    check(ctx.size == 0, "direct delete");

    kprintf("DIRECT TEST %s", failures ? "FAILED" : "PASSED");

    while (1)
        SYS_doVBlankProcess();

    return 0;
}
