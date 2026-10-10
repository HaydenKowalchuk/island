#ifndef ISLAND_CORE_RESIDENTS_H
#define ISLAND_CORE_RESIDENTS_H

/*
 * Data-driven NPC dialogue tables (island1-9fm3). Pure data: no state, no
 * gameplay. Every island resident has a unique voice so conversations carry
 * the story (the lighthouse, the storm that took the pier, hints at what the
 * parcels mean to the island); the depot keeper owns the hand-off chatter.
 *
 * Storage is static and outlives dialog_start(), which keeps pointers into
 * these tables for the lifetime of a conversation. Line counts must stay
 * within DIALOG_MAX_LINES (dialog.h); the arrays are NULL-terminated so a
 * speaker may use fewer slots.
 */

typedef struct {
  const char* name;
  const char* lines_idle[4];       /* casual talk, any quest phase */
  const char* lines_delivery[4];   /* the target resident, package in hand */
  const char* lines_no_package[3]; /* the target resident, package not picked up */
} Dialogue;

/* Number of resident dialogues; game.c asserts this matches HOUSE_COUNT. */
#define RESIDENT_COUNT 4

/* Resident `i` (0..RESIDENT_COUNT-1), one unique voice per house. */
const Dialogue* resident_dialogue(int i);

/* Depot keeper: lines_idle holds the package hand-off chatter. */
const Dialogue* depot_dialogue(void);

#endif
