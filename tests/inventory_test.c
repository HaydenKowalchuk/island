/* GL-free test for the item registry + player inventory (island1-iim2). */
#include <assert.h>
#include <stdio.h>

#include "inventory.h"
#include "items.h"

int main(void) {
    /* Registry sanity: every id has a def with a name, ids match slots,
     * nothing is both quest-only and sellable, and the sentinel edges hold. */
    const ItemDef *none = item_def(ITEM_NONE);
    assert(none != 0 && none->id == ITEM_NONE);
    for (int id = 1; id < ITEM_TOTAL; id++) {
        const ItemDef *d = item_def(id);
        assert(d != 0);
        assert(d->id == id);
        assert(d->name != 0 && d->name[0] != '\0');
        assert(!(d->flags & ITEMF_QUEST) || !(d->flags & ITEMF_SELLABLE));
        assert(d->flags & ITEMF_STACKABLE);
    }
    assert(item_def(ITEM_TOTAL) == 0);
    assert(item_def(-1) == 0);

    /* Empty inventory. */
    inv_init();
    assert(inv_used() == 0);
    assert(inv_count(ITEM_FISH) == 0);

    /* Basic add / count. */
    assert(inv_add(ITEM_FISH, 5) == 5);
    assert(inv_count(ITEM_FISH) == 5);
    assert(inv_used() == 1);

    /* Same item merges into the existing stack. */
    assert(inv_add(ITEM_FISH, 7) == 7);
    assert(inv_count(ITEM_FISH) == 12);
    assert(inv_used() == 1);

    /* Past INV_STACK_MAX the remainder spills into a second slot. */
    assert(inv_add(ITEM_FISH, 95) == 95); /* 12 + 95 = 107 -> 99 + 8 */
    assert(inv_count(ITEM_FISH) == 107);
    assert(inv_used() == 2);

    /* Remove draws down across stacks. */
    assert(inv_remove(ITEM_FISH, 100) == 1);
    assert(inv_count(ITEM_FISH) == 7);

    /* Not enough: all-or-nothing, nothing removed. */
    assert(inv_remove(ITEM_FISH, 8) == 0);
    assert(inv_count(ITEM_FISH) == 7);

    /* Full-capacity overflow: 16 stacks * 99 fills every slot, next fails. */
    inv_clear();
    assert(inv_add(ITEM_SALT, INV_MAX * INV_STACK_MAX) ==
           INV_MAX * INV_STACK_MAX);
    assert(inv_used() == INV_MAX);
    assert(inv_add(ITEM_SALT, 1) == 0);
    assert(inv_remove(ITEM_SALT, INV_MAX * INV_STACK_MAX) == 1);
    assert(inv_used() == 0);

    /* Slot iteration walks occupied slots only, NULL past the end. */
    inv_clear();
    assert(inv_add(ITEM_SALT, 1) == 1);
    assert(inv_add(ITEM_FISH, 2) == 2);
    assert(inv_add(ITEM_LAMP_OIL, 3) == 3);
    assert(inv_used() == 3);
    int seen = 0;
    int sum = 0;
    for (const InvSlot *s = inv_slot(0); s != 0; s = inv_slot(seen)) {
        seen++;
        sum += s->count;
        assert(seen <= INV_MAX);
        if (seen == 1) {
            assert(s->id == ITEM_SALT && s->count == 1);
        } else if (seen == 2) {
            assert(s->id == ITEM_FISH && s->count == 2);
        } else {
            assert(s->id == ITEM_LAMP_OIL && s->count == 3);
        }
    }
    assert(seen == 3);
    assert(sum == 6);
    assert(inv_slot(-1) == 0);

    /* Bad args are no-ops. */
    assert(inv_add(0, 5) == 0);
    assert(inv_add(ITEM_FISH, 0) == 0);
    assert(inv_remove(ITEM_FISH, 0) == 0);
    assert(inv_remove(ITEM_NONE, 1) == 0);

    printf("inventory: registry + slots OK\n");
    return 0;
}
