#pragma once

/* Small rectangle utility helpers */

/* Compute intersection of two rectangles.
   A: (ax,ay) size (aw,ah)
   B: (bx,by) size (bw,bh)
   Returns 1 if intersection is non-empty and writes intersection to ix,iy,iw,ih.
   Returns 0 if no intersection.
*/
int rect_intersect(int ax, int ay, int aw, int ah,
                   int bx, int by, int bw, int bh,
                   int *ix, int *iy, int *iw, int *ih);
