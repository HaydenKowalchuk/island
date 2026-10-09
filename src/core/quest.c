#include "quest.h"

#include <stddef.h>
#include <stdio.h>

#include "inventory.h"
#include "items.h"
#include "residents.h"

/*
 * Quest chain progression (island1-f9x2). Pure state machine: no world, no
 * rendering — game.c reports events (pickup / deliver / talk / claim) and
 * reads the current step to drive ranges, the beacon, parcel spawns and the
 * HUD. Chain tables below are the whole design surface for new content.
 *
 * The delivery run is the bread-and-butter loop; the story chain rotates in
 * every third completion so the grind still feels like the classic game.
 */

#define QUEST_MAX_STEPS 8
#define QUEST_DONE_TIME 1.4f /* post-chain breather, matches the old loop */

/* target encoding: >=0 fixed resident; -1 dynamic deliver (random != last
 * delivered house); -2 dynamic that also avoids the chain's first pick. */

static const QuestStep s_steps_delivery[] = {
    {QSTEP_PICKUP, -1, 0, 0, "PICK UP THE PACKAGE AT THE DEPOT"},
    {QSTEP_DELIVER, -1, 0, 0, "DELIVER IT TO THE RESIDENT"},
};

static const QuestStep s_steps_lighthouse[] = {
    {QSTEP_TALK, 0, 0, 0, "ASK MARLOWE ABOUT THE LIGHTHOUSE"},
    {QSTEP_PICKUP, -1, ITEM_BRASS_KEY, 1, "COLLECT THE KEY PARCEL"},
    {QSTEP_DELIVER, 3, 0, 0, "DELIVER THE PARCEL TO PIP"},
    {QSTEP_GRANT, -1, ITEM_SEASHELL, 1, NULL},
    {QSTEP_CLAIM, -1, ITEM_FISH, 2, "CLAIM THE LOST PARCEL ON THE SHORE"},
    {QSTEP_TALK, 1, 0, 0, "TELL TAMSIN WHAT PIP HEARD"},
};

static const QuestChain s_chains[] = {
    {"ISLAND DELIVERY RUN", s_steps_delivery, 2, ITEM_NONE, 0},
    {"THE LIGHTHOUSE KEY", s_steps_lighthouse, 6, ITEM_CHART, 1},
};
#define CHAIN_COUNT ((int)(sizeof(s_chains) / sizeof(s_chains[0])))
#define STORY_EVERY 3 /* every third completed run plays the story chain */

static int s_chain;
static int s_step;
static int s_step_seq = 1;
static int s_res[QUEST_MAX_STEPS];
static int s_chain_house = -1; /* the chain's first delivery house */
static int s_last_house = -1;  /* last delivered-to house */
static int s_carried;
static int s_delivered;
static int s_completed;
static float s_pause;
static unsigned int s_rng = 0x9E3779B9u;
static char s_toast[40];
static int s_toast_pending;

static int rng_below(int n) {
  if (n <= 0)
    return 0;
  s_rng = s_rng * 1664525u + 1013904223u;
  return (int)((s_rng >> 8) % (unsigned)n);
}

static const QuestChain *chain(void) { return &s_chains[s_chain]; }

static QuestStepType cur_type(void) { return (QuestStepType)chain()->steps[s_step].type; }

/* Resolve dynamic targets for this chain (called at chain start). */
static void resolve_targets(void) {
  const QuestChain *c = chain();
  int dyn[QUEST_MAX_STEPS];
  int ndyn = 0;
  s_chain_house = -1;
  for (int i = 0; i < c->step_count; i++) {
    const QuestStep *st = &c->steps[i];
    if (st->target >= 0) {
      s_res[i] = st->target;
    } else if (st->type == QSTEP_DELIVER) {
      int pick = 0;
      for (int g = 0; g < 64; g++) {
        pick = rng_below(RESIDENT_COUNT);
        if (pick == s_last_house)
          continue;
        int clash = 0;
        if (st->target == -2)
          for (int k = 0; k < ndyn; k++)
            if (dyn[k] == pick)
              clash = 1;
        if (!clash)
          break;
      }
      s_res[i] = pick;
      dyn[ndyn++] = pick;
    } else {
      s_res[i] = -1;
    }
    if (st->type == QSTEP_DELIVER && s_chain_house < 0)
      s_chain_house = s_res[i];
  }
}

static void set_gift_toast(uint16_t item) {
  const ItemDef *d = item_def(item);
  snprintf(s_toast, sizeof(s_toast), "GIFT: %s",
           (d && d->name) ? d->name : "?");
  s_toast_pending = 1;
}

static void advance(void) {
  const QuestChain *c = chain();
  if (s_step + 1 >= c->step_count) {
    /* Chain complete: reward, then the breather. */
    if (c->reward_item && c->reward_count) {
      inv_add(c->reward_item, c->reward_count);
      set_gift_toast(c->reward_item);
    }
    s_pause = QUEST_DONE_TIME;
    return;
  }
  s_step++;
  s_step_seq++;
}

static void start_next_chain(void) {
  s_completed++;
  if (CHAIN_COUNT > 1 && (s_completed % STORY_EVERY) == 0)
    s_chain = 1; /* rotate the story chain in */
  else
    s_chain = 0;
  s_step = 0;
  s_pause = 0.0f;
  resolve_targets();
  s_step_seq++;
}

static void do_grant(void) {
  const QuestStep *st = &chain()->steps[s_step];
  uint16_t n = st->count ? st->count : 1;
  if (st->item) {
    inv_add(st->item, n);
    set_gift_toast(st->item);
  }
}

/* ---- lifecycle ---------------------------------------------------------- */

void quest_init(void) {
  s_chain = 0;
  s_step = 0;
  s_step_seq = 1;
  s_chain_house = -1;
  s_last_house = -1;
  s_carried = 0;
  s_delivered = 0;
  s_completed = 0;
  s_pause = 0.0f;
  s_rng = 0x9E3779B9u;
  s_toast_pending = 0;
  resolve_targets();
}

void quest_start_chain(int idx) {
  if (idx < 0 || idx >= CHAIN_COUNT)
    idx = 0;
  s_chain = idx;
  s_step = 0;
  s_pause = 0.0f;
  s_carried = 0; /* forcing a chain drops whatever was in hand */
  resolve_targets();
  s_step_seq++;
}

void quest_tick(float dt) {
  if (s_pause > 0.0f) {
    s_pause -= dt;
    if (s_pause > 0.0f)
      return;
    start_next_chain();
  }
  while (!quest_paused() && cur_type() == QSTEP_GRANT) {
    do_grant();
    advance();
  }
}

/* ---- events (from game.c) ---------------------------------------------- */

void quest_notify_pickup(void) {
  if (quest_paused() || s_carried || cur_type() != QSTEP_PICKUP)
    return;
  s_carried = 1;
  advance();
}

void quest_notify_deliver(int resident) {
  if (s_carried && cur_type() == QSTEP_DELIVER &&
      resident == quest_step_target()) {
    s_carried = 0;
    s_delivered++;
    s_last_house = resident;
    advance();
  }
}

void quest_notify_talk(int resident) {
  if (cur_type() == QSTEP_TALK && resident == quest_step_target())
    advance();
}

void quest_notify_claim(void) {
  if (!quest_paused() && cur_type() == QSTEP_CLAIM)
    advance();
}

/* ---- read side ---------------------------------------------------------- */

QuestStepType quest_step_type(void) { return cur_type(); }

int quest_step_target(void) { return s_res[s_step]; }

uint16_t quest_step_item(void) { return chain()->steps[s_step].item; }

uint16_t quest_step_count(void) { return chain()->steps[s_step].count; }

const char *quest_step_prompt(void) { return chain()->steps[s_step].prompt; }

int quest_chain_house(void) { return s_chain_house; }

const char *quest_chain_name(void) { return chain()->name; }

int quest_step_seq(void) { return s_step_seq; }

int quest_carried(void) { return s_carried; }

int quest_delivered(void) { return s_delivered; }

int quest_paused(void) { return s_pause > 0.0f; }

const char *quest_take_toast(void) {
  if (!s_toast_pending)
    return NULL;
  s_toast_pending = 0;
  return s_toast;
}
