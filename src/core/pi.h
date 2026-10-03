#ifndef ISLAND_PI_H
#define ISLAND_PI_H

/* Portable pi. <math.h> only exposes M_PI when a feature-test macro (e.g.
 * _GNU_SOURCE) is set, which psp-gcc -std=c99 does not do by default. */
#define ISLAND_PI ((float)3.14159265358979323846f)

#endif
