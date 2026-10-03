#ifndef PASSES_H
#define PASSES_H

/*
 * Render passes, drawn back to front each frame:
 *
 *   sky -> ocean -> terrain -> props -> buildings -> vehicle -> particles
 *       -> beacon -> hud
 *
 * Every pass declares the fixed-function state it needs; see gfx.h. Passes that
 * push state must pop it before returning so nothing leaks into the next frame.
 */

void pass_sky(void);
void pass_ocean(void);
void pass_terrain(void);
void pass_props(void);
void pass_buildings(void);
void pass_vehicle(void);
void pass_particles(void);
void pass_beacon(void);
void pass_hud(void);

#endif