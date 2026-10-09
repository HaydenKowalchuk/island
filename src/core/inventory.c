#include <stddef.h>

#include "inventory.h"

static InvSlot s_slots[INV_MAX];

void inv_init(void) { inv_clear(); }

void inv_clear(void) {
    for (int i = 0; i < INV_MAX; i++) {
        s_slots[i].id = 0;
        s_slots[i].count = 0;
    }
}

int inv_add(int id, int count) {
    if (id <= 0 || count <= 0)
        return 0;
    int stored = 0;

    /* Top up existing stacks first. */
    for (int i = 0; i < INV_MAX && stored < count; i++) {
        if (s_slots[i].id != (uint16_t)id)
            continue;
        int room = INV_STACK_MAX - s_slots[i].count;
        if (room <= 0)
            continue;
        int put = count - stored;
        if (put > room)
            put = room;
        s_slots[i].count = (uint16_t)(s_slots[i].count + put);
        stored += put;
    }

    /* Then claim empty slots. */
    for (int i = 0; i < INV_MAX && stored < count; i++) {
        if (s_slots[i].id != 0)
            continue;
        int put = count - stored;
        if (put > INV_STACK_MAX)
            put = INV_STACK_MAX;
        s_slots[i].id = (uint16_t)id;
        s_slots[i].count = (uint16_t)put;
        stored += put;
    }
    return stored;
}

int inv_remove(int id, int count) {
    if (id <= 0 || count <= 0)
        return 0;
    if (inv_count(id) < count)
        return 0;
    int left = count;
    for (int i = 0; i < INV_MAX && left > 0; i++) {
        if (s_slots[i].id != (uint16_t)id)
            continue;
        int take = (s_slots[i].count < left) ? s_slots[i].count : left;
        s_slots[i].count = (uint16_t)(s_slots[i].count - take);
        left -= take;
        if (s_slots[i].count == 0) {
            s_slots[i].id = 0;
            s_slots[i].count = 0;
        }
    }
    return 1;
}

int inv_count(int id) {
    if (id <= 0)
        return 0;
    int total = 0;
    for (int i = 0; i < INV_MAX; i++)
        if (s_slots[i].id == (uint16_t)id)
            total += s_slots[i].count;
    return total;
}

const InvSlot *inv_slot(int i) {
    if (i < 0)
        return NULL;
    int seen = 0;
    for (int k = 0; k < INV_MAX; k++) {
        if (s_slots[k].id == 0)
            continue;
        if (seen == i)
            return &s_slots[k];
        seen++;
    }
    return NULL;
}

int inv_used(void) {
    int used = 0;
    for (int i = 0; i < INV_MAX; i++)
        if (s_slots[i].id != 0)
            used++;
    return used;
}
