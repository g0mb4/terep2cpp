/*
 * Car files (carN.dat): reading them into a Car, and writing a Car back in
 * the same format.
 *
 *   0x00  uint16 vertices    offset of: uint16 count, CarVertex[count] (0x1c bytes each)
 *   0x02  uint16 springs     offset of: uint16 count, CarSpring[count] (0x0e bytes each)
 *   0x04  uint16 model       offset of the drawing program (to the end of the file)
 *   0x06  uint16 ?
 *   0x08  uint16 drive       0 rear, 1 front, 2 all wheels
 *   0x0a  ...    the game's own state for the car (zero in the files):
 *         steer, throttle, handbrake, mid[3], steer_used, throttle_used,
 *         cell, ?, texture_seg, ref_vertex (offset), lowest_vertex (DS
 *         offset), 0x1e bytes ?, wheel_speed[4], spin[12], ?
 *   0x84  the sections, normally in the order above
 *
 * The original copied the file into the data segment and used it in place,
 * so the header doubles as the car's state.  car_write produces that image
 * again: the tests compare it with the original's data segment.
 */
#include "terep.hpp"

Car cars[10];

namespace {

/* Little-endian reads that stay inside the file. */
struct Reader {
    const uint8_t *p;
    size_t size;

    uint16_t u16(size_t at) const { return at + 2 <= size ? (uint16_t)(p[at] | p[at + 1] << 8) : 0; }
    int16_t  i16(size_t at) const { return (int16_t)u16(at); }
    uint32_t u32(size_t at) const { return (uint32_t)u16(at) | (uint32_t)u16(at + 2) << 16; }
    int32_t  i32(size_t at) const { return (int32_t)u32(at); }
};

struct Writer {
    std::vector<uint8_t> &out;

    void u16(size_t at, uint16_t v) { out[at] = (uint8_t)v; out[at + 1] = (uint8_t)(v >> 8); }
    void u32(size_t at, uint32_t v) { u16(at, (uint16_t)v); u16(at + 2, (uint16_t)(v >> 16)); }
};

CarVertex read_vertex(const Reader &r, size_t at)
{
    CarVertex v;
    v.pos = { { r.u32(at) }, { r.u32(at + 4) }, { r.u32(at + 8) } };
    v.vel = { r.i32(at + 12), r.i32(at + 16), r.i32(at + 20) };
    v.radius = r.i16(at + 24);
    v.wheel = r.u16(at + 26);
    return v;
}

CarSpring read_spring(const Reader &r, size_t at)
{
    CarSpring s;
    s.a = r.u16(at);
    s.b = r.u16(at + 2);
    s.rest = r.i16(at + 4);
    s.last = r.i16(at + 6);
    s.flags = r.u16(at + 8);
    s.min_len = r.u16(at + 10);
    s.max_len = r.u16(at + 12);
    return s;
}

}  // namespace

/* Read a car file.  Returns its size as the original counts it (at most
 * MAX_SIZE bytes are read), 0 when it cannot be read. */
uint16_t car_read(Car &car, FILE *f)
{
    std::vector<uint8_t> file(carfile::MAX_SIZE);
    size_t size = fread(file.data(), 1, file.size(), f);
    if (size == 0)
        return 0;
    Reader r = { file.data(), size };

    car = Car();
    size_t vo = r.u16(0), so = r.u16(2), mo = r.u16(4);
    car.unk_06 = r.u16(6);
    car.drive = r.u16(8);
    car.steer = { r.i16(0x0a) };
    car.throttle = r.i16(0x0c);
    car.handbrake = r.u16(0x0e);
    for (int i = 0; i < 3; i++)
        car.mid[i] = r.i16(0x10 + 2 * i);
    car.steer_used = { r.i16(0x16) };
    car.throttle_used = r.i16(0x18);
    car.cell = r.u16(0x1a);
    car.texture_seg = r.u16(0x1e);
    for (int i = 0; i < 4; i++)
        car.wheel_speed[i] = r.i32(0x42 + 4 * i);
    for (int i = 0; i < 12; i++)
        car.spin[i] = r.i32(0x52 + 4 * i);

    for (uint16_t i = 0, n = r.u16(vo); i < n && vo + 2 + (i + 1) * carfile::VERTEX <= size; i++)
        car.points.push_back(read_vertex(r, vo + 2 + i * carfile::VERTEX));
    for (uint16_t i = 0, n = r.u16(so); i < n && so + 2 + (i + 1) * carfile::SPRING <= size; i++)
        car.spring_list.push_back(read_spring(r, so + 2 + i * carfile::SPRING));
    if (mo < size)
        car.model.assign(file.begin() + (ptrdiff_t)mo, file.begin() + (ptrdiff_t)size);
    return (uint16_t)size;
}

/* The car in the file format, as the original holds it at data segment
 * offset ds_base (lowest_vertex is an offset in the data segment).  The
 * sections are written one after the other. */
std::vector<uint8_t> car_write(const Car &car, uint16_t ds_base)
{
    size_t vo = carfile::HEADER;
    size_t so = vo + 2 + car.points.size() * carfile::VERTEX;
    size_t mo = so + 2 + car.spring_list.size() * carfile::SPRING;
    std::vector<uint8_t> out(mo + car.model.size());
    Writer w = { out };
    auto vertex_ofs = [&](size_t i) { return (uint16_t)(vo + 2 + i * carfile::VERTEX); };

    w.u16(0, (uint16_t)vo);
    w.u16(2, (uint16_t)so);
    w.u16(4, (uint16_t)mo);
    w.u16(6, car.unk_06);
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
    memcpy(out.data() + mo, car.model.data(), car.model.size());
    return out;
}

/* Put the cars into a copy of the data segment where the original keeps
 * them (car_ofs), so it can be compared with the original's. */
void cars_to_dataseg(uint8_t *ds)
{
    for (int i = 0; i < G->num_cars; i++) {
        std::vector<uint8_t> image = car_write(cars[i], G->car_ofs[i]);
        for (size_t k = 0; k < image.size(); k++)
            ds[(uint16_t)(G->car_ofs[i] + k)] = image[k];
    }
}
