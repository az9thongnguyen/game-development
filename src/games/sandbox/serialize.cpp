// =============================================================================
//  games/sandbox/serialize.cpp
// =============================================================================
#include "games/sandbox/serialize.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace sandbox {

namespace {

// Split on spaces at bracket-depth 0, so a "[...]" proto group stays one token.
std::vector<std::string> split_tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '[') { ++depth; cur += c; }
        else if (c == ']') { --depth; cur += c; }
        else if (c == ' ' && depth == 0) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string key_of(const std::string& tok) {
    const auto eq = tok.find('=');
    return eq == std::string::npos ? tok : tok.substr(0, eq);
}
std::string val_of(const std::string& tok) {
    const auto eq = tok.find('=');
    return eq == std::string::npos ? std::string() : tok.substr(eq + 1);
}

float to_f(const std::string& s) { try { return std::stof(s); } catch (...) { return 0.0f; } }
int   to_i(const std::string& s) { try { return std::stoi(s); } catch (...) { return 0; } }

// Minimal, stable float text: %g so emit(parse(emit(x))) == emit(x) for our values.
std::string fmt_f(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%g", double(v));
    return b;
}

std::string hex_of(gfx::Color c) {
    char b[8];
    std::snprintf(b, sizeof(b), "%06x", unsigned(c & 0xFFFFFFu));
    return b;
}
gfx::Color parse_hex(const std::string& s) {
    unsigned v = 0;
    try { v = unsigned(std::stoul(s, nullptr, 16)); } catch (...) { v = 0; }
    return gfx::rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// Content between the first '[' and the last ']', or "" if unbracketed.
std::string strip_brackets(const std::string& s) {
    const auto a = s.find('[');
    const auto b = s.rfind(']');
    if (a == std::string::npos || b == std::string::npos || b <= a) return {};
    return s.substr(a + 1, b - a - 1);
}

} // namespace

std::string archetype_tokens(const Archetype& a) {
    std::string s = "color=" + hex_of(a.color);
    if (a.round) s += " round";
    s += " w=" + fmt_f(a.w) + " h=" + fmt_f(a.h);
    if (a.tag)      s += " tag=" + std::to_string(a.tag);
    if (a.mover)    s += " mover=" + fmt_f(a.vx) + "," + fmt_f(a.vy);
    if (a.spinner)  s += " spinner=" + fmt_f(a.omega);
    if (a.bouncer)  s += " bouncer";
    if (a.lifetime) s += " lifetime=" + fmt_f(a.ttl);
    if (!a.texture.empty()) s += " tex=" + a.texture;
    return s;
}

Archetype parse_archetype(const std::string& tokens) {
    Archetype a;
    for (const auto& tok : split_tokens(tokens)) {
        const std::string k = key_of(tok), v = val_of(tok);
        if      (k == "color")   a.color = parse_hex(v);
        else if (k == "round")   a.round = true;
        else if (k == "w")       a.w = to_f(v);
        else if (k == "h")       a.h = to_f(v);
        else if (k == "tag")     a.tag = to_i(v);
        else if (k == "mover") {
            a.mover = true;
            const auto c = v.find(',');
            a.vx = to_f(v.substr(0, c));
            a.vy = to_f(c == std::string::npos ? std::string("0") : v.substr(c + 1));
        }
        else if (k == "spinner") { a.spinner = true; a.omega = to_f(v); }
        else if (k == "bouncer") a.bouncer = true;
        else if (k == "lifetime"){ a.lifetime = true; a.ttl = to_f(v); }
        else if (k == "tex")     a.texture = v;
    }
    return a;
}

std::string to_scene(const World& w) {
    // ponytail: const_cast only to reach Registry's non-const view/get; the lambda
    // below strictly reads — no structural or component mutation happens here.
    World& mw = const_cast<World&>(w);
    std::string out = "sandbox1\n";
    out += "bounds " + fmt_f(w.bounds_w) + " " + fmt_f(w.bounds_h) + "\n";

    mw.reg.view<Transform2D>([&](ecs::Entity e, Transform2D& t) {
        Archetype a;
        if (Body*    b = mw.reg.get<Body>(e))    { a.w = b->w; a.h = b->h; }
        if (Sprite*  s = mw.reg.get<Sprite>(e))  { a.color = s->color; a.round = s->round; a.texture = s->texture; }
        if (Mover*   m = mw.reg.get<Mover>(e))   { a.mover = true; a.vx = m->vx; a.vy = m->vy; }
        if (Spinner* s = mw.reg.get<Spinner>(e)) { a.spinner = true; a.omega = s->omega; }
        if (mw.reg.has<Bouncer>(e))              a.bouncer = true;
        if (Lifetime* l = mw.reg.get<Lifetime>(e)){ a.lifetime = true; a.ttl = l->ttl; }
        if (Tag* g = mw.reg.get<Tag>(e))         a.tag = g->id;

        std::string line = "e x=" + fmt_f(t.x) + " y=" + fmt_f(t.y) + " rot=" + fmt_f(t.rot)
                         + " " + archetype_tokens(a);
        if (Spawner* sp = mw.reg.get<Spawner>(e))
            line += " spawner=" + fmt_f(sp->interval) + ":[" + archetype_tokens(sp->proto) + "]";
        if (OnOverlap* o = mw.reg.get<OnOverlap>(e)) {
            line += " onoverlap=" + std::to_string(o->other_tag) + ":";
            if      (o->action == Action::DestroySelf)  line += "self";
            else if (o->action == Action::DestroyOther) line += "other";
            else line += "spawn:[" + archetype_tokens(o->proto) + "]";
        }
        out += line + "\n";
    });
    return out;
}

World from_scene(const std::string& text) {
    World w;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.empty()) continue;

        std::vector<std::string> toks = split_tokens(line);
        if (toks.empty()) continue;
        if (toks[0] == "sandbox1") continue;
        if (toks[0] == "bounds") {
            if (toks.size() > 1) w.bounds_w = to_f(toks[1]);
            if (toks.size() > 2) w.bounds_h = to_f(toks[2]);
            continue;
        }
        if (toks[0] != "e") continue;

        float x = 0, y = 0, rot = 0;
        std::string arch;                          // plain archetype tokens
        bool has_sp = false; Spawner sp;
        bool has_ov = false; OnOverlap ov;

        for (std::size_t i = 1; i < toks.size(); ++i) {
            const std::string k = key_of(toks[i]), v = val_of(toks[i]);
            if      (k == "x")   x = to_f(v);
            else if (k == "y")   y = to_f(v);
            else if (k == "rot") rot = to_f(v);
            else if (k == "spawner") {
                has_sp = true;
                const auto colon = v.find(':');
                sp.interval = to_f(v.substr(0, colon));
                sp.proto = parse_archetype(strip_brackets(v));
            }
            else if (k == "onoverlap") {
                has_ov = true;
                const auto c1 = v.find(':');
                ov.other_tag = to_i(v.substr(0, c1));
                const std::string rest = c1 == std::string::npos ? "" : v.substr(c1 + 1);
                if      (rest.rfind("other", 0) == 0) ov.action = Action::DestroyOther;
                else if (rest.rfind("spawn", 0) == 0) {
                    ov.action = Action::SpawnProto;
                    ov.proto  = parse_archetype(strip_brackets(rest));
                }
                else ov.action = Action::DestroySelf;
            }
            else arch += toks[i] + " ";
        }

        ecs::Entity e = w.spawn(parse_archetype(arch), x, y);
        if (rot != 0) w.reg.get<Transform2D>(e)->rot = rot;
        if (has_sp) w.reg.add<Spawner>(e, sp);
        if (has_ov) w.reg.add<OnOverlap>(e, ov);
    }
    return w;
}

} // namespace sandbox
