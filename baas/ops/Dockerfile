# =============================================================================
#  baas/ops/Dockerfile  —  containerize the Game BaaS backend (H2 deployment)
# =============================================================================
#  Build context is the REPO ROOT (the backend needs the root CMakeLists + baas/):
#     docker build -f baas/ops/Dockerfile -t hand-baas .
#
#  This builds ONLY the backend: `-DENGINE_BUILD_DESKTOP=OFF` skips the SDL2 desktop
#  'demo' target so no SDL2 is needed in the image — the build-system split that makes
#  a server container possible (see docs/book/107). The official Drogon image supplies
#  Drogon + its toolchain; we add libsodium for argon2id/JWT.
# =============================================================================

# ---- build stage ------------------------------------------------------------
FROM drogonframework/drogon:latest AS build
# libsodium: argon2id/JWT. libcurl: the gbaas SDK's transport (sdk/cpp is configured as
# part of the root tree even though the image only builds the 'baas' target).
RUN apt-get update \
 && apt-get install -y --no-install-recommends libsodium-dev libcurl4-openssl-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DENGINE_BUILD_DESKTOP=OFF \
 && cmake --build build --target baas -j"$(nproc)"

# ---- runtime stage ----------------------------------------------------------
# Reuse the Drogon image as the runtime base so every shared lib the binary needs
# (libdrogon, libtrantor, jsoncpp, sqlite3, …) is already present — no dependency hunt.
FROM drogonframework/drogon:latest
RUN apt-get update \
 && apt-get install -y --no-install-recommends libsodium23 \
 && rm -rf /var/lib/apt/lists/* \
 && mkdir -p /app/data
WORKDIR /app
COPY --from=build /src/build/baas/baas  /app/baas
COPY --from=build /src/assets           /app/assets
EXPOSE 8080
# Secrets come from the environment (never baked in): BAAS_JWT_SECRET, BAAS_ADMIN_SECRET.
# The SQLite DB lives under /app/data — mount it as a volume so it survives restarts
# (and back it up with baas/ops/backup_restore_drill.sh).
ENTRYPOINT ["/app/baas"]
CMD ["--host", "0.0.0.0", "--port", "8080", "--db", "sqlite:///app/data/baas.db", "--seed"]
