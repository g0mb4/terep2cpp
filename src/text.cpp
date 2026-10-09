/*
 * Bitmap font text.  Each glyph is a list of column bytes (bit 0 = top
 * row) ended by a zero byte; font_offset[c] locates glyph c in
 * font_glyphs (offsets in the data segment, see dataseg_init.cpp).
 */
#include "terep.hpp"

/* Control bytes: 0 or 13 end the string, 9 is a tab (20 pixels),
 * 27 followed by a byte changes the colour, space is 5 pixels wide.
 * orig 1000:5940 */
void draw_text(const char *s, int16_t x, int16_t y, uint8_t color)
{
    const uint16_t font = (uint16_t)offsetof(DataSeg, font_offset);
    const uint16_t glyphs = (uint16_t)offsetof(DataSeg, font_glyphs);
    for (;;) {
        uint8_t ch = (uint8_t)*s++;
        if (ch == 0 || ch == 13)
            return;
        if (ch == 9) {
            x = (int16_t)(x + 20);
            continue;
        }
        if (ch == 27) {
            color = (uint8_t)*s++;
            continue;
        }
        if (ch == ' ') {
            x = (int16_t)(x + 5);
            continue;
        }
        /* 16-bit offsets: characters >= 128 index past the table and
         * wrap around the data segment, as in the original.  Only the
         * title can hold such characters (see Car::jump); their "glyphs"
         * may come from past the end of the original data segment
         * image, which differs between builds (the Win16 port keeps its
         * own variables there), so those few columns of the title are not
         * reproduced exactly. */
        uint16_t col = (uint16_t)(glyphs + *(const uint16_t *)ds_ptr((uint16_t)(font + ch * 2)));
        for (; *ds_ptr(col); col++, x++) {
            int16_t row = y;
            for (uint8_t bits = *ds_ptr(col); bits; bits >>= 1, row++)
                if (bits & 1)
                    put_block((int16_t)(x * render_scale), (int16_t)(row * render_scale), color);
        }
        x++;                            /* one column of spacing */
    }
}
