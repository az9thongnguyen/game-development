// =============================================================================
//  games/creatures/netbattle.cpp  —  see netbattle.hpp
// =============================================================================
#include "games/creatures/netbattle.hpp"

#include <sstream>

namespace creature {

std::string write_party(const Party& p) {
    std::ostringstream o;
    for (int i = 0; i < p.count && i < kPartySize; ++i) {
        if (i) o << ' ';
        o << p.member[i].species << ':' << p.member[i].level;
    }
    return o.str();
}

bool read_party(const Dex& d, const std::string& text, Party& out) {
    std::vector<std::pair<int, int>> spec;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        const auto colon = tok.find(':');
        if (colon == std::string::npos) return false;
        const std::string a = tok.substr(0, colon), b = tok.substr(colon + 1);
        if (a.empty() || b.empty()) return false;
        char* end = nullptr;
        const long species = std::strtol(a.c_str(), &end, 10);
        if (*end != '\0') return false;
        const long level = std::strtol(b.c_str(), &end, 10);
        if (*end != '\0') return false;
        // A level cap and a species that exists. This is the ONE place a peer's
        // claim about itself is checked, and it is checked because the alternative
        // is a match against a level-9000 creature that both sides compute
        // identically and neither refuses.
        if (!d.species_by_id(static_cast<int>(species))) return false;
        if (level < 1 || level > 100) return false;
        if (spec.size() >= static_cast<std::size_t>(kPartySize)) return false;
        spec.emplace_back(static_cast<int>(species), static_cast<int>(level));
    }
    if (spec.empty()) return false;
    out = make_party(d, spec);
    return true;
}

bool NetBattle::refuse(std::string why) {
    phase_   = NetPhase::Refused;
    problem_ = std::move(why);
    return false;
}

void NetBattle::begin(const Dex& d, int my_side, std::uint64_t seed, const Party& mine) {
    *this = NetBattle{};
    side_ = my_side == 1 ? 1 : 0;
    seed_ = seed;
    mine_ = mine;
    outbox_.push_back("party " + std::to_string(kNetVersion) + " " + write_party(mine));
    phase_ = NetPhase::Handshake;
    try_build(d);
}

void NetBattle::try_build(const Dex& d) {
    if (!have_theirs_ || phase_ != NetPhase::Handshake) return;

    // Sides are assigned by the SERVER, not by arrival order: side 0's party goes in
    // slot 0 on BOTH machines, whichever of them received the other's frame first.
    battle_ = Battle{};
    battle_.side[side_]     = mine_;
    battle_.side[1 - side_] = theirs_;
    battle_.rng             = seed_;

    tape_       = Replay{};
    tape_.rules = rules_hash(d);
    tape_.start = battle_;

    phase_ = NetPhase::MyTurn;
    try_resolve(d);          // the peer may already have sent turn 1
}

void NetBattle::act(const Dex& d, Action a) {
    if (phase_ != NetPhase::MyTurn) return;
    my_action_ = a;
    acted_     = true;
    outbox_.push_back("act " + std::to_string(turn_ + 1) + " " +
                      std::to_string(static_cast<int>(a.kind)) + " " +
                      std::to_string(a.index));
    phase_ = NetPhase::Waiting;
    try_resolve(d);
}

void NetBattle::try_resolve(const Dex& d) {
    while (acted_ && !battle_.over) {
        const auto it = peer_action_.find(turn_ + 1);
        if (it == peer_action_.end()) return;

        ++turn_;
        const Action mine  = my_action_;
        const Action peer  = it->second;
        const Action a0    = side_ == 0 ? mine : peer;
        const Action a1    = side_ == 0 ? peer : mine;

        log_.clear();
        step(d, battle_, a0, a1, &log_);
        tape_.turns.push_back(Turn{a0, a1, hash(battle_)});

        peer_action_.erase(it);
        acted_ = false;
        outbox_.push_back("hash " + std::to_string(turn_) + " " + hex16(hash(battle_)));

        // A hash for this turn may already have arrived — the peer resolves the same
        // turn we do and there is no ordering between two sockets.
        check_hash(turn_);
        if (phase_ == NetPhase::Desync) return;

        phase_ = battle_.over ? NetPhase::Over : NetPhase::MyTurn;
        if (battle_.over) return;
    }
}

void NetBattle::check_hash(int turn) {
    if (turn < 1 || turn > static_cast<int>(tape_.turns.size())) return;
    const auto it = peer_hash_.find(turn);
    if (it == peer_hash_.end()) return;

    const std::uint64_t mine = tape_.turns[turn - 1].after;
    if (mine == it->second) { peer_hash_.erase(it); return; }

    phase_       = NetPhase::Desync;
    desync_turn_ = turn;
    mine_hash_   = mine;
    their_hash_  = it->second;
    problem_     = "turn " + std::to_string(turn) + ": " + hex16(mine) +
                   " != " + hex16(it->second);
}

bool NetBattle::on_frame(const Dex& d, const std::string& frame) {
    if (phase_ == NetPhase::Refused || phase_ == NetPhase::Desync) return false;

    std::istringstream in(frame);
    std::string kind;
    if (!(in >> kind)) return refuse("empty frame");

    if (kind == "party") {
        int version = 0;
        if (!(in >> version)) return refuse("party frame has no version");
        // A peer speaking a protocol this build does not know is refused, not
        // half-understood — the same rule map2, the save and the replay follow.
        if (version != kNetVersion)
            return refuse("peer speaks protocol " + std::to_string(version));
        if (have_theirs_) return refuse("a second party frame");
        std::string rest;
        std::getline(in, rest);
        if (!read_party(d, rest, theirs_)) return refuse("peer sent an illegal party");
        have_theirs_ = true;
        try_build(d);
        return true;
    }

    if (kind == "act") {
        int t = 0, k = 0, i = 0;
        if (!(in >> t >> k >> i)) return refuse("malformed act");
        // Bounded on BOTH sides. Below turn_ is a duplicate or a replayed frame;
        // far above it is a peer trying to make us hold a map of nothing.
        if (t <= turn_) return true;                        // already resolved: ignore
        if (t > turn_ + kNetLookahead) return refuse("act too far ahead");
        if (k < 0 || k > 3) return refuse("unknown action kind");
        // The index is NOT range-checked: `step` treats an illegal one as a wasted
        // turn on purpose (see battle.cpp), and rejecting it here would need a round
        // trip to tell the peer so. A file gets checked; a peer gets tolerated.
        peer_action_.emplace(t, Action{static_cast<Action::Kind>(k), i});
        try_resolve(d);
        return true;
    }

    if (kind == "hash") {
        int t = 0;
        std::string h;
        std::uint64_t v = 0;
        if (!(in >> t >> h)) return refuse("malformed hash");
        if (!parse_hex16(h, v)) return refuse("malformed hash value");
        if (t < 1 || t > turn_ + kNetLookahead) return refuse("hash for an impossible turn");
        peer_hash_[t] = v;
        check_hash(t);
        return phase_ != NetPhase::Desync;
    }

    return refuse("unknown frame: " + kind);
}

std::vector<std::string> NetBattle::drain() {
    std::vector<std::string> out;
    out.swap(outbox_);
    return out;
}

} // namespace creature
