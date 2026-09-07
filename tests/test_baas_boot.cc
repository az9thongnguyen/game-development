// =============================================================================
//  tests/test_baas_boot.cc  —  the binary, started, from outside
// =============================================================================
//  Every other test in this suite links `baas_core` and calls `register_routes()`
//  itself. That covers the whole server except the file that decides what the server
//  DOES on startup — and for thirty-five chapters that file had this in it:
//
//      if (do_seed) { seed(db); printf(...); return 0; }
//
//  while `baas/ops/Dockerfile` shipped `CMD [..., "--seed"]`. So the container
//  seeded and exited, every time, and with `restart: unless-stopped` in front of it
//  that was a loop that never listened. Nothing in ctest could see it: the bug was
//  in main(), and no test had ever run main().
//
//  This one does. It spawns the real binary and asks it questions from outside.
// =============================================================================
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tests/baas_test_util.h"

extern char** environ;

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

// Start the binary with `args` (argv[0] is added). Returns the pid, or -1.
pid_t spawn_baas(const std::vector<std::string>& args, const std::string& out_path) {
    std::vector<char*> argv;
    std::string        bin = BAAS_BIN;
    argv.push_back(bin.data());
    std::vector<std::string> owned = args;
    for (auto& a : owned) argv.push_back(a.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, out_path.c_str(),
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, bin.c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    return rc == 0 ? pid : -1;
}

// true if `pid` is still running (has not been reaped and has not exited).
bool alive(pid_t pid, int* exit_code) {
    int   status = 0;
    pid_t r      = waitpid(pid, &status, WNOHANG);
    if (r == 0) return true;
    if (exit_code) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return false;
}

std::string slurp(const std::string& path) {
    std::ifstream      in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

}  // namespace

int main() {
    const std::string tmp_db   = "test_baas_boot.db";
    const std::string log_path = "test_baas_boot.log";
    baastest::cleanup_db(tmp_db);
    std::remove(log_path.c_str());

    // The spawned server always uses SQLite: this test is about main(), and pointing a
    // detached process at the shared Postgres would race every other test's schema wipe.
    const std::string db_arg  = "sqlite://" + tmp_db;
    const std::string port    = std::to_string(baastest::find_free_port());
    const std::string base    = "http://127.0.0.1:" + port;

    // ---- 1. --seed SERVES ---------------------------------------------------
    pid_t pid = spawn_baas({"--db", db_arg, "--host", "127.0.0.1", "--port", port,
                            "--jwt-secret", "t", "--admin-secret", "t", "--seed"},
                           log_path);
    CHECK(pid > 0);
    if (pid > 0) {
        baastest::Resp health;
        for (int i = 0; i < 100; ++i) {
            health = baastest::http("GET", base + "/healthz", {});
            if (health.status == 200) break;
            usleep(100 * 1000);
        }
        int code = 0;
        const bool still_running = alive(pid, &code);
        if (!still_running)
            std::printf("      the process exited with %d; it logged:\n%s\n", code,
                        slurp(log_path).c_str());
        CHECK(still_running);                 // THE assertion this file exists for
        CHECK(health.status == 200);
        CHECK(health.body.find("\"status\":\"ok\"") != std::string::npos);

        // It really seeded, and said so BEFORE exiting — stdout is block-buffered into
        // a pipe, so an unflushed printf would sit there for the life of the server and
        // `docker logs` would show nothing.
        CHECK(slurp(log_path).find("pk_demo_colony") != std::string::npos);

        // The seeded project is usable: a guest sign-in needs the api key to resolve, a
        // writable database, and a working JWT signer.
        const auto guest = baastest::http(
            "POST", base + "/v1/auth/guest", {"X-Api-Key: pk_demo_colony"},
            R"({"display_name":"boot"})");
        CHECK(guest.status == 200);
        CHECK(baastest::parse(guest.body)["access_token"].asString().size() > 20);

        // ...and the document it serves is the one committed in the repo.
        const auto served = baastest::http("GET", base + "/openapi.json", {});
        CHECK(served.status == 200);
        CHECK(served.body + "\n" == slurp(std::string(REPO_ROOT) + "/baas/openapi.json"));

        kill(pid, SIGTERM);
        for (int i = 0; i < 50 && !alive(pid, nullptr); ++i) usleep(100 * 1000);
        int st = 0;
        waitpid(pid, &st, WNOHANG);
        kill(pid, SIGKILL);
        waitpid(pid, &st, 0);
    }

    // ---- 2. --seed-only stops ------------------------------------------------
    // The other half of the split. A flag that always exits was the bug; a flag that
    // never exits would be a different one, so both directions are checked.
    {
        pid_t p = spawn_baas({"--db", db_arg, "--port", port, "--jwt-secret", "t",
                              "--admin-secret", "t", "--seed-only"},
                             log_path);
        CHECK(p > 0);
        int status = 0;
        CHECK(waitpid(p, &status, 0) == p);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(slurp(log_path).find("pk_demo_colony") != std::string::npos);
    }

    // ---- 3. --openapi writes the committed bytes ------------------------------
    {
        const std::string out = "test_baas_boot_openapi.json";
        std::remove(out.c_str());
        pid_t p = spawn_baas({"--openapi", out}, log_path);
        CHECK(p > 0);
        int status = 0;
        CHECK(waitpid(p, &status, 0) == p);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(slurp(out) == slurp(std::string(REPO_ROOT) + "/baas/openapi.json"));
        std::remove(out.c_str());
    }

    baastest::cleanup_db(tmp_db);
    std::remove(log_path.c_str());
    if (g_failures == 0) std::printf("baas_boot: all tests passed\n");
    else                 std::printf("baas_boot: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
