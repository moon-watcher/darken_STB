// #include <genesis.h>

// #include "dsmap2.h"


// /* ================================================================
//  * OBJECT
//  * ================================================================ */

// typedef struct
// {
//     uint16_t x;
//     uint16_t y;
//     uint16_t vx;
//     uint16_t vy;

// } Entity;


// /* ================================================================
//  * STORAGE
//  * ================================================================ */

// #define MAX_ENTITIES 8

// DSMAP2_DECLARE(
//     storage_dsmap2,
//     MAX_ENTITIES,
//     Entity
// );


// /* ================================================================
//  * TEST
//  * ================================================================ */

// void test_dsmap2(void)
// {
//     dsmap2_t map;

//     dsmap2_handle_t a;
//     dsmap2_handle_t b;
//     dsmap2_handle_t c;
//     dsmap2_handle_t d;

//     Entity *entity;


//     /* ------------------------------------------------------------
//      * Initialize
//      * ------------------------------------------------------------ */

//     map = (dsmap2_t)DSMAP2_INIT(
//         storage_dsmap2,
//         Entity
//     );

//     dsmap2_init(&map);


//     kprintf(
//         "DSMAP2 INIT capacity=%u size=%u count=%u",
//         map.capacity,
//         map.size,
//         map.count
//     );


//     /* ------------------------------------------------------------
//      * Allocate
//      * ------------------------------------------------------------ */

//     a = dsmap2_alloc(&map);
//     b = dsmap2_alloc(&map);
//     c = dsmap2_alloc(&map);


//     kprintf(
//         "ALLOC a=%u b=%u c=%u count=%u",
//         a,
//         b,
//         c,
//         map.count
//     );


//     /* ------------------------------------------------------------
//      * Write data
//      * ------------------------------------------------------------ */

//     entity = dsmap2_data(&map, a);

//     entity->x = 10;
//     entity->y = 20;


//     entity = dsmap2_data(&map, b);

//     entity->x = 30;
//     entity->y = 40;


//     entity = dsmap2_data(&map, c);

//     entity->x = 50;
//     entity->y = 60;


//     /* ------------------------------------------------------------
//      * Read B
//      * ------------------------------------------------------------ */

//     entity = dsmap2_data(&map, b);

//     kprintf(
//         "B x=%u y=%u",
//         entity->x,
//         entity->y
//     );


//     /* ------------------------------------------------------------
//      * Remove B
//      *
//      * Object B stays physically where it was.
//      * Only active[] changes.
//      * ------------------------------------------------------------ */

//     dsmap2_remove(
//         &map,
//         b
//     );


//     kprintf(
//         "REMOVE B valid=%u count=%u",
//         dsmap2_valid(&map, b),
//         map.count
//     );


//     /* ------------------------------------------------------------
//      * C must remain valid.
//      * ------------------------------------------------------------ */

//     entity = dsmap2_data(
//         &map,
//         c
//     );

//     kprintf(
//         "C valid=%u x=%u y=%u",
//         dsmap2_valid(&map, c),
//         entity->x,
//         entity->y
//     );


//     /* ------------------------------------------------------------
//      * Dump active list
//      * ------------------------------------------------------------ */

//     for (uint16_t i = 0;
//          i < map.count;
//          i++)
//     {
//         dsmap2_handle_t handle =
//             dsmap2_active(&map, i);

//         entity = dsmap2_data(
//             &map,
//             handle
//         );

//         kprintf(
//             "slot=%u handle=%u x=%u y=%u",
//             i,
//             handle,
//             entity->x,
//             entity->y
//         );
//     }


//     /* ------------------------------------------------------------
//      * Reuse handle B
//      * ------------------------------------------------------------ */

//     d = dsmap2_alloc(&map);

//     entity = dsmap2_data(
//         &map,
//         d
//     );

//     entity->x = 70;
//     entity->y = 80;


//     kprintf(
//         "REUSE handle=%u count=%u x=%u y=%u",
//         d,
//         map.count,
//         entity->x,
//         entity->y
//     );


//     /* ------------------------------------------------------------
//      * Final validation
//      * ------------------------------------------------------------ */

//     kprintf(
//         "FINAL a=%u b=%u c=%u d=%u",
//         dsmap2_valid(&map, a),
//         dsmap2_valid(&map, b),
//         dsmap2_valid(&map, c),
//         dsmap2_valid(&map, d)
//     );
// }
