// The files of the loaded map, by the names the game uses (its data segment).
#include <filesystem>
#include "renderer_api.hpp"
#include "voxel_preview.hpp"            // find_file_nocase

MapFiles map_files(const std::string &dir)
{
    MapFiles f;
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(dir, ec).lexically_normal();
    if (!abs.has_filename())                    // "dir/" or "dir/."
        abs = abs.parent_path();
    f.dir = ec ? dir : abs.string();
    auto find = [&](const char *name) { return find_file_nocase(f.dir, name); };
    f.map = find(G->fname_map);
    f.col = find(G->fname_col);
    f.maptex = find(G->fname_maptex);
    f.textures = find(G->fname_textures);
    f.fog = find(G->fname_fog);
    f.smoke = find(G->fname_smoke);
    for (int i = 0; i < G->num_cars && i < 5; i++) {
        MapFiles::Car c;
        c.dat = find((const char *)ds_ptr(G->car_dat_name[i]));
        if (car_get(i).texture_seg)
            c.pcx = find((const char *)ds_ptr(G->car_pcx_name[i]));
        f.cars.push_back(c);
    }
    return f;
}
