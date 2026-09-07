// =============================================================================
//  games/creatures/pvp.cpp  —  see pvp.hpp
// =============================================================================
#include "games/creatures/pvp.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include "engine/assets.hpp"
#include "engine/rand.hpp"
#include "games/creatures/replay.hpp"

namespace creature {

PvpClient::PvpClient(const Dex& d, gbaas::Config cfg, std::string board,
                     std::unique_ptr<gbaas::ITransport> transport)
    : dex_(d),
      client_(transport ? gbaas::Client(std::move(cfg), std::move(transport))
                        : gbaas::Client(std::move(cfg))),
      board_(std::move(board)) {}

void PvpClient::cancel() {
    if (state_ == State::Idle || state_ == State::Playing || state_ == State::Reporting)
        return;
    // `cancel()` on a socket that never opened is harmless — the SDK buffers ops — and
    // sending it is the point: a client that just stops updating stays in the server's
    // queue and gets matched with somebody who then waits for a peer that is not coming.
    client_.realtime().cancel();
    client_.realtime().disconnect();
    state_ = State::Idle;
    problem_.clear();
}

bool PvpClient::waiting_for_action() const {
    return state_ == State::Playing && !auto_play_ && net_.phase() == NetPhase::MyTurn;
}

bool PvpClient::act(Action a) {
    if (!waiting_for_action()) return false;
    net_.act(dex_, a);
    // NOT sent here: `update()` drains and sends every frame the protocol produced,
    // and one sender is what keeps "the wire carries what the protocol decided" true.
    return true;
}

void PvpClient::fail(std::string why) {
    state_   = State::Failed;
    problem_ = std::move(why);
}

void PvpClient::start(const Party& mine) {
    mine_  = mine;
    state_ = State::SigningIn;
    client_.auth().guest([this](gbaas::Result<gbaas::Session> r) {
        if (!r) {
            fail(r.error ? r.error->message : "sign-in failed");
            return;
        }
        user_id_ = r->user_id;
        state_   = State::Connecting;
    });
}

void PvpClient::update() {
    if (state_ == State::Idle || state_ == State::Done || state_ == State::Failed) {
        client_.update();
        return;
    }
    client_.update();

    if (state_ == State::Connecting) {
        // `connect()` is idempotent and returns false until there is a token; the
        // queue call is safe to make immediately because the SDK buffers ops sent
        // before the socket opens (that is what `outbox_` in Realtime is for).
        if (!client_.realtime().connect()) return;
        client_.realtime().queue();
        state_ = State::Queued;
        return;
    }

    gbaas::RtEvent ev;
    while (client_.realtime().poll(ev)) {
        if (ev.ev == "matched" && state_ == State::Queued) {
            std::uint64_t seed = 0;
            if (!parse_hex16(ev.seed, seed)) { fail("the server sent a malformed seed"); return; }
            if (ev.side != 0 && ev.side != 1) { fail("the server sent no side"); return; }
            room_          = ev.room;
            opponent_id_   = ev.opponent.user_id;
            opponent_name_ = ev.opponent.name;
            net_.begin(dex_, ev.side, seed, mine_);
            state_ = State::Playing;
        } else if (ev.ev == "msg" && state_ == State::Playing) {
            net_.on_frame(dex_, ev.data);
        } else if (ev.ev == "disconnected" && state_ != State::Reporting) {
            fail("the socket dropped mid-match");
            return;
        } else if (ev.ev == "error") {
            fail(ev.message.empty() ? "the server refused" : ev.message);
            return;
        }
    }

    if (state_ != State::Playing) {
        if (state_ == State::Reporting && store_done_ && report_done_) state_ = State::Done;
        return;
    }

    if (net_.phase() == NetPhase::MyTurn && auto_play_)
        net_.act(dex_, choose(dex_, net_.battle(), net_.side()));
    for (const auto& f : net_.drain()) client_.realtime().send(f);

    switch (net_.phase()) {
        case NetPhase::Over:    finish_match(); break;
        // A desync is not a draw and not a loss: neither side knows what happened
        // after the turn they stopped agreeing, so NOTHING is reported to the
        // ladder. A rating moved by a match nobody can reconstruct is worse than no
        // rating at all.
        case NetPhase::Desync:  fail("desync — " + net_.problem()); break;
        case NetPhase::Refused: fail("refused — " + net_.problem()); break;
        default: break;
    }
}

void PvpClient::finish_match() {
    state_ = State::Reporting;

    std::string why;
    tape_text_ = write_replay(net_.tape(), &why);
    if (tape_text_.empty()) {
        // Not fatal: a match that happened is worth rating even if the recording
        // could not be written. Reporting it is the part a player notices.
        store_done_ = true;
    } else {
        client_.replays().save("pvp-" + room_, tape_text_,
                               [this](gbaas::Result<gbaas::ReplayMeta> r) {
                                   if (r) replay_id_ = r->id;
                                   store_done_ = true;
                               });
    }

    client_.leaderboard(board_).report_match(
        opponent_id_, net_.won() ? 1 : (net_.winner() < 0 ? 0 : -1), room_,
        [this](gbaas::Result<gbaas::MatchOutcome> r) {
            if (r) {
                rating_         = static_cast<int>(r->value);
                rating_delta_   = r->delta;
                rating_applied_ = r->applied;
            } else if (r.error) {
                problem_ = r.error->message;
            }
            report_done_ = true;
        });
}

// ---- the headless mode ----------------------------------------------------------

namespace {
bool read_asset(const char* path, std::string& out) {
    const auto bytes = assets::load_file(path);
    if (!bytes) return false;
    out.assign(bytes->begin(), bytes->end());
    return true;
}
const char* state_name(PvpClient::State s) {
    switch (s) {
        case PvpClient::State::Idle:       return "idle";
        case PvpClient::State::SigningIn:  return "signing in";
        case PvpClient::State::Connecting: return "connecting";
        case PvpClient::State::Queued:     return "queued";
        case PvpClient::State::Playing:    return "playing";
        case PvpClient::State::Reporting:  return "reporting";
        case PvpClient::State::Done:       return "done";
        case PvpClient::State::Failed:     return "failed";
    }
    return "?";
}
} // namespace

int run_pvp(const std::string& base_url, const std::string& api_key) {
    Dex         dex;
    std::string why;
    if (!load_dex(dex, read_asset, &why)) {
        std::fprintf(stderr, "pvp: %s\n", why.c_str());
        return 1;
    }

    PvpClient p(dex, gbaas::Config{base_url, api_key});
    // A team drawn at random, because this client has no player to pick one — and
    // because a FIXED team meant every `--pvp` match was a mirror. Two identical
    // parties is the case that found the stalemate (chapter 139), and it is also a
    // dull demo. The match stays perfectly reproducible either way: the tape records
    // the parties, not the process that chose them.
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    engine::Rng pick(now ^ 0x9E3779B97F4A7C15ull);
    std::vector<std::pair<int, int>> team;
    for (int i = 0; i < 3; ++i) team.emplace_back(pick.range(1, 18), pick.range(18, 24));
    p.start(make_party(dex, team));

    std::fprintf(stderr, "pvp: queueing at %s\n", base_url.c_str());
    PvpClient::State last = PvpClient::State::Idle;
    // A wall clock, not a turn count: this waits for a HUMAN to queue on the other
    // side. Two minutes at 8 ms a tick.
    for (int i = 0; i < 15000; ++i) {
        p.update();
        if (p.state() != last) {
            last = p.state();
            std::fprintf(stderr, "pvp: %s\n", state_name(last));
        }
        if (p.state() == PvpClient::State::Done || p.state() == PvpClient::State::Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    if (p.state() != PvpClient::State::Done) {
        std::fprintf(stderr, "pvp: %s — %s\n", state_name(p.state()), p.problem().c_str());
        return 1;
    }

    std::printf("match   %s vs %s (user %lld)\n", p.room().c_str(),
                p.opponent_name().empty() ? "?" : p.opponent_name().c_str(), p.opponent_id());
    std::printf("result  %s in %zu turns\n", p.net().won() ? "WON" : "lost",
                p.net().tape().turns.size());
    if (p.rating_applied())
        std::printf("rating  %d (%+d)\n", p.rating(), p.rating_delta());
    else
        std::printf("rating  %d  [the opponent reported this match first, so the "
                    "change is theirs to show]\n", p.rating());
    if (p.replay_id() != 0)
        std::printf("replay  #%lld, %zu bytes — verify it with `--cmd creature.verify`\n",
                    p.replay_id(), p.tape_text().size());
    return 0;
}

} // namespace creature
