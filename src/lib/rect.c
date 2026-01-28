#include "rect.h"

int rect_intersect(int ax, int ay, int aw, int ah,
                   int bx, int by, int bw, int bh,
                   int *ix, int *iy, int *iw, int *ih) {
    int a0x = ax;
    int a0y = ay;
    int a1x = ax + aw;
    int a1y = ay + ah;

    int b0x = bx;
    int b0y = by;
    int b1x = bx + bw;
    int b1y = by + bh;

    int rx0 = a0x > b0x ? a0x : b0x;
    int ry0 = a0y > b0y ? a0y : b0y;
    int rx1 = a1x < b1x ? a1x : b1x;
    int ry1 = a1y < b1y ? a1y : b1y;

    if (rx1 <= rx0 || ry1 <= ry0) {
        return 0; /* empty */
    }

    if (ix) *ix = rx0;
    if (iy) *iy = ry0;
    if (iw) *iw = rx1 - rx0;
    if (ih) *ih = ry1 - ry0;
    return 1;
}
