/*
 * Polygon rasteriser: clipping against the view rectangle, edge scanning
 * and the three span fillers (flat, Gouraud, textured).
 *
 * A polygon is built in G->poly_buf[0] (screen x, y and up to two 16-bit
 * attributes per vertex).  Drawing clips it against the four sides of the
 * clip rectangle, ping-ponging between poly_buf[0] and poly_buf[1], walks
 * its edges into the per-scanline tables span_x / span_a / span_b and then
 * fills every scanline between span_top and span_bottom.
 */
#include "terep.h"

/* Texture used by draw_textured (the original's FS segment). */
const uint8_t *raster_texture;

/* Clear the frame buffer to colour 0xff.  orig 1000:2b98 */
void clear_screen(void)
{
    memset(screen(), 0xff, 64000);
}

/* orig 1000:3f98 */
void put_pixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < G->clip_left || x > G->clip_right || y < G->clip_top || y > G->clip_bottom)
        return;
    screen()[(uint16_t)(y * 320 + x)] = color;
}

/* The value at distance 0 on the line through (da, va) and (db, vb), where
 * da and db are signed distances to a clipping plane.  orig 1000:3f7a
 * (1000:4680 is an identical copy). */
int16_t edge_lerp(int16_t da, int16_t va, int16_t db, int16_t vb)
{
    if (va == vb)
        return va;
    int32_t num = (int32_t)db * va - (int32_t)da * vb;
    return (int16_t)(num / (int16_t)(db - da));
}

/* ------------------------------------------------------------------------
 * Clipping against the view rectangle
 * ---------------------------------------------------------------------- */

enum { CLIP_LEFT, CLIP_RIGHT, CLIP_TOP, CLIP_BOTTOM };

/* Signed distance of a vertex to a clip edge, positive inside. */
static int16_t clip_distance(const PolyVertex *v, int edge)
{
    switch (edge) {
    case CLIP_LEFT:   return (int16_t)(v->sx - G->clip_left);
    case CLIP_RIGHT:  return (int16_t)(G->clip_right - v->sx);
    case CLIP_TOP:    return (int16_t)(v->sy - G->clip_top);
    default:          return (int16_t)(G->clip_bottom - v->sy);
    }
}

/* How many attributes the polygon carries: 0 flat, 1 shade, 2 texture. */
static void copy_vertex(PolyVertex *dst, const PolyVertex *src, int attrs)
{
    dst->sx = src->sx;
    dst->sy = src->sy;
    if (attrs >= 1) dst->a = src->a;
    if (attrs >= 2) dst->b = src->b;
}

/* Point where the edge p -> c crosses the clip edge. */
static void clip_intersect(PolyVertex *out, const PolyVertex *p, const PolyVertex *c,
                           int edge, int attrs)
{
    int horizontal = edge == CLIP_LEFT || edge == CLIP_RIGHT;
    int16_t bound = edge == CLIP_LEFT ? G->clip_left : edge == CLIP_RIGHT ? G->clip_right
                  : edge == CLIP_TOP ? G->clip_top : G->clip_bottom;
    /* distances measured along the clipped axis, as the original does */
    int16_t dc = (int16_t)((horizontal ? c->sx : c->sy) - bound);
    int16_t dp = (int16_t)((horizontal ? p->sx : p->sy) - bound);

    if (horizontal) {
        out->sx = bound;
        out->sy = edge_lerp(dc, c->sy, dp, p->sy);
    } else {
        out->sx = edge_lerp(dc, c->sx, dp, p->sx);
        out->sy = bound;
    }
    if (attrs == 1) {
        out->a = (uint16_t)edge_lerp(dc, (int16_t)c->a, dp, (int16_t)p->a);
    } else if (attrs == 2) {
        /* texture coordinates are unsigned: interpolated at half precision */
        out->a = (uint16_t)(edge_lerp(dc, (int16_t)(c->a >> 1), dp, (int16_t)(p->a >> 1)) << 1);
        out->b = (uint16_t)(edge_lerp(dc, (int16_t)(c->b >> 1), dp, (int16_t)(p->b >> 1)) << 1);
    }
}

/* One Sutherland-Hodgman pass.  The input's first vertex is copied past
 * its last one to close the loop.  When an edge leaves through the left or
 * top side from a vertex lying exactly on it, no extra vertex is added.
 * orig 1000:2df2/2eaf/2f6c/302d (flat), 324f/3376/34a2/35cf (shade),
 * 390a/3aa3/3c3c/3ddb (texture) */
static void clip_pass(PolyBuf *src, PolyBuf *dst, int edge, int attrs)
{
    PolyVertex *in = src->v;            /* may run past v[9], like the original */
    PolyVertex *out = dst->v;
    uint16_t n = src->count, count = 0;

    if (n) {
        copy_vertex(&in[n], &in[0], attrs);
        int prev_in = clip_distance(&in[0], edge) >= 0;
        if (prev_in)
            copy_vertex(&out[count++], &in[0], attrs);
        for (uint16_t i = 1; i <= n; i++) {
            const PolyVertex *p = &in[i - 1], *c = &in[i];
            int cur_in = clip_distance(c, edge) >= 0;
            if (prev_in && !cur_in) {
                int on_edge = (edge == CLIP_LEFT || edge == CLIP_TOP) && clip_distance(p, edge) == 0;
                if (!on_edge)
                    clip_intersect(&out[count++], p, c, edge, attrs);
            } else if (!prev_in && cur_in) {
                clip_intersect(&out[count++], p, c, edge, attrs);
            }
            if (cur_in && i < n)
                copy_vertex(&out[count++], c, attrs);
            prev_in = cur_in;
        }
    }
    dst->count = count;
}

/* Clip poly_buf[0] in place; returns the number of vertices left. */
static uint16_t clip_polygon(int attrs)
{
    PolyBuf *a = &G->poly_buf[0], *b = &G->poly_buf[1];
    clip_pass(a, b, CLIP_LEFT, attrs);
    clip_pass(b, a, CLIP_RIGHT, attrs);
    clip_pass(a, b, CLIP_TOP, attrs);
    clip_pass(b, a, CLIP_BOTTOM, attrs);
    return a->count;
}

/* ------------------------------------------------------------------------
 * Edge scanning
 * ---------------------------------------------------------------------- */

/* Record the x of an edge on every scanline it covers, into the left table
 * for downward edges and the right one for upward edges.  16.16 DDA; the
 * last row gets the accumulated value, not x1.  orig 1000:2c4b */
static void scan_edge_x(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int side = 0;
    if (y0 > y1) {
        int16_t t;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
        side = 1;
    }
    if (y0 < G->span_top) G->span_top = y0;
    if (y1 > G->span_bottom) G->span_bottom = y1;

    int16_t dy = (int16_t)(y1 - y0);
    if (!dy)
        return;
    int32_t step = (int32_t)((uint32_t)(uint16_t)(x1 - x0) << 16) / dy;
    uint32_t x = (uint32_t)(uint16_t)x0 << 16;
    int16_t *row = side ? &G->span_x[0].right : &G->span_x[0].left;
    row += 2 * y0;
    for (int16_t i = 0; i < dy; i++, row += 2) {
        *row = (int16_t)(x >> 16);
        x += (uint32_t)step;
    }
    *row = (int16_t)(x >> 16);
}

/* Same for one attribute.  orig 1000:317d */
static void scan_edge_attr(int16_t *table, int16_t a0, int16_t y0, int16_t a1, int16_t y1)
{
    int side = 0;
    if (y0 > y1) {
        int16_t t;
        t = a0; a0 = a1; a1 = t;
        t = y0; y0 = y1; y1 = t;
        side = 1;
    }
    int16_t dy = (int16_t)(y1 - y0);
    if (!dy)
        return;
    int32_t step = (int32_t)((uint32_t)(uint16_t)(a1 - a0) << 16) / dy;
    uint32_t a = (uint32_t)(uint16_t)a0 << 16;
    int16_t *row = table + side + 2 * y0;
    for (int16_t i = 0; i < dy; i++, row += 2) {
        *row = (int16_t)(a >> 16);
        a += (uint32_t)step;
    }
    *row = (int16_t)(a >> 16);
}

/* Both texture coordinates at once.  orig 1000:379b */
static void scan_edge_uv(int16_t y0, int16_t y1, uint16_t u0, uint16_t v0, uint16_t u1, uint16_t v1)
{
    int side = 0;
    if (y0 > y1) {
        int16_t t = y0; y0 = y1; y1 = t;
        uint16_t w;
        w = u0; u0 = u1; u1 = w;
        w = v0; v0 = v1; v1 = w;
        side = 1;
    }
    int16_t dy = (int16_t)(y1 - y0);
    if (!dy)
        return;
    int32_t du = (int32_t)((uint32_t)(uint16_t)(u1 - u0) << 16) / dy;
    int32_t dv = (int32_t)((uint32_t)(uint16_t)(v1 - v0) << 16) / dy;
    uint32_t u = (uint32_t)u0 << 16, v = (uint32_t)v0 << 16;
    int16_t *ru = &G->span_a[0].left + side + 2 * y0;
    int16_t *rv = &G->span_b[0].left + side + 2 * y0;
    for (int16_t i = 0; i < dy; i++, ru += 2, rv += 2) {
        *ru = (int16_t)(u >> 16);
        *rv = (int16_t)(v >> 16);
        u += (uint32_t)du;
        v += (uint32_t)dv;
    }
    *ru = (int16_t)(u >> 16);
    *rv = (int16_t)(v >> 16);
}

/* Walk the polygon outline into span_x.  Returns 0 when nothing is left
 * after clipping. */
static int scan_outline(int attrs)
{
    uint16_t n = clip_polygon(attrs);
    if (n < 3)
        return 0;
    const PolyVertex *v = G->poly_buf[0].v;
    G->span_top = G->clip_bottom;
    G->span_bottom = G->clip_top;
    for (uint16_t i = 0; i + 1 < n; i++)
        scan_edge_x(v[i].sx, v[i].sy, v[i + 1].sx, v[i + 1].sy);
    scan_edge_x(v[n - 1].sx, v[n - 1].sy, v[0].sx, v[0].sy);
    return 1;
}

/* Left end and width of a scanline; the tables may hold the ends in either
 * order. */
static uint16_t span_extent(int16_t y, int16_t *x0, int16_t *w)
{
    int16_t l = G->span_x[y].left;
    int16_t d = (int16_t)(G->span_x[y].right - l);
    if (d < 0) {
        l = (int16_t)(l + d);
        d = (int16_t)-d;
    }
    *x0 = l;
    *w = (int16_t)(d + 1);
    return (uint16_t)(y * 320 + l);
}

/* ------------------------------------------------------------------------
 * Span fillers
 * ---------------------------------------------------------------------- */

/* Flat polygon in poly_color.  A colour word >= 0xf0f0 is translucent:
 * its high byte - 0xf0 picks a row of the shade table.  Otherwise the two
 * bytes of the word are written alternately (a dither pattern when they
 * differ).  orig 1000:2bec / 1000:2d61 */
void draw_flat(void)
{
    if (!scan_outline(0))
        return;
    int16_t rows = (int16_t)(G->span_bottom - G->span_top);
    if (!rows)
        return;
    uint8_t *scr = screen();
    uint16_t color = G->poly_color;
    for (int16_t y = G->span_top; rows >= 0; rows--, y++) {
        int16_t x0, w;
        uint16_t o = span_extent(y, &x0, &w);
        if (color >= 0xf0f0) {
            const uint8_t *shade = G->shade_table[(uint8_t)((color >> 8) - 0xf0)];
            for (uint16_t i = 0; i < (uint16_t)w; i++, o++)
                scr[o] = shade[scr[o]];
        } else {
            for (uint16_t i = 0; i < (uint16_t)w; i++, o++)
                scr[o] = (i & 1) ? (uint8_t)(color >> 8) : (uint8_t)color;
        }
    }
}

/* Gouraud-shaded polygon, attribute a = colour in 8.8.  orig 1000:30ee / 1000:31d1 */
void draw_shaded(void)
{
    if (!scan_outline(1))
        return;
    const PolyVertex *v = G->poly_buf[0].v;
    uint16_t n = G->poly_buf[0].count;
    int16_t *tab = &G->span_a[0].left;
    for (uint16_t i = 0; i + 1 < n; i++)
        scan_edge_attr(tab, (int16_t)v[i].a, v[i].sy, (int16_t)v[i + 1].a, v[i + 1].sy);
    scan_edge_attr(tab, (int16_t)v[n - 1].a, v[n - 1].sy, (int16_t)v[0].a, v[0].sy);

    int16_t rows = (int16_t)(G->span_bottom - G->span_top);
    if (!rows)
        return;
    uint8_t *scr = screen();
    for (int16_t y = G->span_top; rows >= 0; rows--, y++) {
        int16_t a0 = G->span_a[y].left;
        int16_t da = (int16_t)(G->span_a[y].right - a0);
        if ((int16_t)(G->span_x[y].right - G->span_x[y].left) < 0) {
            a0 = (int16_t)(a0 + da);
            da = (int16_t)-da;
        }
        int16_t x0, w;
        uint16_t o = span_extent(y, &x0, &w);
        int32_t step = ((int32_t)da << 8) / (int32_t)(uint16_t)w;
        uint32_t a = (uint32_t)((int32_t)a0 << 8);
        for (uint16_t i = 0; i < (uint16_t)w; i++, o++) {
            scr[o] = (uint8_t)(a >> 16);
            a += (uint32_t)step;
        }
    }
}

/* Texture-mapped polygon (affine), attributes u, v in 8.8 texels of a
 * 256-wide texture.  Texel 0xff is transparent, 0xf0..0xfe darken what is
 * behind through the shade table.  orig 1000:36fe / 1000:3827 */
void draw_textured(void)
{
    if (!scan_outline(2))
        return;
    const PolyVertex *v = G->poly_buf[0].v;
    uint16_t n = G->poly_buf[0].count;
    for (uint16_t i = 0; i + 1 < n; i++)
        scan_edge_uv(v[i].sy, v[i + 1].sy, v[i].a, v[i].b, v[i + 1].a, v[i + 1].b);
    scan_edge_uv(v[n - 1].sy, v[0].sy, v[n - 1].a, v[n - 1].b, v[0].a, v[0].b);

    int16_t rows = (int16_t)(G->span_bottom - G->span_top);
    if (!rows)
        return;
    uint8_t *scr = screen();
    const uint8_t *tex = raster_texture;
    for (int16_t y = G->span_top; rows >= 0; rows--, y++) {
        int16_t u0 = G->span_a[y].left, du = (int16_t)(G->span_a[y].right - u0);
        int16_t v0 = G->span_b[y].left, dv = (int16_t)(G->span_b[y].right - v0);
        if ((int16_t)(G->span_x[y].right - G->span_x[y].left) < 0) {
            u0 = (int16_t)(u0 + du); du = (int16_t)-du;
            v0 = (int16_t)(v0 + dv); dv = (int16_t)-dv;
        }
        int16_t x0, w;
        uint16_t o = span_extent(y, &x0, &w);
        int32_t ustep = ((int32_t)du << 8) / (int32_t)(uint16_t)w;
        int32_t vstep = ((int32_t)dv << 8) / (int32_t)(uint16_t)w;
        uint32_t u = (uint32_t)(uint16_t)u0 << 8, vv = (uint32_t)(uint16_t)v0 << 8;
        for (uint16_t i = 0; i < (uint16_t)w; i++, o++) {
            uint8_t t = tex[(uint16_t)((u >> 16) + ((vv >> 16) << 8))];
            if (t != 0xff)
                scr[o] = t >= 0xf0 ? G->shade_table[t - 0xf0][scr[o]] : t;
            u += (uint32_t)ustep;
            vv += (uint32_t)vstep;
        }
    }
}

/* Is poly_buf[0] (its first three vertices) facing away, i.e. clockwise
 * on screen?  orig 1000:2662 */
int poly_backfacing(void)
{
    const PolyVertex *v = G->poly_buf[0].v;
    int32_t a = (int32_t)(int16_t)(v[0].sx - v[1].sx) * (int16_t)(v[2].sy - v[1].sy);
    int32_t b = (int32_t)(int16_t)(v[2].sx - v[1].sx) * (int16_t)(v[0].sy - v[1].sy);
    return (int32_t)((uint32_t)b - (uint32_t)a) < 0;
}

/* ------------------------------------------------------------------------
 * Building polygons from camera-space points, clipped at the near plane
 * ---------------------------------------------------------------------- */

/* State of the near clipper: the previous point (orig DS e990..e996) */
static struct {
    int16_t x, dist, z;
    uint16_t count;
} nc;

/* Projection of a point lying on the near plane.  The original uses the
 * full-screen centre here even in split screen. */
static void near_point(PolyVertex *out, int16_t x, int16_t z, uint32_t attr)
{
    int16_t sx, sy;
    project(x, G->near_clip, z, &sx, &sy);
    out->sx = (int16_t)(sx + 160);
    out->sy = (int16_t)(100 - sy);
    out->a = (uint16_t)attr;
    out->b = (uint16_t)(attr >> 16);
}

static void emit_projected(const ProjVertex *p, uint32_t attr)
{
    PolyVertex *o = &G->poly_buf[0].v[nc.count++];
    o->sx = p->sx;
    o->sy = p->sy;
    o->a = (uint16_t)attr;
    o->b = (uint16_t)(attr >> 16);
}

/* First point of a polygon.  attr is (b << 16 | a).  orig 1000:46a0 */
void poly_begin(const ProjVertex *p, uint32_t attr)
{
    nc.count = 0;
    nc.dist = (int16_t)(p->y - G->near_clip);
    if (nc.dist >= 0)
        emit_projected(p, attr);
    nc.x = p->x;
    nc.z = p->z;
}

/* Next point.  Where the outline crosses the near plane a point on the
 * plane is inserted; it takes the attribute of the current point.
 * orig 1000:46d3 */
void poly_add(const ProjVertex *p, uint32_t attr)
{
    int16_t d = (int16_t)(p->y - G->near_clip);
    if (nc.dist >= 0) {
        if (d >= 0) {
            emit_projected(p, attr);
        } else if (nc.dist != 0) {
            near_point(&G->poly_buf[0].v[nc.count++],
                       edge_lerp(d, p->x, nc.dist, nc.x),
                       edge_lerp(d, p->z, nc.dist, nc.z), attr);
        }
    } else if (d >= 0) {
        near_point(&G->poly_buf[0].v[nc.count++],
                   edge_lerp(nc.dist, nc.x, d, p->x),
                   edge_lerp(nc.dist, nc.z, d, p->z), attr);
        emit_projected(p, attr);
    }
    nc.dist = d;
    nc.x = p->x;
    nc.z = p->z;
}

/* Closing point (normally the first one again): only the crossing is
 * added.  Sets the vertex count; returns it.  orig 1000:47ec */
uint16_t poly_end(const ProjVertex *p, uint32_t attr)
{
    int16_t d = (int16_t)(p->y - G->near_clip);
    if (nc.dist >= 0) {
        if (d < 0 && nc.dist != 0)
            near_point(&G->poly_buf[0].v[nc.count++],
                       edge_lerp(d, p->x, nc.dist, nc.x),
                       edge_lerp(d, p->z, nc.dist, nc.z), attr);
    } else if (d >= 0) {
        near_point(&G->poly_buf[0].v[nc.count++],
                   edge_lerp(nc.dist, nc.x, d, p->x),
                   edge_lerp(nc.dist, nc.z, d, p->z), attr);
    }
    G->poly_buf[0].count = nc.count;
    return nc.count;
}
