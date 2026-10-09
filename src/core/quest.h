#ifndef ISLAND_CORE_QUEST_H
#define ISLAND_CORE_QUEST_H

#include <stdint.h>

/*
 * Data-driven quest chains (island1-f9x2). A chain is a table of ordered
 * steps; the world (game.c) reports what happened through quest_notify_*
 * and reads the current step to decide what E does, where the beacon points,
 * and what the HUD says. Chain tables are static data that outlive play —
 * quest.c owns progression, game.c owns the world.
 *
 *   QSTEP_PICKUP   collect the parcel at the depot (E in range)
 *   QSTEP_DELIVER  sealed hand-off: talk to step target while carrying
 *   QSTEP_TALK     open a conversation with the step target
 *   QSTEP_CLAIM    lost parcel at the depot: E claims it into the inventory
 *   QSTEP_GRANT    instant: item into the inventory, chain moves on
 *
 * Step target encoding: >=0 is a fixed resident index; -1 on a DELIVER step
 * picks a random house != the last delivered one when the chain starts; -2
 * also avoids the chain's first pick (a second dynamic delivery).
 */

typedef enum {
    QSTEP_PICKUP,
    QSTEP_DELIVER,
    QSTEP_TALK,
    QSTEP_CLAIM,
    QSTEP_GRANT,
} QuestStepType;

typedef struct {
    uint8_t type;      /* QuestStepType */
    int8_t target;     /* resident index, or -1/-2 dynamic (see above) */
    uint16_t item;     /* PICKUP/CLAIM payload, GRANT gift (0 = default) */
    uint16_t count;
    const char *prompt; /* HUD objective line; NULL = none */
} QuestStep;

typedef struct {
    const char *name;
    const QuestStep *steps;
    uint8_t step_count;
    uint16_t reward_item;  /* granted into the inventory at chain end */
    uint16_t reward_count;
} QuestChain;

void quest_init(void);            /* chain 0, step 0; call from game_init */
void quest_start_chain(int idx);  /* force a chain (tests / dev) */
void quest_tick(float dt);        /* pause countdown + GRANT auto-skip;
                                   * call only while no dialog is open */

void quest_notify_pickup(void);
void quest_notify_deliver(int resident);
void quest_notify_talk(int resident);
void quest_notify_claim(void);

QuestStepType quest_step_type(void);
int quest_step_target(void);      /* resolved resident index, or -1 */
uint16_t quest_step_item(void);
uint16_t quest_step_count(void);
const char *quest_step_prompt(void);
const char *quest_chain_name(void);
int quest_chain_house(void);      /* the chain's first delivery house, or -1 */
int quest_step_seq(void);         /* bumps on every chain/step change */

int quest_carried(void);
int quest_delivered(void);
int quest_paused(void);           /* post-chain breather */

/* One-shot gift toast text ("GIFT: ..."), NULL when none is waiting. */
const char *quest_take_toast(void);

#endif
