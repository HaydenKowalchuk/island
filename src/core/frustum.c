#include "frustum.h"

#include <math.h>

static void frustum_plane(const float row3[4], const float row[4], float sign,
                          Plane *p) {
    p->a = row3[0] + sign * row[0];
    p->b = row3[1] + sign * row[1];
    p->c = row3[2] + sign * row[2];
    p->d = row3[3] + sign * row[3];

    float len = sqrtf(p->a * p->a + p->b * p->b + p->c * p->c);
    if (len > 1e-6f) {
        p->a /= len;
        p->b /= len;
        p->c /= len;
        p->d /= len;
    }
}

void frustum_extract(const float *clip, Plane planes[6]) {
    const float row0[4] = {clip[0], clip[4], clip[8], clip[12]};
    const float row1[4] = {clip[1], clip[5], clip[9], clip[13]};
    const float row2[4] = {clip[2], clip[6], clip[10], clip[14]};
    const float row3[4] = {clip[3], clip[7], clip[11], clip[15]};

    frustum_plane(row3, row0, +1.0f, &planes[0]); /* left   */
    frustum_plane(row3, row0, -1.0f, &planes[1]); /* right  */
    frustum_plane(row3, row1, +1.0f, &planes[2]); /* bottom */
    frustum_plane(row3, row1, -1.0f, &planes[3]); /* top    */
    frustum_plane(row3, row2, +1.0f, &planes[4]); /* near   */
    frustum_plane(row3, row2, -1.0f, &planes[5]); /* far    */
}

int frustum_aabb_visible(const Plane planes[6], float x0, float y0, float z0,
                         float x1, float y1, float z1) {
    for (int i = 0; i < 6; i++) {
        const Plane *p = &planes[i];
        float px = (p->a >= 0.0f) ? x1 : x0;
        float py = (p->b >= 0.0f) ? y1 : y0;
        float pz = (p->c >= 0.0f) ? z1 : z0;
        if (p->a * px + p->b * py + p->c * pz + p->d < 0.0f)
            return 0;
    }
    return 1;
}

int frustum_sphere_visible(const Plane planes[6], float x, float y, float z,
                           float radius) {
    for (int i = 0; i < 6; i++) {
        const Plane *p = &planes[i];
        if (p->a * x + p->b * y + p->c * z + p->d < -radius)
            return 0;
    }
    return 1;
}

int frustum_quad_visible(const Plane planes[6], float x0, float z0, float x1,
                         float z1, float y00, float y01, float y11, float y10) {
    const float cx[4] = {x0, x0, x1, x1};
    const float cz[4] = {z0, z1, z1, z0};
    const float cy[4] = {y00, y01, y11, y10};

    for (int i = 0; i < 6; i++) {
        const Plane *p = &planes[i];
        int inside = 0;
        for (int k = 0; k < 4; k++) {
            if (p->a * cx[k] + p->b * cy[k] + p->c * cz[k] + p->d >= 0.0f) {
                inside = 1;
                break;
            }
        }
        if (!inside)
            return 0;
    }
    return 1;
}
