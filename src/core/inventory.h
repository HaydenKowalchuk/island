#ifndef ISLAND_CORE_INVENTORY_H
#define ISLAND_CORE_INVENTORY_H

#include <stdint.h>

/*
 * Player inventory (island1-iim2): fixed static storage, no malloc — the PSP
 * build keeps everything in BSS. Slots stack up to INV_STACK_MAX; the future
 * trading NPC reads inv_slot() directly and removes what you buy with it.
 */

#define INV_MAX 16
#define INV_STACK_MAX 99

typedef struct {
    uint16_t id;    /* ItemId; 0 = empty slot */
    uint16_t count; /* 1..INV_STACK_MAX */
} InvSlot;

void inv_init(void); /* reset to empty (game_init) */
void inv_clear(void);

/* Add up to `count`; fills the matching stack first, then empty slots.
 * Returns how many were actually stored (may be less on overflow). */
int inv_add(int id, int count);

/* Remove exactly `count` or nothing: returns 1 on success, 0 when the
 * inventory holds fewer than requested. */
int inv_remove(int id, int count);

/* Total units of `id` across all stacks. */
int inv_count(int id);

/* i-th occupied slot in index order, or NULL past the end (HUD iteration). */
const InvSlot *inv_slot(int i);

/* Number of occupied slots. */
int inv_used(void);

#endif
