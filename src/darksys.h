#ifndef DARKEN_SYSTEMS_H
#define DARKEN_SYSTEMS_H

#include "darken.h"

typedef void (*de_system_fn)(de_manager *);

/* ============================================================
 * SYSTEM DECLARATION MACROS
 * ============================================================ */

#define DE_SYSTEM(NAME, PAYLOAD_TYPE, CODE) \
    static void NAME(de_manager *_sys_mgr) \
    { \
        de_manager_iterate(_sys_mgr, { \
            PAYLOAD_TYPE *data = (PAYLOAD_TYPE *)ENTITY->data; \
            (void)data; \
            CODE; \
        }); \
    }

#define DE_SYSTEM_ALL(NAME, PAYLOAD_TYPE, CODE) \
    static void NAME(de_manager *_sys_mgr) \
    { \
        de_manager_iterateAll(_sys_mgr, { \
            PAYLOAD_TYPE *data = (PAYLOAD_TYPE *)ENTITY->data; \
            (void)data; \
            CODE; \
        }); \
    }

#define DE_SYSTEM_TAG(NAME, PAYLOAD_TYPE, TAG_VALUE, CODE) \
    static void NAME(de_manager *_sys_mgr) \
    { \
        de_manager_iterate(_sys_mgr, { \
            if (ENTITY->tag == (TAG_VALUE)) { \
                PAYLOAD_TYPE *data = (PAYLOAD_TYPE *)ENTITY->data; \
                (void)data; \
                CODE; \
            } \
        }); \
    }

/* ============================================================
 * PIPELINE
 * ============================================================ */

typedef struct de_pipeline
{
    de_system_fn *fns;
    uint8_t       count;
    uint8_t       capacity;
} de_pipeline;

#define de_pipeline_init(P, FNS_ARRAY, CAP) \
    do { \
        (P)->fns = (FNS_ARRAY); \
        (P)->count = 0; \
        (P)->capacity = (CAP); \
    } while (0)

#define de_pipeline_add(P, FN) \
    do { \
        if ((P)->count < (P)->capacity) \
            (P)->fns[(P)->count++] = (FN); \
    } while (0)

#define de_pipeline_run(P, MGR) \
    do { \
        for (uint8_t _pi = 0; _pi < (P)->count; ++_pi) \
            (P)->fns[_pi](MGR); \
    } while (0)

#endif /* DARKEN_SYSTEMS_H */