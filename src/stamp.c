#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "stamp.h"

Stamp* new_polygon_stamp(Polygon* p) {
    Stamp* s = malloc(sizeof(Stamp));
    if (s == NULL) {
        return NULL;
    }

    s->polygon = p;
    s->tr_matrix = new_mat3();

    if (s->tr_matrix == NULL) {
        free(s);
        return NULL;
    }

    return s;
}

Stamp* new_rectangle_stamp(const size_t width, const size_t height) {
    Polygon* p = new_polygon();
    if (p == NULL) {
        return NULL;
    }

    add_vertex(p, (Point) {0, 0});
    add_vertex(p, (Point) {width, 0});
    add_vertex(p, (Point) {width, height});
    add_vertex(p, (Point) {0, height});
    close_polygon(p);

    Stamp* s = new_polygon_stamp(p);
    if (s == NULL) {
        free_polygon(p);
        return NULL;
    }

    return s;
}

Stamp* new_circle_stamp(const size_t steps, const size_t radius) {
    Polygon* p = new_polygon();
    if (p == NULL) {
        return NULL;
    }

    float stepa = (float) M_PI / (steps / 2.0f);
    for (size_t i = 0; i < steps; i++) {
        add_vertex(p, (Point) {
            cosf(stepa * i) * radius + radius,
            sinf(stepa * i) * radius + radius
        }); // +radius to move it to the positive side
    }
    close_polygon(p);

    Stamp* s = new_polygon_stamp(p);
    if (s == NULL) {
        free_polygon(p);
        return NULL;
    }

    return s;
}

void free_stamp(Stamp* s) {
    if (s->polygon) {
        free_polygon(s->polygon);
    }
    if (s->bitmap) {
        free_canvas(s->bitmap);
    }

    free_mat3(s->tr_matrix);
    free(s);
}

Point get_stamp_center(const Stamp* stamp) {
    if (stamp->type == POLYGON) {
        return get_polygon_center(stamp->polygon);
    }

    return (Point) {stamp->bitmap->width / 2, stamp->bitmap->height / 2};
}

void apply_matrix(Stamp* s) {
    if (s->polygon) {
        transform_polygon(s->polygon, s->tr_matrix);
    }

    reset_mat3(s->tr_matrix);
}

void bresenham(Canvas* c, Color color, int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);

    int incx = (x1 < x2) ? 1 : -1;
    int incy = (y1 < y2) ? 1 : -1;

    int err = ((dx > dy) ? dx : -dy) / 2;
    int e2 = 0;

    for(;;){
        set_pixel(c, color, x1, y1);
        if (x1 == x2 && y1 == y2) {
            break;
        }

        e2 = err;
        if (e2 >-dx) {
            err -= dy;
            x1 += incx;
        }
        if (e2 < dy) {
            err += dx;
            y1 += incy;
        }
    }
}

int draw_stamp_outline(Canvas* c, Color color, const Stamp* s) {
    if (s->type == BITMAP) {
        return -1;
    }

    for (size_t i = 1; i < s->polygon->next; i++) {
        Point from = transform_point(s->polygon->vertices[i - 1], s->tr_matrix);
        Point to = transform_point(s->polygon->vertices[i], s->tr_matrix);
        bresenham(c, color, (int) from.x, (int) from.y, (int) to.x, (int) to.y);
    }

    return 1;
}

static inline int to_fixed(const float f) {
    return (int) roundf(16.0f * f);
}

static inline int min(const int a, const int b, const int c) {
    int smaller = (a < b) ? a : b;
    smaller = (c < smaller) ? c : smaller;
    return (0 > smaller) ? 0 : smaller;
}

static inline int max(const int a, const int b, const int c, const int m) {
    int bigger = (a > b) ? a : b;
    bigger = (c > bigger) ? c : bigger;
    return (m < bigger) ? m : bigger;
}

static inline int half_space(const int C, const int DX, const int DY,
                       const int x0, const int y0, const int x1, const int y1) {
    bool a00 = C + DX * y0 - DY * x0 > 0;
    bool a10 = C + DX * y0 - DY * x1 > 0;
    bool a01 = C + DX * y1 - DY * x0 > 0;
    bool a11 = C + DX * y1 - DY * x1 > 0;
    return a00 | (a10 << 1) | (a01 << 2) | (a11 << 3);
}

void fill_triangle(Canvas* canvas, const Point v1, const Point v2,
                   const Point v3, void(*set_pixel)(Canvas*, const int, const int)) {
    // 28.4 fixed-point coordinates
    const int Y1 = to_fixed(v1.y);
    const int Y2 = to_fixed(v2.y);
    const int Y3 = to_fixed(v3.y);

    const int X1 = to_fixed(v1.x);
    const int X2 = to_fixed(v2.x);
    const int X3 = to_fixed(v3.x);

    // Deltas
    const int DX12 = X1 - X2;
    const int DX23 = X2 - X3;
    const int DX31 = X3 - X1;

    const int DY12 = Y1 - Y2;
    const int DY23 = Y2 - Y3;
    const int DY31 = Y3 - Y1;

    // Fixed-point deltas
    const int FDX12 = DX12 << 4;
    const int FDX23 = DX23 << 4;
    const int FDX31 = DX31 << 4;

    const int FDY12 = DY12 << 4;
    const int FDY23 = DY23 << 4;
    const int FDY31 = DY31 << 4;

    // Bounding rectangle. Also clip to canvas size
    int minx = (min(X1, X2, X3) + 0xF) >> 4;
    int maxx = (max(X1, X2, X3, to_fixed(canvas->width)) + 0xF) >> 4;
    int miny = (min(Y1, Y2, Y3) + 0xF) >> 4;
    int maxy = (max(Y1, Y2, Y3, to_fixed(canvas->height)) + 0xF) >> 4;

    // Block size, standard 8x8 (must be power of two)
    const int q = 8;

    // Start in corner of 8x8 block
    minx &= ~(q - 1);
    miny &= ~(q - 1);

    // Half-edge constants
    int C1 = DY12 * X1 - DX12 * Y1;
    int C2 = DY23 * X2 - DX23 * Y2;
    int C3 = DY31 * X3 - DX31 * Y3;

    // Correct for fill convention
    if(DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
    if(DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
    if(DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

    // Loop through blocks
    for(int y = miny; y < maxy; y += q) {
        for(int x = minx; x < maxx; x += q) {
            // Corners of block
            int x0 = x << 4;
            int x1 = (x + q - 1) << 4;
            int y0 = y << 4;
            int y1 = (y + q - 1) << 4;

            // Evaluate half-space functions
            int a = half_space(C1, DX12, DY12, x0, y0, x1, y1);
            int b = half_space(C2, DX23, DY23, x0, y0, x1, y1);
            int c = half_space(C3, DX31, DY31, x0, y0, x1, y1);

            // Skip block when outside an edge
            if(a == 0x0 || b == 0x0 || c == 0x0) continue;

            // Accept whole block when totally covered
            if(a == 0xF && b == 0xF && c == 0xF) {
                for(int iy = y; iy < y + q; iy++) {
                    for(int ix = x; ix < x + q; ix++) {
                        set_pixel(canvas, ix, iy);
                    }
                }
            } else { // Partially covered block
                int CY1 = C1 + DX12 * y0 - DY12 * x0;
                int CY2 = C2 + DX23 * y0 - DY23 * x0;
                int CY3 = C3 + DX31 * y0 - DY31 * x0;

                for(int iy = y; iy < y + q; iy++) {
                    int CX1 = CY1;
                    int CX2 = CY2;
                    int CX3 = CY3;

                    for(int ix = x; ix < x + q; ix++) {
                        if(CX1 > 0 && CX2 > 0 && CX3 > 0) {
                            set_pixel(canvas, ix, iy);
                        }

                        CX1 -= FDY12;
                        CX2 -= FDY23;
                        CX3 -= FDY31;
                    }

                    CY1 += FDX12;
                    CY2 += FDX23;
                    CY3 += FDX31;
                }
            }
        }
    }
}

int fill_shape(Canvas* c, Color color, const Stamp* s) {
    Point p1 = transform_point(s->polygon->vertices[0], s->tr_matrix);
    Point p2 = transform_point(s->polygon->vertices[1], s->tr_matrix);

    for (int i = 2; i <= s->polygon->next; i++) {
        Point p3 = transform_point(s->polygon->vertices[i], s->tr_matrix);

        if (color == WHITE) {
            fill_triangle(c, p3, p2, p1, set_white_pixel_unsafe);
        } else {
            fill_triangle(c, p3, p2, p1, set_black_pixel_unsafe);
        }

        p2 = p3;
    }

   return 1;
}

