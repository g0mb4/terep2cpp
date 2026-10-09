/*
 * Drawing the world: the visible part of the height map, the cars standing
 * on it and the particles.
 *
 * Painter's algorithm on the map grid: the cells are visited from the far
 * end of the view towards the camera, row by row, and inside each row from
 * both ends towards the camera's column.  A car is drawn right after the
 * cell it was assigned to, so it covers the terrain behind it and is
 * covered by what comes later.
 *
 * Depending on the view direction the "rows" are map rows (y) or map
 * columns (x), walked upwards or downwards: four traversal orders.
 */
#include <utility>
#include "terep.hpp"

bool poly_hidden;

/* Camera-space transform of a point given relative to the eye, plus its
 * projection when it is in front of the near plane.  Returns false when it
 * is not (sx/sy are left untouched then).  orig 1000:13cc, 1000:2418 */
bool view_transform(int16_t x, int16_t y, int16_t z, ProjVertex *out)
{
    int16_t r[3];
    rotate(G->view_matrix, x, y, z, r);
    out->x = r[0];
    out->y = (int16_t)-r[1];
    out->z = r[2];
    if (out->y < G->near_clip)
        return false;
    int16_t sx, sy;
    project(out->x, out->y, out->z, &sx, &sy);
    out->sx = (int16_t)(sx + G->screen_cx);
    out->sy = (int16_t)(G->screen_cy - sy);
    return true;
}

/* A point given in map units, relative to the eye. */
static bool view_transform_units(uint16_t x, uint16_t y, uint16_t z, ProjVertex *out)
{
    return view_transform((int16_t)(x - G->eye.x.units()), (int16_t)(y - G->eye.y.units()),
                          (int16_t)(z - G->eye.z.units()), out);
}

/* ------------------------------------------------------------------------
 * Visible area
 * ---------------------------------------------------------------------- */

/* One clipping pass of the visible hexagon against a side of the map
 * rectangle.  orig 1000:4394/444d/4506/45c3 */
static void vis_clip_pass(PointBuf &src, PointBuf &dst, bool axis_y, int16_t bound, bool keep_greater)
{
    Point2 *in = src.v, *out = dst.v;
    uint16_t n = src.count, count = 0;
    auto coord = [&](const Point2 &p) { return axis_y ? p.y : p.x; };
    auto inside = [&](const Point2 &p) { return keep_greater ? coord(p) >= bound : coord(p) <= bound; };

    if (n) {
        in[n] = in[0];
        bool prev_in = inside(in[0]);
        if (prev_in)
            out[count++] = in[0];
        for (uint16_t i = 1; i <= n; i++) {
            const Point2 &p = in[i - 1], &c = in[i];
            bool cur_in = inside(c);
            if (prev_in != cur_in) {
                int16_t dc = (int16_t)(coord(c) - bound), dp = (int16_t)(coord(p) - bound);
                Point2 &o = out[count++];
                if (axis_y) {
                    o.x = edge_lerp(dc, c.x, dp, p.x);
                    o.y = bound;
                } else {
                    o.x = bound;
                    o.y = edge_lerp(dc, c.y, dp, p.y);
                }
            }
            if (cur_in && i < n)
                out[count++] = c;
            prev_in = cur_in;
        }
    }
    dst.count = count;
}

/* Record one edge of the visible polygon in vis_span: left ends for edges
 * going one way, right ends for the other (which way depends on whether
 * the polygon is mirrored).  Bresenham-style stepping.
 * orig 1000:4120 / 1000:4302 */
static void vis_span_edge(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool mirrored)
{
    bool right = mirrored;
    if (y0 > y1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
        right = !right;
    }
    if (y0 < G->vis_row_first) G->vis_row_first = y0;
    if (y1 > G->vis_row_last) G->vis_row_last = y1;

    uint16_t dy = (uint16_t)(y1 - y0);
    if (!dy)
        return;
    int16_t *row = (right ? &G->vis_span[0].right : &G->vis_span[0].left) + 2 * y0;
    int16_t dx = (int16_t)(x1 - x0);
    int16_t step = 1;
    if (dx < 0) {
        dx = (int16_t)-dx;
        step = -1;
    }
    int16_t x = x0, err = 0;
    for (uint16_t i = 0; i < dy; i++, row += 2) {
        *row = x;
        err = (int16_t)(err - dx);
        while (err < 0) {
            x = (int16_t)(x + step);
            err = (int16_t)(err + (int16_t)dy);
        }
    }
    *row = x1;
}

/* The part of the map that can be visible: a hexagon around the camera,
 * opening in the view direction, clipped to the map and turned into a
 * first/last column for every row.  With swap_xy everything is done with
 * x and y exchanged.  orig 1000:3fd0 / 1000:41b2 */
static void compute_visible_area(bool swap_xy)
{
    int16_t ex = G->eye.x.cell();
    int16_t ey = G->eye.y.cell();
    int16_t s7 = G->yaw_sin7, c7 = G->yaw_cos7;
    int16_t c = (int16_t)(c7 >> 6), s = (int16_t)(s7 >> 6);
    Point2 *p = G->vis_buf[0].v;

    /* three points just around the camera, behind it */
    p[0].x = (int16_t)(ex - c); p[0].y = (int16_t)(ey - s);
    p[1].x = (int16_t)(ex - s); p[1].y = (int16_t)(ey + c);
    p[2].x = (int16_t)(ex + c); p[2].y = (int16_t)(ey + s);
    /* and three far away, in the view direction */
    int16_t ca = (int16_t)(c7 * G->view_side_a), sb = (int16_t)(s7 * G->view_side_b);
    int16_t cb = (int16_t)(c7 * G->view_side_b), sa = (int16_t)(s7 * G->view_side_a);
    p[3].x = (int16_t)(((int16_t)(cb + sa) >> 8) + ex);
    p[3].y = (int16_t)(((int16_t)(sb - ca) >> 8) + ey);
    p[4].x = (int16_t)(ex + ((int16_t)(G->view_reach * s7) >> 8));
    p[4].y = (int16_t)(ey - ((int16_t)(G->view_reach * c7) >> 8));
    p[5].x = (int16_t)(((int16_t)(sa - cb) >> 8) + ex);
    p[5].y = (int16_t)(((int16_t)(-sb - ca) >> 8) + ey);
    G->vis_buf[0].count = 6;
    if (swap_xy) {
        for (int i = 0; i < 6; i++)
            p[i] = { p[i].y, p[i].x };
    }

    vis_clip_pass(G->vis_buf[0], G->vis_buf[1], false, G->vis_min, true);
    vis_clip_pass(G->vis_buf[1], G->vis_buf[0], false, G->vis_max, false);
    vis_clip_pass(G->vis_buf[0], G->vis_buf[1], true, G->vis_lo, true);
    vis_clip_pass(G->vis_buf[1], G->vis_buf[0], true, G->vis_hi, false);

    uint16_t n = G->vis_buf[0].count;
    if (n < 3)
        return;                         /* keeps last frame's spans */
    G->vis_row_first = G->vis_hi;
    G->vis_row_last = G->vis_lo;
    for (uint16_t i = 0; i + 1 < n; i++)
        vis_span_edge(p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, swap_xy);
    vis_span_edge(p[n - 1].x, p[n - 1].y, p[0].x, p[0].y, swap_xy);
}

/* ------------------------------------------------------------------------
 * Terrain cells
 * ---------------------------------------------------------------------- */

/* Corners of a cell as seen by draw_cell: d0 is the cell's own grid point,
 * d1 its neighbour along the traversal's columns, s0 along its rows. */
struct CellQuad {
    const ProjVertex &d0, &d1, &s0, &s1;
    bool swap_xy;
};

/* Texture coordinates of a corner: the cell colour picks a 16x16 tile of
 * maptex.pcx (low nibble: column, high nibble: row). */
static uint32_t tile_uv(uint8_t color, bool dx, bool dy)
{
    uint16_t u = (uint16_t)(((color & 0x0f) << 12) | 0x80);
    uint16_t v = (uint16_t)(((color & 0xf0) << 8) | 0x80);
    if (dx) u = (uint16_t)(u | 0x0f00);
    if (dy) v = (uint16_t)(v | 0x0f00);
    return (uint32_t)v << 16 | u;
}

/* Facing test of the triangle just built.  The grid is seen from above,
 * so with the traversal used the visible triangles are the "back-facing"
 * ones; with swap_xy the quad is mirrored and the test flips.  Triangles
 * cut down to less than three points are not tested. */
static bool cell_visible(const CellQuad &q, uint16_t count)
{
    if (count < 3)
        return false;
    poly_hidden = poly_backfacing();
    return q.swap_xy ? !poly_hidden : poly_hidden;
}

/* One map cell as two triangles: textured with maptex when near, a flat
 * fogged colour (second triangle one shade darker) when far.
 * orig 1000:1cde / 1000:1e3a */
static void draw_cell(const CellQuad &q, uint8_t color)
{
    int16_t dist = (int16_t)((int16_t)(q.d1.y + q.s0.y) >> 2);

    if (dist >= G->fog_start) {
        uint8_t c = (&G->fog_table[0][0])[(uint8_t)(dist >> 8) * 256 + color];   /* may run past the table */
        G->poly_color = (uint16_t)(c << 8 | c);
        poly_begin(q.d1, 0);
        poly_add(q.d0, 0);
        poly_add(q.s0, 0);
        if (cell_visible(q, poly_end(q.d1, 0)))
            draw_flat();
        poly_begin(q.d1, 0);
        poly_add(q.s0, 0);
        poly_add(q.s1, 0);
        if (cell_visible(q, poly_end(q.d1, 0))) {
            if (G->poly_color & 0x0f)
                G->poly_color -= 0x101;
            draw_flat();
        }
        return;
    }

    /* corner offsets in map x/y: mirrored when traversing by columns */
    bool sw = q.swap_xy;
    uint32_t uv_d0 = tile_uv(color, false, false);
    uint32_t uv_d1 = tile_uv(color, !sw, sw);
    uint32_t uv_s0 = tile_uv(color, sw, !sw);
    uint32_t uv_s1 = tile_uv(color, true, true);

    raster_texture = seg_ptr(G->seg_maptex);
    poly_begin(q.d1, uv_d1);
    poly_add(q.d0, uv_d0);
    poly_add(q.s0, uv_s0);
    if (cell_visible(q, poly_end(q.d1, uv_d1)))
        draw_textured();
    poly_begin(q.d1, uv_d1);
    poly_add(q.s0, uv_s0);
    poly_add(q.s1, uv_s1);
    if (cell_visible(q, poly_end(q.d1, uv_d1)))
        draw_textured();
}

static void draw_cars_in_cell(uint16_t cell)
{
    for (int i = 0; i < G->num_cars; i++)
        if (car_get(i).cell == cell)
            draw_car(car_get(i));
}

/* ------------------------------------------------------------------------
 * Which cell each car is drawn with
 * ---------------------------------------------------------------------- */

static int8_t abs8(int8_t v)
{
    return v < 0 ? (int8_t)-v : v;     /* -128 stays -128 */
}

/* A car spans several cells; it is drawn after the one of its vertices
 * that the traversal reaches last (per axis, the one closest to the
 * camera's row/column).  orig 1000:22f0/233b/2384/23cf */
static void pick_car_cell(Car &car, bool swap_xy, bool descending)
{
    CarVertex *v = car.vertices();
    uint16_t n = car.vertex_count();
    uint8_t ex = G->eye.x.cell(), ey = G->eye.y.cell();
    int8_t best_x = 0x7f, best_y = 0x7f;
    uint8_t cell_x = 0, cell_y = 0;

    for (uint16_t i = 0; i < n; i++) {
        uint8_t cx = v[i].pos.x.cell(), cy = v[i].pos.y.cell();
        int8_t dx = (int8_t)(cx - ex), dy = (int8_t)(cy - ey);
        if (!swap_xy) {
            dx = abs8(dx);
            dy = descending ? dy : (int8_t)-dy;
        } else {
            dy = abs8(dy);
            dx = descending ? dx : (int8_t)-dx;
        }
        if (dx < best_x) { cell_x = cx; best_x = dx; }
        if (dy < best_y) { cell_y = cy; best_y = dy; }
    }
    car.cell = (uint16_t)(cell_y << 8 | cell_x);
}

/* ------------------------------------------------------------------------
 * Traversal
 * ---------------------------------------------------------------------- */

/* Project grid point (row, col) of the current traversal into the row
 * buffer. */
static void project_grid_point(uint8_t row, uint8_t col, bool swap_xy, ProjVertex *out)
{
    uint16_t cell = swap_xy ? (uint16_t)(col << 8 | row) : (uint16_t)(row << 8 | col);
    view_transform_units((uint16_t)((cell & 0xff) << 8), (uint16_t)(cell & 0xff00),
                         (uint16_t)(heightmap()[cell] << 4), out);
}

/* Draw cell (row_c, col_c): its corners are in the row buffers lo (row_c)
 * and hi (row_c + 1). */
static void draw_grid_cell(uint8_t row_c, uint8_t col_c, ProjVertex *lo, ProjVertex *hi, bool swap_xy)
{
    uint16_t cell = swap_xy ? (uint16_t)(col_c << 8 | row_c) : (uint16_t)(row_c << 8 | col_c);
    CellQuad q = { lo[col_c], lo[col_c + 1], hi[col_c], hi[col_c + 1], swap_xy };
    draw_cell(q, colormap()[cell]);
    draw_cars_in_cell(cell);
}

/* orig 1000:1965 (and its second half, FUN_1965_NP) */
void draw_terrain()
{
    uint16_t yaw = (uint16_t)G->eye_angles[2];
    uint8_t octant = (uint8_t)(yaw >> 8);

    /* view mostly along x: traverse by columns */
    bool swap_xy = !parity_even(octant & 0x60);
    bool descending = !swap_xy ? !parity_even(octant & 0xa0) : !(octant & 0x80);

    G->terrain_row_cur = 0;
    G->terrain_row_prev = 0xa00;
    G->terrain_swap_xy = (uint8_t)swap_xy;
    compute_visible_area(swap_xy);
    for (int i = 0; i < G->num_cars; i++)
        pick_car_cell(car_get(i), swap_xy, descending);

    uint8_t eye_col = swap_xy ? G->eye.y.cell() : G->eye.x.cell();
    uint8_t first = (uint8_t)G->vis_row_first, last = (uint8_t)G->vis_row_last;
    uint8_t end_row = descending ? last : first;     /* no cells to draw on it */
    int16_t row_index = descending ? G->vis_row_last : G->vis_row_first;
    uint8_t row = (uint8_t)row_index;

    for (;;) {
        ScanEdge *span = &G->vis_span[row_index];
        ScanEdge *prev = descending ? span + 1 : span - 1;
        uint8_t left = (uint8_t)span->left, right = (uint8_t)span->right;
        uint8_t prev_left = (uint8_t)prev->left, prev_right = (uint8_t)prev->right;
        ProjVertex *cur_buf = G->terrain_rows[G->terrain_row_cur ? 1 : 0];
        ProjVertex *prev_buf = G->terrain_rows[G->terrain_row_prev ? 1 : 0];
        ProjVertex *lo = descending ? cur_buf : prev_buf;
        ProjVertex *hi = descending ? prev_buf : cur_buf;
        uint8_t row_c = descending ? row : (uint8_t)(row - 1);

        /* left part of the row, towards the camera's column */
        for (uint8_t col = left;; col++) {
            project_grid_point(row, col, swap_xy, &cur_buf[col]);
            if (row != end_row && col != left && col > prev_left && col <= prev_right)
                draw_grid_cell(row_c, (uint8_t)(col - 1), lo, hi, swap_xy);
            if (col >= eye_col)
                break;
        }
        /* right part, also towards the camera's column */
        for (uint8_t col = right;; col--) {
            project_grid_point(row, col, swap_xy, &cur_buf[col]);
            if (row != end_row && col != right && col >= prev_left && col < prev_right)
                draw_grid_cell(row_c, col, lo, hi, swap_xy);
            if (col <= eye_col)
                break;
        }

        if (descending) {
            if (row <= first)
                break;
            row--;
            row_index--;
        } else {
            if (row >= last)
                break;
            row++;
            row_index++;
        }
        G->terrain_row_cur ^= 0xa00;
        G->terrain_row_prev ^= 0xa00;
    }
}

/* ------------------------------------------------------------------------
 * Particles
 * ---------------------------------------------------------------------- */

/* A camera-facing square of the given world size.  orig 1000:0cd3 and
 * model opcode 9 */
void draw_sprite(int16_t sx, int16_t sy, int16_t depth, uint16_t sprite_ofs)
{
    const uint8_t *sp = ds_ptr(sprite_ofs);
    int16_t h = (int16_t)(*reinterpret_cast<const int16_t *>(sp) / depth);
    const uint32_t *uv = reinterpret_cast<const uint32_t *>(sp + 2);
    PolyVertex *o = G->poly_buf[0].v;
    static const int8_t corner[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int i = 0; i < 4; i++) {
        o[i].sx = (int16_t)(sx + corner[i][0] * h);
        o[i].sy = (int16_t)(sy + corner[i][1] * h);
        o[i].a = (uint16_t)uv[i];
        o[i].b = (uint16_t)(uv[i] >> 16);
    }
    G->poly_buf[0].count = 4;
    draw_textured();
}

/* Smoke and debris: textured squares, only where the terrain is visible.
 * orig 1000:0b25 */
void draw_particles()
{
    raster_texture = seg_ptr(G->seg_textures);
    for (uint16_t i = 0; i < G->particle_bytes / sizeof(Particle); i++) {
        const Particle &p = G->particles[i];
        uint8_t a = p.pos.x.cell(), b = p.pos.y.cell();
        int16_t row = G->terrain_swap_xy ? a : b;
        int16_t col = G->terrain_swap_xy ? b : a;
        if (row < G->vis_row_first || row > G->vis_row_last)
            continue;
        if (col < G->vis_span[row].left || col > G->vis_span[row].right)
            continue;

        ProjVertex v;
        if (!view_transform_units(p.pos.x.units(), p.pos.y.units(), p.pos.z.units(), &v))
            continue;
        draw_sprite(v.sx, v.sy, v.y, G->smoke_sprite[(uint8_t)p.kind >> 4]);
    }
}
