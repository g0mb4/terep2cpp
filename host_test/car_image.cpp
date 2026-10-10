/*
 * Writing the cars back in the car file format, at the place of the data
 * segment where the original keeps them (car_pool), so the test host can
 * compare the whole segment with the original's.  The layout of the file
 * is described in src/car_file.cpp.
 */
#include <vector>
#include "car_image.hpp"
#include "../host/host.hpp"

namespace {

struct Writer {
    std::vector<uint8_t> &out;

    void u16(size_t at, uint16_t v) { out[at] = (uint8_t)v; out[at + 1] = (uint8_t)(v >> 8); }
    void u32(size_t at, uint32_t v) { u16(at, (uint16_t)v); u16(at + 2, (uint16_t)(v >> 16)); }
};

/* What the game does not keep of a car file. */
struct CarFileRest {
    uint16_t unk_06 = 0;            /* header word of unknown use */
    std::vector<uint8_t> model;     /* the drawing program, as in the file */
};

CarFileRest car_rest[sizeof cars / sizeof cars[0]];

}  // namespace

/* Reads the files the way car_read does (at most MAX_SIZE bytes). */
bool car_files_read()
{
    for (int i = 0; i < G->num_cars; i++) {
        const char *name = (const char *)ds_ptr(G->car_dat_name[i]);
        FILE *f = data_fopen(name);
        if (!f)
            return false;
        std::vector<uint8_t> file(carfile::MAX_SIZE);
        size_t size = fread(file.data(), 1, file.size(), f);
        fclose(f);
        size_t mo = size >= 6 ? (size_t)(file[4] | file[5] << 8) : size;
        car_rest[i].unk_06 = size >= 8 ? (uint16_t)(file[6] | file[7] << 8) : 0;
        car_rest[i].model.assign(file.begin() + (ptrdiff_t)(mo < size ? mo : size), file.begin() + (ptrdiff_t)size);
    }
    return true;
}

/* The car in the file format, as the original holds it at data segment
 * offset ds_base (lowest_vertex is an offset in the data segment).  The
 * sections are written one after the other. */
static std::vector<uint8_t> car_write(const Car &car, const CarFileRest &rest, uint16_t ds_base)
{
    size_t vo = carfile::HEADER;
    size_t so = vo + 2 + car.points.size() * carfile::VERTEX;
    size_t mo = so + 2 + car.spring_list.size() * carfile::SPRING;
    std::vector<uint8_t> out(mo + rest.model.size());
    Writer w = { out };
    auto vertex_ofs = [&](size_t i) { return (uint16_t)(vo + 2 + i * carfile::VERTEX); };

    w.u16(0, (uint16_t)vo);
    w.u16(2, (uint16_t)so);
    w.u16(4, (uint16_t)mo);
    w.u16(6, rest.unk_06);
    w.u16(8, car.drive);
    w.u16(0x0a, car.steer.bits());
    w.u16(0x0c, (uint16_t)car.throttle);
    w.u16(0x0e, car.handbrake);
    for (int i = 0; i < 3; i++)
        w.u16(0x10 + 2 * i, (uint16_t)car.mid[i]);
    w.u16(0x16, car.steer_used.bits());
    w.u16(0x18, (uint16_t)car.throttle_used);
    w.u16(0x1a, car.cell);
    w.u16(0x1e, car.texture_seg);
    w.u16(0x20, vertex_ofs(car.ref_index));
    w.u16(0x22, car.lowest < 0 ? 0 : (uint16_t)(ds_base + vertex_ofs((size_t)car.lowest)));
    for (int i = 0; i < 4; i++)
        w.u32(0x42 + 4 * i, (uint32_t)car.wheel_speed[i]);
    for (int i = 0; i < 12; i++)
        w.u32(0x52 + 4 * i, (uint32_t)car.spin[i]);

    w.u16(vo, (uint16_t)car.points.size());
    for (size_t i = 0; i < car.points.size(); i++) {
        const CarVertex &v = car.points[i];
        size_t at = vertex_ofs(i);
        w.u32(at, v.pos.x.raw);
        w.u32(at + 4, v.pos.y.raw);
        w.u32(at + 8, v.pos.z.raw);
        w.u32(at + 12, (uint32_t)v.vel.x);
        w.u32(at + 16, (uint32_t)v.vel.y);
        w.u32(at + 20, (uint32_t)v.vel.z);
        w.u16(at + 24, (uint16_t)v.radius);
        w.u16(at + 26, v.wheel);
    }
    w.u16(so, (uint16_t)car.spring_list.size());
    for (size_t i = 0; i < car.spring_list.size(); i++) {
        const CarSpring &s = car.spring_list[i];
        size_t at = so + 2 + i * carfile::SPRING;
        w.u16(at, s.a);
        w.u16(at + 2, s.b);
        w.u16(at + 4, (uint16_t)s.rest);
        w.u16(at + 6, (uint16_t)s.last);
        w.u16(at + 8, s.flags);
        w.u16(at + 10, s.min_len);
        w.u16(at + 12, s.max_len);
    }
    memcpy(out.data() + mo, rest.model.data(), rest.model.size());
    return out;
}

/* Put the cars into a copy of the data segment where the original keeps
 * them (car_ofs), so it can be compared with the original's. */
void cars_to_dataseg(uint8_t *ds)
{
    for (int i = 0; i < G->num_cars; i++) {
        std::vector<uint8_t> image = car_write(cars[i], car_rest[i], G->car_ofs[i]);
        for (size_t k = 0; k < image.size(); k++)
            ds[(uint16_t)(G->car_ofs[i] + k)] = image[k];
    }
}
