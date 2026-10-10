// 8-bit, one plane, RLE PCX (what the map files use).
#include <cstdio>
#include "pcx.hpp"

bool PcxImage::load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    std::vector<uint8_t> file;
    uint8_t buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
        file.insert(file.end(), buf, buf + n);
    fclose(f);
    if (file.size() < 128 || file[0] != 10 || file[2] != 1 || file[3] != 8 || file[65] != 1)
        return false;

    auto u16 = [&](size_t at) { return file[at] | file[at + 1] << 8; };
    width = u16(8) - u16(4) + 1;
    height = u16(10) - u16(6) + 1;
    int line = u16(66);                 // bytes per line, may be padded past width
    if (width <= 0 || height <= 0 || line < width)
        return false;

    pixels.assign((size_t)width * height, 0);
    size_t in = 128;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < line && in < file.size();) {
            uint8_t v = file[in++], n = 1;
            if ((v & 0xc0) == 0xc0 && in < file.size()) {
                n = v & 0x3f;
                v = file[in++];
            }
            for (; n && x < line; n--, x++)
                if (x < width)
                    pixels[(size_t)y * width + x] = v;
        }
    }

    palette.clear();
    if (file.size() >= 128 + 769 && file[file.size() - 769] == 12)
        palette.assign(file.end() - 768, file.end());
    return true;
}
