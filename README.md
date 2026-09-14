# CppTube — a YouTube clone written in C++

A single-binary video-sharing web app: the entire backend — HTTP server, video
streaming with byte-range seek support, uploads, search, views, likes and
comments — is one C++17 file (`server.cpp`) built on two header-only
libraries. The frontend is a YouTube-style dark-theme SPA in vanilla
HTML/CSS/JS, no build step.

## Features

- **Home page** — responsive video grid with thumbnails, view counts, channel names and upload dates
- **Watch page** — HTML5 player that streams from the C++ server, view counter (counted once per visitor), likes, comments, recommended videos sidebar
- **Upload** — drag-and-drop or file picker, with a live progress bar; files up to 2 GB
- **Search** — searches titles, channels and descriptions
- **Persistence** — everything stored in `data/db.json` (no external database needed)
- **Streaming** — HTTP `Range` requests are honoured, so seeking in the player is instant. Demo site: https://cpptube.onrender.com
- **Thumbnails** — stable gradient SVG thumbnails generated per video (no ffmpeg needed)

## Deploy on Render.com

Render has no native C++ runtime, but it fully supports **Docker** — this repo
ships a `Dockerfile` (multi-stage: compiles the server with GCC 13, then packs
a slim Debian image with just the binary, the frontend and the demo videos)
plus a `render.yaml` blueprint.

### Quick deploy (free tier)

1. Push this project to a GitHub or GitLab repo.
2. On Render: **New → Web Service**, connect that repo.
3. Render auto-detects the Dockerfile — leave the defaults and click
   **Create Web Service**.
4. Your site is live at `https://<service-name>.onrender.com`.

The server already binds `0.0.0.0` and reads the `PORT` environment variable
that Render injects, so no code changes are needed.

### Free-tier caveats (important!)

- **Ephemeral filesystem**: uploaded videos, thumbnails and `data/db.json`
  are wiped whenever the service restarts or redeploys. The demo videos
  survive because they are baked into the image and re-seeded on every boot.
- **Sleep**: free services sleep after ~15 minutes of inactivity; the first
  request after waking takes a few extra seconds.

### Persisting uploads (paid plans)

Attach a **Render Disk** mounted at `/mnt/data`, then set two environment
variables in the Render dashboard (or uncomment them in `render.yaml`):

```
CPPTUBE_STORAGE_DIR=/mnt/data/storage
CPPTUBE_DATA_DIR=/mnt/data/data
```

The server writes all uploads, thumbnails and the database under those
paths, so content survives restarts and deploys. (The demo-seed source
stays pinned to the image path and keeps working either way.)

## Build (local)

Requires any C++17 compiler. No other dependencies — `httplib.h` and
`json.hpp` are vendored in `include/`.

```bash
make                 # uses g++ by default
# or manually:
g++ -std=c++17 -O2 -Iinclude -o cpptube server.cpp -pthread
```

On Windows (MSVC):

```bat
cl /std:c++17 /EHsc /Iinclude server.cpp
```

## Run

```bash
./cpptube            # serves on http://localhost:8080
./cpptube 3000       # custom port as argument
PORT=3000 ./cpptube  # custom port via env var (this is what Render uses)
```

On first launch it seeds itself with two demo videos (in `storage/seed/`)
so the home page isn't empty. Delete `data/db.json` to reset the database.

## Project layout

```
server.cpp            the whole backend (routes, JSON API, streaming, upload)
include/httplib.h     cpp-httplib v0.18.3 — header-only HTTP server
include/json.hpp      nlohmann/json v3.11.3 — header-only JSON
public/               frontend SPA (index.html, app.js, style.css)
storage/videos/       uploaded video files
storage/thumbs/       generated SVG thumbnails
storage/seed/         first-run demo videos + seed.json
data/db.json          persisted video metadata
```

## API

| Method | Path                            | What it does                       |
|--------|---------------------------------|------------------------------------|
| GET    | `/api/videos?q=`                | list / search videos               |
| GET    | `/api/videos/{id}`              | one video with comments            |
| GET    | `/stream/{id}`                 | video bytes (Range supported)      |
| POST   | `/api/upload`                   | multipart upload                   |
| POST   | `/api/videos/{id}/view`         | increment view count               |
| POST   | `/api/videos/{id}/like`         | increment likes                    |
| POST   | `/api/videos/{id}/comments`     | add a comment (`{author, text}`)   |

## Notes

- Demo videos are from the Blender Foundation (Big Buck Bunny, CC-BY) and the
  MediaElement.js test suite.
- This is an educational project — auth, per-user channels and transcoding
  are out of scope by design.
