#include "items.h"

#include <stddef.h>

static const ItemDef s_items[ITEM_TOTAL] = {
    {ITEM_NONE, "NONE", "", 0},
    {ITEM_BRASS_KEY, "BRASS KEY", "Cold from the lighthouse door.", ITEMF_QUEST | ITEMF_STACKABLE},
    {ITEM_RADIO_TUBE, "RADIO TUBE", "Hums when nobody is listening.", ITEMF_QUEST | ITEMF_STACKABLE},
    {ITEM_FUSE, "SPARE FUSE", "Depot spares, rated for the beacon.", ITEMF_QUEST | ITEMF_STACKABLE},
    {ITEM_PIGMENT, "BLUE PIGMENT", "The last good blue on the island.", ITEMF_QUEST | ITEMF_STACKABLE},
    {ITEM_NET_TWINE, "NET TWINE", "Tarred twine for mending nets.", ITEMF_QUEST | ITEMF_STACKABLE},
    {ITEM_MEDICINE, "MEDICINE", "Shore-house prescription.", ITEMF_QUEST | ITEMF_STACKABLE},
    {ITEM_LETTER, "SEALED LETTER", "Addressed, stamped, not ours to read.", ITEMF_QUEST | ITEMF_STACKABLE},
    {ITEM_BRASS_GEAR, "BRASS GEAR", "One tooth missing. It matters.", ITEMF_QUEST | ITEMF_STACKABLE},
    {ITEM_CHART, "ISLAND CHART", "Hand-drawn, soundings included.", ITEMF_QUEST | ITEMF_STACKABLE},
    {ITEM_FISH, "SMOKED FISH", "Keeps without ice.", ITEMF_SELLABLE | ITEMF_STACKABLE},
    {ITEM_BOLT_CLOTH, "BOLT OF CLOTH", "Dyed in the mainland style.", ITEMF_SELLABLE | ITEMF_STACKABLE},
    {ITEM_SALT, "SEA SALT", "Raked from the flats.", ITEMF_SELLABLE | ITEMF_STACKABLE},
    {ITEM_LAMP_OIL, "LAMP OIL", "Burns steady, smells like home.", ITEMF_SELLABLE | ITEMF_STACKABLE},
    {ITEM_SEASHELL, "SEASHELL", "Polished by the tide.", ITEMF_SELLABLE | ITEMF_STACKABLE},
};

const ItemDef* item_def(int id) {
  if (id < 0 || id >= ITEM_TOTAL)
    return NULL;
  return &s_items[id];
}
