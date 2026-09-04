// =============================================================================
//  engine/tilemap/map2.cpp
// =============================================================================
#include "engine/tilemap/map2.hpp"

#include <sstream>

namespace tilemap {
namespace {

// Layer names the engine gives meaning to. Everything else is decoration the
// renderer draws and nothing else interprets.
constexpr const char* kCollide = "collide";

std::vector<Property> parse_props(std::istringstream& in) {
    // Properties run to the end of the line as `key=value` words. Values may not
    // contain spaces — a deliberate limit that keeps the whole format parseable with
    // a stream and greppable by eye; a value that needs spaces wants its own file.
    std::vector<Property> out;
    std::string line;
    std::getline(in, line);
    std::istringstream ls(line);
    std::string word;
    while (ls >> word) {
        const auto eq = word.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        out.push_back(Property{word.substr(0, eq), word.substr(eq + 1)});
    }
    return out;
}

void write_props(std::string& s, const std::vector<Property>& props) {
    for (const auto& p : props) s += " " + p.key + "=" + p.value;
}

} // namespace

std::string prop(const std::vector<Property>& props, const std::string& key,
                 const std::string& fallback) {
    for (const auto& p : props)
        if (p.key == key) return p.value;
    return fallback;
}

const Layer* Map::layer(const std::string& n) const {
    for (const auto& l : layers)
        if (l.name == n) return &l;
    return nullptr;
}

Layer* Map::layer(const std::string& n) {
    for (auto& l : layers)
        if (l.name == n) return &l;
    return nullptr;
}

std::int32_t Map::at(const std::string& layer_name, int x, int y) const {
    if (!in_bounds(x, y)) return 0;
    const Layer* l = layer(layer_name);
    if (!l) return 0;
    return l->cells[static_cast<std::size_t>(y) * w + x];
}

void Map::set(const std::string& layer_name, int x, int y, std::int32_t id) {
    if (!in_bounds(x, y)) return;
    Layer* l = layer(layer_name);
    if (!l) return;
    l->cells[static_cast<std::size_t>(y) * w + x] = id;
}

bool Map::solid(int x, int y) const {
    if (!in_bounds(x, y)) return true;     // outside the world is a wall
    const Layer* l = layer(kCollide);
    if (!l) return false;                  // no collision layer: nothing blocks
    return l->cells[static_cast<std::size_t>(y) * w + x] != 0;
}

std::vector<const Trigger*> Map::triggers_at(int qx, int qy, int qw, int qh) const {
    std::vector<const Trigger*> out;
    for (const auto& t : triggers) {
        const bool overlap = qx < t.x + t.w && t.x < qx + qw &&
                             qy < t.y + t.h && t.y < qy + qh;
        if (overlap) out.push_back(&t);
    }
    return out;
}

const Entity* Map::entity(const std::string& n) const {
    for (const auto& e : entities)
        if (e.name == n) return &e;
    return nullptr;
}

std::string to_text(const Map& m) {
    std::string s = "map2 " + std::to_string(kFormatVersion) + "\n";
    s += "name " + (m.name.empty() ? std::string("untitled") : m.name) + "\n";
    s += "size " + std::to_string(m.w) + " " + std::to_string(m.h) + "\n";
    s += "tile " + std::to_string(m.tile) + "\n";
    for (const auto& t : m.tilesets) s += "tileset " + t.name + " " + t.path + "\n";

    for (const auto& l : m.layers) {
        s += "layer " + l.name + " ";
        s += (l.kind == LayerKind::Mask) ? "mask" : ("tiles " + l.tileset);
        s += "\n";
        for (int y = 0; y < m.h; ++y) {
            s += "row";
            for (int x = 0; x < m.w; ++x)
                s += " " + std::to_string(l.cells[static_cast<std::size_t>(y) * m.w + x]);
            s += "\n";
        }
    }
    for (const auto& e : m.entities) {
        s += "entity " + e.name + " " + std::to_string(e.x) + " " + std::to_string(e.y);
        write_props(s, e.props);
        s += "\n";
    }
    for (const auto& t : m.triggers) {
        s += "trigger " + t.name + " " + std::to_string(t.x) + " " + std::to_string(t.y) +
             " " + std::to_string(t.w) + " " + std::to_string(t.h);
        write_props(s, t.props);
        s += "\n";
    }
    return s;
}

namespace {

std::optional<Map> parse_map2(std::istringstream& in) {
    int version = 0;
    if (!(in >> version)) return std::nullopt;
    // Refuse a file from the future rather than guessing at it. Reading a newer
    // schema with an older parser is how a tool silently drops the fields it does
    // not know about and then writes the loss back to disk.
    if (version <= 0 || version > kFormatVersion) return std::nullopt;

    Map m;
    bool have_size = false;
    std::string tok;
    while (in >> tok) {
        if (tok == "name") {
            if (!(in >> m.name)) return std::nullopt;
        } else if (tok == "size") {
            if (!(in >> m.w >> m.h) || m.w <= 0 || m.h <= 0) return std::nullopt;
            have_size = true;
        } else if (tok == "tile") {
            if (!(in >> m.tile) || m.tile <= 0) return std::nullopt;
        } else if (tok == "tileset") {
            TilesetRef t;
            if (!(in >> t.name >> t.path)) return std::nullopt;
            m.tilesets.push_back(std::move(t));
        } else if (tok == "layer") {
            if (!have_size) return std::nullopt;      // rows cannot be read without w/h
            Layer l;
            std::string kind;
            if (!(in >> l.name >> kind)) return std::nullopt;
            if (kind == "mask") {
                l.kind = LayerKind::Mask;
            } else if (kind == "tiles") {
                l.kind = LayerKind::Tiles;
                if (!(in >> l.tileset)) return std::nullopt;
            } else {
                return std::nullopt;
            }
            l.cells.assign(static_cast<std::size_t>(m.w) * m.h, 0);
            for (int y = 0; y < m.h; ++y) {
                std::string row;
                if (!(in >> row) || row != "row") return std::nullopt;
                for (int x = 0; x < m.w; ++x) {
                    long long v = 0;
                    if (!(in >> v)) return std::nullopt;
                    l.cells[static_cast<std::size_t>(y) * m.w + x] = static_cast<std::int32_t>(v);
                }
            }
            m.layers.push_back(std::move(l));
        } else if (tok == "entity") {
            Entity e;
            if (!(in >> e.name >> e.x >> e.y)) return std::nullopt;
            e.props = parse_props(in);
            m.entities.push_back(std::move(e));
        } else if (tok == "trigger") {
            Trigger t;
            if (!(in >> t.name >> t.x >> t.y >> t.w >> t.h)) return std::nullopt;
            t.props = parse_props(in);
            m.triggers.push_back(std::move(t));
        } else {
            return std::nullopt;                      // unknown directive: refuse
        }
    }
    if (!have_size) return std::nullopt;
    return m;
}

} // namespace

std::optional<Map> from_fpsmap1(const std::string& text) {
    std::istringstream in(text);
    std::string tok;
    if (!(in >> tok) || tok != "fpsmap1") return std::nullopt;
    if (!(in >> tok) || tok != "size")    return std::nullopt;

    Map m;
    m.name = "migrated";
    m.tile = 16;
    if (!(in >> m.w >> m.h) || m.w <= 0 || m.h <= 0) return std::nullopt;

    // fpsmap1 packs everything into one grid: id 0 is empty floor, anything else is
    // a solid wall whose number picks a texture. That is two facts in one number, so
    // the migration splits them — the ids become a `wall` tile layer and the
    // "is it solid" bit becomes the `collide` mask the rest of the engine reads.
    Layer wall{"wall", LayerKind::Tiles, "wall", {}};
    Layer collide{"collide", LayerKind::Mask, "", {}};
    wall.cells.assign(static_cast<std::size_t>(m.w) * m.h, 0);
    collide.cells.assign(static_cast<std::size_t>(m.w) * m.h, 0);

    for (int y = 0; y < m.h; ++y) {
        if (!(in >> tok) || tok != "row") return std::nullopt;
        for (int x = 0; x < m.w; ++x) {
            int v = 0;
            if (!(in >> v) || v < 0 || v > 255) return std::nullopt;
            const std::size_t i = static_cast<std::size_t>(y) * m.w + x;
            wall.cells[i]    = v;
            collide.cells[i] = (v != 0) ? 1 : 0;
        }
    }
    m.layers.push_back(std::move(wall));
    m.layers.push_back(std::move(collide));

    // The optional spawn line becomes an entity, which is how every other authored
    // point in map2 is expressed. Facing is kept verbatim so a migrated level starts
    // exactly where and how it did before.
    if (in >> tok && tok == "spawn") {
        int cx = 0, cy = 0;
        float dir = 0.0f;
        if ((in >> cx >> cy >> dir) && cx >= 0 && cy >= 0 && cx < m.w && cy < m.h) {
            Entity e;
            e.name = "spawn_player";
            e.x = cx;
            e.y = cy;
            e.props.push_back(Property{"dir", std::to_string(dir)});
            m.entities.push_back(std::move(e));
        }
    }
    return m;
}

std::optional<Map> load(const std::string& text) {
    std::istringstream probe(text);
    std::string magic;
    if (!(probe >> magic)) return std::nullopt;
    if (magic == "fpsmap1") return from_fpsmap1(text);
    if (magic != "map2")    return std::nullopt;
    std::istringstream in(text);
    in >> magic;                                      // consume "map2"
    return parse_map2(in);
}

} // namespace tilemap
