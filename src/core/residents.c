#include "residents.h"

#include <stddef.h>

/*
 * The island cast (island1-9fm3). Five voices, each with a thread of the
 * island's story: the storm that took the old pier, the lighthouse nobody
 * enters, the radio that picks up a station that should not exist. Lines are
 * picked at random per conversation (no immediate repeat, see game.c) and
 * rendered uppercase-friendly through the 8x8 HUD font — keep them under
 * DIALOG_MAX_CHARS (96) so the textbox wrap has room.
 */

static const Dialogue s_residents[RESIDENT_COUNT] = {
    {
        /* MARLOWE — old fisher; the sea, the fog, the pier. */
        "MARLOWE",
        {
            "FOG'S THICK AGAIN. IT COMES IN WRONG, LIKE IT'S LOOKING FOR SOMEBODY.",
            "MY GRANDFATHER FISHED THESE WATERS. HE SAID THE ISLAND MOVES WHEN NOBODY'S WATCHING.",
            "YOU DRIVE THAT CAR LIKE THE ROADS REMEMBER YOU.",
            "STORM TOOK THE OLD PIER. NOW EVERYTHING COMES IN BY YOUR HANDS, COURIER.",
        },
        {
            "MY CATCH BOX! HA — THE MAINLAND STILL REMEMBERS US.",
            "YOU'RE A GOOD SORT. THE FOG DOESN'T FOOL YOU, COURIER.",
            "SET IT DOWN EASY. THINGS THAT CROSS THE WATER BRING THE WATER WITH THEM.",
            "TELL THEM MARLOWE SAYS THE TIDE'S BEEN LYING LATELY.",
        },
        {
            "EMPTY-HANDED? THE SEA HATES A SLOW ERRAND.",
            "COME BACK WHEN THE CRATE'S READY. I'LL BE HERE, COUNTING WAVES.",
            "YOU LOOK LIKE SOMEONE WHO LOST SOMETHING. GO FIND IT.",
        },
    },
    {
        /* TAMSIN — mechanic and radio operator; the signal. */
        "TAMSIN",
        {
            "RADIO'S PICKING UP A STATION THAT SHOULDN'T EXIST. SAME SONG EVERY NIGHT.",
            "IF YOUR CAR WHINES IN THIRD IT'S THE BEARING. NOT THE SPIRITS, LIKE MARLOWE SAYS.",
            "I FIX WHAT THE STORM BREAKS. THESE DAYS THAT'S EVERYTHING.",
            "YOU'RE THE ONLY NEW FACE IN YEARS. TALK TO ME OFTEN, COURIER.",
        },
        {
            "PARTS! NOW I CAN PUT THE RADIO RIGHT PROPERLY.",
            "JUST IN TIME — THE TRANSMITTER'S ACTING UP AGAIN.",
            "ISLAND RULE: A NOD'S A CONTRACT. WE'RE DONE HERE.",
            "GOOD HAUL. THE ISLAND RUNS ON YOU AND BUBBLE WRAP NOW.",
        },
        {
            "NO PACKAGE? THEN STAY AND KEEP ME COMPANY INSTEAD.",
            "THE DEPOT'S THAT WAY. BREN'S WAITING TO HAND IT OVER.",
            "CHECK YOUR MAPS, COURIER. OR THE DEPOT. OR BOTH.",
        },
    },
    {
        /* ORIN — painter; light, colour, the fog that is not grey. */
        "ORIN",
        {
            "HOLD STILL — THE LIGHT ON YOU IS PERFECT. NO, DON'T SMILE.",
            "I'M PAINTING THE ISLAND FOUR TIMES. SAME SCENE, FOUR DIFFERENT MOODS.",
            "PEOPLE SAY THE FOG IS GREY. IT ISN'T. IT'S VIOLET, AND IT'S ANGRY.",
            "EVERY PARCEL YOU CARRY ADDS A COLOUR TO THIS PLACE.",
        },
        {
            "FOR ME? THIS GOES STRAIGHT INTO THE COMPOSITION.",
            "A DELIVERY IS JUST A LITTLE STORY IN A BOX. I LOVE IT.",
            "PERFECT. I WAS DOWN TO MY LAST GOOD BLUE.",
            "YOU'VE GOT AN EYE FOR TIMING, COURIER. THAT'S A TALENT.",
        },
        {
            "EMPTY ARMS? THEN POSE FOR ME INSTEAD.",
            "THE WAITING IS PART OF THE PAINTING. TRUST ME.",
            "BREN'S GOT YOUR PACKAGE. GO FETCH IT BEFORE THE LIGHT CHANGES.",
        },
    },
    {
        /* PIP — kid; the secret society, the lighthouse. */
        "PIP",
        {
            "I'M KEEPER OF THE SECRET SOCIETY. THERE'S ONLY ME, BUT IT'S LEGIT.",
            "GROWNUPS SAY THE LIGHTHOUSE IS CLOSED. I SAY NOTHING'S CLOSED IF YOU CLIMB.",
            "YOUR CAR IS COOLER THAN THE BOAT. DON'T TELL THE BOAT.",
            "I HEARD SOMETHING IN THE LIGHTHOUSE LAST NIGHT — A HUM, LIKE A SLEEPING GIANT.",
        },
        {
            "YESSS! THE LONG-AWAITED BOX! I MEAN... THANK YOU, COURIER.",
            "YOU DELIVERED! NOW YOU OWE ME ONE STORY. ISLAND RULE.",
            "I'LL OPEN THIS WHEN NOBODY'S LOOKING. WHICH IS NOW. BYE!",
            "EACH BOX IS A TREASURE. I'VE BURIED NINE ALREADY. NONE OF THEM MINE.",
        },
        {
            "WHERE'S MY STUFF?! I'VE WAITED A WHOLE FOREVER.",
            "TRY THE DEPOT! BREN GUARDS IT LIKE A DRAGON. A TIRED DRAGON.",
            "RACE YOU THERE? YOU'LL LOSE — I KNOW THE SHORTCUTS.",
        },
    },
};

static const Dialogue s_keeper = {
    "BREN",
    {
        "HOUSE ON THE SLIP. BOX'S WEIGHTED RIGHT — DON'T DROP IT IN THE WATER.",
        "ANOTHER RUN. THE ISLAND REMEMBERS EVERY DELIVERY YOU MAKE, COURIER.",
        "TAKE THE RIDGE ROAD. THE SHORE PATH'S FLOODED... AGAIN.",
        "SIGN HERE IN THE DIRT. ISLAND LOGISTICS, ISLAND RULES.",
    },
    {NULL},
    {NULL},
};

const Dialogue* resident_dialogue(int i) {
  if (i < 0 || i >= RESIDENT_COUNT)
    i = 0;
  return &s_residents[i];
}

const Dialogue* depot_dialogue(void) {
  return &s_keeper;
}
