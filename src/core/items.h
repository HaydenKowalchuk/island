#ifndef ISLAND_CORE_ITEMS_H
#define ISLAND_CORE_ITEMS_H

#include <stdint.h>

/*
 * Item registry (island1-iim2). Pure data: every stackable thing that can
 * ride in a parcel, sit in the player inventory, or be traded by the future
 * shop NPC. IDs are stable save-file/quest-script vocabulary — append only.
 *
 *   ITEMF_QUEST      story/turn-in item; the shop will never buy it
 *   ITEMF_SELLABLE   the trading NPC accepts it
 *   ITEMF_STACKABLE  stacks in inventory (all current items do)
 */

typedef enum {
    ITEM_NONE = 0,
    /* quest / story */
    ITEM_BRASS_KEY,   /* the lighthouse key somebody lost */
    ITEM_RADIO_TUBE,  /* TAMSIN's impossible station */
    ITEM_FUSE,        /* depot spares */
    ITEM_PIGMENT,     /* ORIN ran out of blue */
    ITEM_NET_TWINE,   /* MARLOWE's mending twine */
    ITEM_MEDICINE,    /* shore-house prescription */
    ITEM_LETTER,      /* sealed, not ours to read */
    ITEM_BRASS_GEAR,  /* a toy that needs one */
    ITEM_CHART,       /* reward: hand-drawn island chart */
    /* trade goods (the future shop) */
    ITEM_FISH,        /* smoked fish */
    ITEM_BOLT_CLOTH,  /* a bolt of cloth */
    ITEM_SALT,        /* sea salt */
    ITEM_LAMP_OIL,    /* lamp oil */
    ITEM_SEASHELL,    /* polished shell trinket */
    ITEM_TOTAL
} ItemId;

#define ITEMF_QUEST 0x01
#define ITEMF_SELLABLE 0x02
#define ITEMF_STACKABLE 0x04

typedef struct {
    uint16_t id; /* ItemId */
    const char *name;
    const char *desc;
    uint16_t flags;
} ItemDef;

/* NULL when id is out of range (id 0 is the legitimate ITEM_NONE entry). */
const ItemDef *item_def(int id);

#endif
