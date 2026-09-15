#include <stdint.h>
#include "darksys-1.0.0_dev.h"

#define CAPACITY 8
#define PARAMS 4

DARKSYS_DECLARE(storage, CAPACITY, PARAMS);
static darksys_t system;

#define assert(x) \
    if (!(x))     \
        kprintf("ERROR: %s", #x);

static void test_add_data(void)
{
    int a = 10;
    int b = 20;
    int c = 30;
    int d = 40;

    system = DARKSYS_BIND(storage);

    darksys_handle_t h = DARKSYS_ADD(&system, &a, &b, &c, &d);

    assert(h == 0);
    assert(system.count == 1);
    assert(darksys_valid(&system, h));

    darksys_data_t row = DARKSYS_DATA(&system, h);

    assert(*(int *)row[0] == 10);
    assert(*(int *)row[1] == 20);
    assert(*(int *)row[2] == 30);
    assert(*(int *)row[3] == 40);

    kprintf("ADD/DATA OK");
}

static void test_foreach(void)
{
    int x0 = 1;
    int y0 = 2;
    int vx0 = 3;
    int vy0 = 4;

    int x1 = 10;
    int y1 = 20;
    int vx1 = 30;
    int vy1 = 40;

    system = DARKSYS_BIND(storage);

    DARKSYS_ADD(&system, &x0, &y0, &vx0, &vy0);
    DARKSYS_ADD(&system, &x1, &y1, &vx1, &vy1);

    DARKSYS_FOREACH(&system, int *x, int *y, int *vx, int *vy, {
        *x += *vx;
        *y += *vy;
    });

    assert(x0 == 4);
    assert(y0 == 6);
    assert(x1 == 40);
    assert(y1 == 60);

    kprintf("FOREACH OK");
}

static void test_remove_middle(void)
{
    int a0 = 10;
    int a1 = 11;
    int a2 = 12;
    int a3 = 13;

    int b0 = 20;
    int b1 = 21;
    int b2 = 22;
    int b3 = 23;

    int c0 = 30;
    int c1 = 31;
    int c2 = 32;
    int c3 = 33;

    darksys_handle_t a;
    darksys_handle_t b;
    darksys_handle_t c;

    system = DARKSYS_BIND(storage);

    a = DARKSYS_ADD(&system, &a0, &a1, &a2, &a3);
    b = DARKSYS_ADD(&system, &b0, &b1, &b2, &b3);
    c = DARKSYS_ADD(&system, &c0, &c1, &c2, &c3);

    darksys_remove(&system, a);

    assert(system.count == 2);
    assert(darksys_valid(&system, b));
    assert(!darksys_valid(&system, a));
    assert(darksys_valid(&system, c));

    darksys_data_t rc = DARKSYS_DATA(&system, c);
    darksys_data_t rb = DARKSYS_DATA(&system, b);

    assert(*(int *)rc[0] == 30);
    assert(*(int *)rc[1] == 31);
    assert(*(int *)rc[2] == 32);
    assert(*(int *)rc[3] == 33);

    assert(*(int *)rb[0] == 20);
    assert(*(int *)rb[1] == 21);
    assert(*(int *)rb[2] == 22);
    assert(*(int *)rb[3] == 23);

    kprintf("REMOVE OK");
}

static void test_handle_reuse(void)
{
    int a0 = 1;
    int a1 = 2;
    int a2 = 3;
    int a3 = 4;

    int b0 = 5;
    int b1 = 6;
    int b2 = 7;
    int b3 = 8;

    int c0 = 9;
    int c1 = 10;
    int c2 = 11;
    int c3 = 12;

    system = DARKSYS_BIND(storage);

    darksys_handle_t a = DARKSYS_ADD(&system, &a0, &a1, &a2, &a3);
    darksys_handle_t b = DARKSYS_ADD(&system, &b0, &b1, &b2, &b3);

    darksys_remove(&system, a);

    assert(!darksys_valid(&system, a));
    assert(darksys_valid(&system, b));

    darksys_handle_t c = DARKSYS_ADD(&system, &c0, &c1, &c2, &c3);

    assert(c == a);
    assert(system.count == 2);
    assert(darksys_valid(&system, c));
    assert(darksys_valid(&system, b));

    darksys_data_t rc = DARKSYS_DATA(&system, c);

    assert(*(int *)rc[0] == 9);
    assert(*(int *)rc[1] == 10);
    assert(*(int *)rc[2] == 11);
    assert(*(int *)rc[3] == 12);

    kprintf("REUSE OK");
}

static void test_data_write(void)
{
    int a0 = 10;
    int a1 = 20;
    int a2 = 30;
    int a3 = 40;

    int b0 = 100;
    int b1 = 200;
    int b2 = 300;
    int b3 = 400;

    system = DARKSYS_BIND(storage);

    darksys_handle_t h = DARKSYS_ADD(&system, &a0, &a1, &a2, &a3);

    darksys_data_t row = DARKSYS_DATA(&system, h);

    *(int *)row[0] = 50;
    *(int *)row[1] = 60;
    *(int *)row[2] = 70;
    *(int *)row[3] = 80;

    assert(a0 == 50);
    assert(a1 == 60);
    assert(a2 == 70);
    assert(a3 == 80);

    darksys_handle_t h2 = DARKSYS_ADD(&system, &b0, &b1, &b2, &b3);

    darksys_data_t row2 = DARKSYS_DATA(&system, h2);

    assert(*(int *)row2[0] == 100);
    assert(*(int *)row2[1] == 200);
    assert(*(int *)row2[2] == 300);
    assert(*(int *)row2[3] == 400);

    kprintf("DATA WRITE OK");
}

static void test_clear(void)
{
    int a = 1;
    int b = 2;
    int c = 3;
    int d = 4;

    system = DARKSYS_BIND(storage);

    DARKSYS_ADD(&system, &a, &b, &c, &d);

    assert(system.count == 1);

    darksys_clear(&system);

    assert(system.count == 0);
    assert(system.next == 0);
    assert(system.free_head == DARKSYS_INVALID_HANDLE);

    darksys_handle_t h = DARKSYS_ADD(&system, &a, &b, &c, &d);

    assert(h == 0);
    assert(system.count == 1);
    assert(darksys_valid(&system, h));

    kprintf("CLEAR OK");
}

int test_darksys(void)
{
    test_add_data();
    test_foreach();
    test_remove_middle();
    test_handle_reuse();
    test_data_write();
    test_clear();

    kprintf("ALL TESTS OK");
    return 0;
}
