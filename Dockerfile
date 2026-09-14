# ============================================================
#  CppTube — multi-stage Docker image
#
#  Build:  docker build -t cpptube .
#  Run:    docker run -p 8080:8080 cpptube
#
#  Render.com: push this repo, create a "Web Service" with runtime
#  "Docker" — Render detects this Dockerfile automatically.
# ============================================================

# ---- build stage: compile the C++ server -----------------------------
FROM gcc:13-bookworm AS build
WORKDIR /app
COPY include/ include/
COPY server.cpp Makefile ./
RUN make

# ---- runtime stage: minimal image -------------------------------------
# The Makefile statically links libstdc++/libgcc, so a bare debian:bookworm-slim
# (glibc only) is enough to run the binary.
FROM debian:bookworm-slim
WORKDIR /app

COPY --from=build /app/cpptube ./cpptube
COPY public/ public/
COPY storage/seed/ storage/seed/

# Runtime storage directories (ephemeral on Render free tier; point
# CPPTUBE_STORAGE_DIR / CPPTUBE_DATA_DIR at a mounted disk to persist).
RUN mkdir -p data storage/videos storage/thumbs

# Render injects the listen port via the PORT env var; the binary honours it.
ENV PORT=8080
EXPOSE 8080

CMD ["./cpptube"]
