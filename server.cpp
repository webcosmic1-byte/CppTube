// ============================================================================
//  CppTube - a YouTube-clone web server written in C++
//
//  Stack:   cpp-httplib (header-only HTTP server) + nlohmann/json
//  Build:   see Makefile / README.md
//  Run:     ./cpptube [port]        (default port 8080)
// ============================================================================

#include "httplib.h"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Paths
//
// DATA_DIR and STORAGE_DIR are overridable via environment variables so that
// a Render.com Disk (or any persistent volume) can be mounted for durable
// storage. SEED_DIR stays pinned to the image-baked path so the first-run
// demo videos are always available even when STORAGE_DIR is redirected.
// ---------------------------------------------------------------------------
static std::string envOr(const char* name, const char* def) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string(def);
}

static const std::string DATA_DIR    = envOr("CPPTUBE_DATA_DIR", "data");
static const std::string DB_PATH     = DATA_DIR + "/db.json";
static const std::string STORAGE_DIR = envOr("CPPTUBE_STORAGE_DIR", "storage");
static const std::string VIDEOS_DIR  = STORAGE_DIR + "/videos";
static const std::string THUMBS_DIR  = STORAGE_DIR + "/thumbs";
static const std::string SEED_DIR    = "storage/seed";   // baked into image
static const std::string PUBLIC_DIR = "public";

// ---------------------------------------------------------------------------
// In-memory database (persisted to data/db.json)
// ---------------------------------------------------------------------------
static json g_db;                 // { "videos": [ ... ] }
static std::mutex g_db_mutex;

static std::string makeId() {
    static std::mt19937_64 rng(
        (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
    std::stringstream ss;
    ss << std::hex << (uint64_t)std::chrono::system_clock::now().time_since_epoch().count()
       << rng() % 0xFFFFF;
    return ss.str();
}

static std::string nowIso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// NOTE: callers must already hold g_db_mutex.
static void saveDb() {
    std::ofstream f(DB_PATH, std::ios::binary);
    if (f) f << g_db.dump(2);
}

static json* findVideo(const std::string& id) {
    for (auto& v : g_db["videos"])
        if (v.value("id", "") == id) return &v;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += '&'; out += "amp;";  break;
            case '<':  out += '<';  out += "lt;";   break;
            case '>':  out += '>';  out += "gt;";   break;
            case '"':  out += '&'; out += "quot;"; break;
            case '\'': out += '&'; out += "apos;"; break;
            default:   out += c;
        }
    }
    return out;
}

static bool isAllowedVideoExt(const std::string& name) {
    std::string ext = fs::path(name).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".mp4" || ext == ".webm" || ext == ".mkv" ||
           ext == ".mov" || ext == ".avi" || ext == ".ogv" || ext == ".ogg";
}

static std::string sanitizeFilename(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')
            out += (char)std::tolower((unsigned char)c);
        else
            out += '_';
    }
    if (out.size() > 80) out = out.substr(out.size() - 80);
    return out;
}

// Deterministic pretty gradient thumbnail (SVG), since we do not require
// ffmpeg to extract a real frame. Hue is derived from the title hash so
// every video gets a stable, distinct color pair.
static void writeSvgThumb(const std::string& id, const std::string& title,
                          const std::string& duration) {
    size_t h = std::hash<std::string>{}(title);
    int hue1 = (int)(h % 360);
    int hue2 = (hue1 + 60) % 360;

    std::string trunc = title.substr(0, std::min<size_t>(title.size(), 46));
    if (title.size() > 46) trunc.replace(43, 3, "...");

    std::string durBadge;
    if (!duration.empty()) {
        durBadge = "<rect x='252' y='148' width='58' height='22' rx='4' "
                   "fill='rgba(0,0,0,0.8)'/>"
                   "<text x='281' y='163' font-family='Roboto,Arial' font-size='13' "
                   "font-weight='600' fill='#fff' text-anchor='middle'>" +
                   xmlEscape(duration) + "</text>";
    }

    std::ofstream f(THUMBS_DIR + "/" + id + ".svg");
    f << "<svg xmlns='http://www.w3.org/2000/svg' width='320' height='180'>"
      << "<defs><linearGradient id='g' x1='0' y1='0' x2='1' y2='1'>"
      << "<stop offset='0' stop-color='hsl(" << hue1 << ",65%,45%)'/>"
      << "<stop offset='1' stop-color='hsl(" << hue2 << ",70%,25%)'/>"
      << "</linearGradient></defs>"
      << "<rect width='320' height='180' fill='url(#g)'/>"
      << "<circle cx='160' cy='86' r='34' fill='rgba(0,0,0,0.35)'/>"
      << "<path d='M150 70 L150 102 L178 86 Z' fill='rgba(255,255,255,0.95)'/>"
      << durBadge
      << "<text x='12' y='30' font-family='Roboto,Arial' font-size='14' "
      << "font-weight='700' fill='rgba(255,255,255,0.92)'>"
      << xmlEscape(trunc) << "</text>"
      << "</svg>";
}

static json makeVideo(const std::string& id, const std::string& title,
                      const std::string& description, const std::string& channel,
                      const std::string& filename, const std::string& duration,
                      int views, int likes, const json& comments) {
    writeSvgThumb(id, title, duration);
    return json{
        {"id", id},
        {"title", title},
        {"description", description},
        {"channel", channel},
        {"filename", filename},
        {"duration", duration},
        {"views", views},
        {"likes", likes},
        {"uploadedAt", nowIso()},
        {"comments", comments.is_null() ? json::array() : comments},
    };
}

// ---------------------------------------------------------------------------
// First-run seeding from storage/seed/seed.json (demo content)
// ---------------------------------------------------------------------------
static void seedIfEmpty() {
    std::error_code ec;

    std::ifstream dbf(DB_PATH);
    if (dbf) {  // DB exists -> load it
        try {
            dbf >> g_db;
            if (g_db.contains("videos")) return;
        } catch (...) { /* fall through to reseed */ }
    }

    std::ifstream sf(SEED_DIR + "/seed.json");
    if (!sf) { g_db["videos"] = json::array(); return; }

    json seeds;
    try { sf >> seeds; } catch (...) { g_db["videos"] = json::array(); return; }

    g_db["videos"] = json::array();
    for (auto& s : seeds) {
        const std::string id = makeId();
        const std::string src = SEED_DIR + "/" + s.value("file", "");
        const std::string dst = VIDEOS_DIR + "/" + id + "_" + sanitizeFilename(s.value("file", ""));
        if (fs::exists(src)) fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);

        json v = makeVideo(id, s.value("title", "Untitled"),
                           s.value("description", ""), s.value("channel", "CppTube"),
                           fs::path(dst).filename().string(), s.value("duration", ""),
                           s.value("views", 0), s.value("likes", 0),
                           s.value("comments", json::array()));
        g_db["videos"].push_back(v);
    }
    saveDb();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    // Port resolution order:
    //   1. PORT environment variable (used by Render.com and most PaaS hosts)
    //   2. command-line argument                (./cpptube 3000)
    //   3. default 8080
    int port = 8080;
    if (const char* envPort = std::getenv("PORT")) {
        port = std::atoi(envPort);
    } else if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    if (port <= 0 || port > 65535) port = 8080;

    std::error_code ec;
    fs::create_directories(DATA_DIR, ec);
    fs::create_directories(VIDEOS_DIR, ec);
    fs::create_directories(THUMBS_DIR, ec);

    seedIfEmpty();

    httplib::Server svr;
    svr.set_payload_max_length((size_t)2 * 1024 * 1024 * 1024);  // 2 GB uploads

    svr.set_exception_handler([](const auto& req, auto& res, std::exception_ptr ep) {
        std::string msg = "Internal server error";
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { msg = e.what(); }
        catch (...) {}
        res.status = 500;
        res.set_content(json{{"error", msg}}.dump(), "application/json");
    });

    // ---- static assets -----------------------------------------------------
    if (!svr.set_mount_point("/static", PUBLIC_DIR))
        fprintf(stderr, "warning: cannot mount public dir\n");
    if (!svr.set_mount_point("/thumbs", THUMBS_DIR))
        fprintf(stderr, "warning: cannot mount thumbs dir\n");

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(PUBLIC_DIR + "/index.html");
        std::stringstream ss; ss << f.rdbuf();
        res.set_content(ss.str(), "text/html; charset=utf-8");
    });

    // ---- API: list / search --------------------------------------------------
    svr.Get("/api/videos", [](const httplib::Request& req, httplib::Response& res) {
        std::string q;
        if (req.has_param("q")) q = req.get_param_value("q");

        std::transform(q.begin(), q.end(), q.begin(), ::tolower);
        json out = json::array();

        std::lock_guard<std::mutex> lock(g_db_mutex);
        for (auto& v : g_db["videos"]) {
            if (!q.empty()) {
                std::string hay = v.value("title", "") + " " + v.value("channel", "") + " " +
                                  v.value("description", "");
                std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
                if (hay.find(q) == std::string::npos) continue;
            }
            out.push_back(v);
        }
        // newest first
        std::sort(out.begin(), out.end(), [](const json& a, const json& b) {
            return a.value("uploadedAt", "") > b.value("uploadedAt", "");
        });
        res.set_content(out.dump(), "application/json");
    });

    // ---- API: single video ---------------------------------------------------
    svr.Get(R"(/api/videos/([A-Za-z0-9]+))",
            [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_db_mutex);
        json* v = findVideo(req.matches[1].str());
        if (!v) { res.status = 404;
                  res.set_content(json{{"error","video not found"}}.dump(), "application/json");
                  return; }
        res.set_content(v->dump(), "application/json");
    });

    // ---- video streaming with HTTP Range support -----------------------------
    svr.Get(R"(/stream/([A-Za-z0-9]+))",
            [](const httplib::Request& req, httplib::Response& res) {
        std::string path;
        {
            std::lock_guard<std::mutex> lock(g_db_mutex);
            json* v = findVideo(req.matches[1].str());
            if (!v) { res.status = 404; return; }
            path = VIDEOS_DIR + "/" + v->value("filename", "");
        }
        if (!fs::exists(path)) { res.status = 404; return; }

        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        std::string mime = "video/mp4";
        if (ext == ".webm") mime = "video/webm";
        else if (ext == ".mkv") mime = "video/x-matroska";
        else if (ext == ".mov") mime = "video/quicktime";
        else if (ext == ".ogv" || ext == ".ogg") mime = "video/ogg";

        // Stream from disk via a content provider with a known total length.
        // Because the length is known up-front, cpp-httplib honours HTTP
        // Range requests (206 Partial Content) automatically - which is what
        // makes seeking inside the <video> player instant.
        const size_t size = (size_t)fs::file_size(path);
        auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
        if (!file->is_open()) { res.status = 404; return; }

        res.set_content_provider(
            size, mime,
            [file](size_t offset, size_t length, httplib::DataSink& sink) -> bool {
                std::vector<char> buf(length);
                file->seekg((std::streamoff)offset);
                file->read(buf.data(), (std::streamsize)length);
                auto got = file->gcount();
                if (got <= 0) return false;
                sink.write(buf.data(), (size_t)got);
                return true;
            },
            [file](bool) { file->close(); });
    });

    // ---- upload ---------------------------------------------------------------
    svr.Post("/api/upload", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("video")) {
            res.status = 400;
            res.set_content(json{{"error","missing video file"}}.dump(), "application/json");
            return;
        }
        const auto& file = req.get_file_value("video");
        if (file.filename.empty() || !isAllowedVideoExt(file.filename)) {
            res.status = 400;
            res.set_content(json{{"error","unsupported file type (allowed: mp4 webm mkv mov avi ogv)"}}
                                .dump(), "application/json");
            return;
        }

        std::string title = req.has_file("title") ? req.get_file_value("title").content : "";
        std::string description =
            req.has_file("description") ? req.get_file_value("description").content : "";
        std::string channel =
            req.has_file("channel") ? req.get_file_value("channel").content : "";

        if (title.empty()) title = fs::path(file.filename).stem().string();
        if (channel.empty()) channel = "You";

        const std::string id = makeId();
        const std::string fname = id + "_" + sanitizeFilename(file.filename);
        const std::string fpath = VIDEOS_DIR + "/" + fname;

        std::ofstream out(fpath, std::ios::binary);
        if (!out) {
            res.status = 500;
            res.set_content(json{{"error","cannot write to storage"}}.dump(), "application/json");
            return;
        }
        out.write(file.content.data(), (std::streamsize)file.content.size());
        out.close();

        json v;
        {
            std::lock_guard<std::mutex> lock(g_db_mutex);
            v = makeVideo(id, title, description, channel, fname, "", 0, 0, json::array());
            g_db["videos"].push_back(v);
            saveDb();
        }
        res.status = 201;
        res.set_content(v.dump(), "application/json");
    });

    // ---- view counter ---------------------------------------------------------
    svr.Post(R"(/api/videos/([A-Za-z0-9]+)/view)",
             [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_db_mutex);
        json* v = findVideo(req.matches[1].str());
        if (!v) { res.status = 404; return; }
        (*v)["views"] = v->value("views", 0) + 1;
        saveDb();
        res.set_content(json{{"views", v->value("views", 0)}}.dump(), "application/json");
    });

    // ---- like -------------------------------------------------------------------
    svr.Post(R"(/api/videos/([A-Za-z0-9]+)/like)",
             [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_db_mutex);
        json* v = findVideo(req.matches[1].str());
        if (!v) { res.status = 404; return; }
        (*v)["likes"] = v->value("likes", 0) + 1;
        saveDb();
        res.set_content(json{{"likes", v->value("likes", 0)}}.dump(), "application/json");
    });

    // ---- comments -----------------------------------------------------------------
    svr.Post(R"(/api/videos/([A-Za-z0-9]+)/comments)",
             [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(json{{"error","invalid JSON body"}}.dump(), "application/json");
            return;
        }
        std::string author = body.value("author", "");
        std::string text   = body.value("text", "");
        if (author.empty()) author = "Anonymous";
        if (text.empty()) {
            res.status = 400;
            res.set_content(json{{"error","comment text is empty"}}.dump(), "application/json");
            return;
        }
        json c = json{{"author", author}, {"text", text}, {"createdAt", nowIso()}};
        {
            std::lock_guard<std::mutex> lock(g_db_mutex);
            json* v = findVideo(req.matches[1].str());
            if (!v) { res.status = 404; return; }
            (*v)["comments"].push_back(c);
            saveDb();
        }
        res.status = 201;
        res.set_content(c.dump(), "application/json");
    });

    // ---- run ---------------------------------------------------------------------
    fprintf(stderr, "CppTube running on http://localhost:%d  (Ctrl+C to stop)\n", port);
    if (!svr.listen("0.0.0.0", port)) {
        fprintf(stderr, "failed to listen on port %d\n", port);
        return 1;
    }
    return 0;
}
