// =============================================================================
//  games/iso/serialize.cpp  —  Farm save/load implementation
// =============================================================================
#include "games/iso/serialize.hpp"

#include <sstream>
#include <string>
#include <utility>

namespace iso {
namespace {

char terrain_char(Terrain t) {
    switch (t) {
        case Terrain::Grass: return '.';
        case Terrain::Soil:  return ':';
        case Terrain::Water: return '~';
        case Terrain::Path:  return '#';
    }
    return '.';
}

Terrain terrain_from(char c) {
    switch (c) {
        case ':': return Terrain::Soil;
        case '~': return Terrain::Water;
        case '#': return Terrain::Path;
        default:  return Terrain::Grass;   // '.' and anything unexpected
    }
}

// Only the placeable kinds round-trip through the OBJECTS section. The farmer is
// stored separately, so it has no char here.
char objkind_char(ObjKind k) {
    switch (k) {
        case ObjKind::Tree:  return 'T';
        case ObjKind::Rock:  return 'R';
        case ObjKind::House: return 'H';
        case ObjKind::Fence: return 'F';
        case ObjKind::Wheat: return 'W';
        case ObjKind::Farmer: return '@';   // never written, but keeps the switch total
    }
    return '?';
}

bool objkind_from(char c, ObjKind& out) {
    switch (c) {
        case 'T': out = ObjKind::Tree;  return true;
        case 'R': out = ObjKind::Rock;  return true;
        case 'H': out = ObjKind::House; return true;
        case 'F': out = ObjKind::Fence; return true;
        case 'W': out = ObjKind::Wheat; return true;
        default:  return false;
    }
}

} // namespace

std::vector<uint8_t> save_farm(const Farm& f) {
    std::ostringstream s;
    s << "FARM 1\n";
    s << "SIZE " << f.width() << ' ' << f.height() << "\n";

    s << "TILES\n";
    for (int y = 0; y < f.height(); ++y) {
        for (int x = 0; x < f.width(); ++x) s << terrain_char(f.terrain_at(x, y));
        s << '\n';
    }

    // Collect placeable objects from the ECS (skip the farmer + its kind).
    const World& w = f.world();
    std::ostringstream objs;
    int count = 0;
    for (const Entity e : w.alive()) {
        if (e == f.farmer()) continue;
        const Renderable* r = w.renderables.get(e);
        const Position*   p = w.positions.get(e);
        if (!r || !p || r->kind == ObjKind::Farmer) continue;
        objs << objkind_char(r->kind) << ' '
             << static_cast<int>(p->x) << ' ' << static_cast<int>(p->y) << '\n';
        ++count;
    }
    s << "OBJECTS " << count << '\n' << objs.str();

    if (f.farmer() != kInvalid) {
        const Vec2i c = f.farmer_cell();
        s << "FARMER " << c.x << ' ' << c.y << '\n';
    } else {
        s << "FARMER -1 -1\n";
    }

    const std::string str = s.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

bool load_farm(Farm& f, const std::vector<uint8_t>& bytes) {
    std::string s(bytes.begin(), bytes.end());
    std::istringstream in(s);
    std::string tok;
    int ver = 0;

    if (!(in >> tok >> ver) || tok != "FARM" || ver != 1) return false;

    int w = 0, h = 0;
    if (!(in >> tok >> w >> h) || tok != "SIZE") return false;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    if (!(in >> tok) || tok != "TILES") return false;

    // Build into a temporary so a malformed file never corrupts the live farm.
    Farm tmp(w, h);
    for (int y = 0; y < h; ++y) {
        std::string row;
        if (!(in >> row) || static_cast<int>(row.size()) != w) return false;
        for (int x = 0; x < w; ++x) tmp.set_terrain(x, y, terrain_from(row[x]));
    }

    int n = 0;
    if (!(in >> tok >> n) || tok != "OBJECTS" || n < 0) return false;
    for (int i = 0; i < n; ++i) {
        std::string kc;
        int x = 0, y = 0;
        if (!(in >> kc >> x >> y) || kc.size() != 1) return false;
        ObjKind k;
        if (!objkind_from(kc[0], k)) return false;
        tmp.place_object(x, y, k);   // out-of-bounds silently ignored by Farm
    }

    if (!(in >> tok) || tok != "FARMER") return false;
    int fx = -1, fy = -1;
    if (!(in >> fx >> fy)) return false;
    if (fx >= 0 && fy >= 0) tmp.spawn_farmer(fx, fy);

    f = std::move(tmp);   // commit
    return true;
}

} // namespace iso
