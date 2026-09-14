/* ============================================================
   CppTube — frontend app (vanilla JS, hash router)
   ============================================================ */

const $content = document.getElementById("content");
const $searchInput = document.getElementById("search-input");

const HUES = [0, 30, 60, 120, 180, 210, 260, 300];
const hueFor = (s) => HUES[[...s].reduce((a, c) => a + c.charCodeAt(0), 0) % HUES.length];

/* ---------- helpers ---------- */
function fmtViews(n) {
  if (n >= 1e7) return (n / 1e7).toFixed(1).replace(/\.0$/, "") + " crore";
  if (n >= 1e5) return (n / 1e5).toFixed(1).replace(/\.0$/, "") + " lakh";
  if (n >= 1e3) return (n / 1e3).toFixed(1).replace(/\.0$/, "") + "K";
  return String(n);
}

function timeAgo(iso) {
  const s = Math.max(1, (Date.now() - new Date(iso).getTime()) / 1000);
  const units = [["year", 31536000], ["month", 2592000], ["week", 604800],
                 ["day", 86400], ["hour", 3600], ["minute", 60]];
  for (const [name, sec] of units) {
    if (s >= sec) {
      const v = Math.floor(s / sec);
      return `${v} ${name}${v > 1 ? "s" : ""} ago`;
    }
  }
  return "just now";
}

function esc(s) {
  const AMP = String.fromCharCode(38); // entity escape built from char codes
  const m = {};
  m["&"] = AMP + "amp;";
  m["<"] = AMP + "lt;";
  m[">"] = AMP + "gt;";
  m['"'] = AMP + "quot;";
  m["'"] = AMP + "apos;";
  return String(s ?? "").replace(/[&<>"']/g, (c) => m[c]);
}

async function api(path, opts) {
  const r = await fetch(path, opts);
  if (!r.ok) throw new Error((await r.json().catch(() => ({}))).error || r.statusText);
  return r.json();
}

/* ---------- shared fragments ---------- */
function avatarHtml(name) {
  const h = hueFor(name || "?");
  return `<div class="card-avatar" style="background:hsl(${h},55%,45%)">${esc((name || "?")[0].toUpperCase())}</div>`;
}

function cardHtml(v) {
  return `
  <div class="card" onclick="location.hash='#/watch?id=${v.id}'">
    <div class="thumb-wrap">
      <img class="thumb" src="/thumbs/${v.id}.svg" alt="" loading="lazy"/>
      ${v.duration ? `<span class="duration">${esc(v.duration)}</span>` : ""}
    </div>
    <div class="card-meta">
      ${avatarHtml(v.channel)}
      <div class="card-info">
        <div class="card-title">${esc(v.title)}</div>
        <div class="card-sub"><a>${esc(v.channel)}</a></div>
        <div class="card-sub">${fmtViews(v.views)} views &middot; ${timeAgo(v.uploadedAt)}</div>
      </div>
    </div>
  </div>`;
}

/* ============================================================
   Views
   ============================================================ */
async function viewHome(query = "") {
  const videos = await api("/api/videos" + (query ? `?q=${encodeURIComponent(query)}` : ""));
  document.title = query ? `${query} - CppTube` : "CppTube";
  $content.innerHTML = videos.length
    ? `<h2 class="section-title">${query ? `Results for “${esc(query)}”` : "Recommended"}</h2>
       <div class="videos-grid">${videos.map(cardHtml).join("")}</div>`
    : `<div class="empty-state">${
        query ? "No videos found. Try another search." :
        "No videos yet. Click <b>Create</b> in the top-right to upload the first one."}</div>`;
}

async function viewWatch(id) {
  const [v, all] = await Promise.all([api(`/api/videos/${id}`), api("/api/videos")]);
  document.title = `${v.title} - CppTube`;
  const recs = all.filter((x) => x.id !== id).slice(0, 8);
  let viewCounted = false, liked = false;

  $content.innerHTML = `
  <div class="watch-layout">
    <div class="watch-main">
      <div class="watch-player">
        <video id="player" controls preload="metadata" src="/stream/${v.id}"></video>
      </div>

      <h1 class="watch-title">${esc(v.title)}</h1>

      <div class="watch-row">
        <div class="channel-row">
          ${avatarHtml(v.channel)}
          <div>
            <div class="channel-name">${esc(v.channel)}</div>
            <div class="channel-sub">${fmtViews(v.views)} views</div>
          </div>
        </div>
        <div class="actions">
          <button class="pill" id="like-btn">
            <svg viewBox="0 0 24 24"><path d="M18.77 11h-4.23l1.52-4.94A1.54 1.54 0 0 0 14.6 4c-.38 0-.74.15-1.01.42L7 11H4v10h13.68a2 2 0 0 0 1.97-1.65l1.32-7.3A1.5 1.5 0 0 0 18.77 11zM6 20H5v-8h1v8z"/></svg>
            <span id="like-count">${fmtViews(v.likes)}</span>
          </button>
          <button class="pill" onclick="navigator.clipboard && navigator.clipboard.writeText(location.href)">
            <svg viewBox="0 0 24 24"><path d="M15 5.63L20.66 12 15 18.37V15h-3a9 9 0 0 0-7.94 4.75A10 10 0 0 1 12 13h3V5.63z"/></svg>
            Share
          </button>
        </div>
      </div>

      <div class="description">
        <div class="views-date">${fmtViews(v.views)} views &middot; ${timeAgo(v.uploadedAt)}</div>
        ${esc(v.description) || "No description."}
      </div>

      <div class="comments">
        <div class="comments-title">${v.comments.length} Comments</div>
        <form class="comment-form" id="comment-form">
          ${avatarHtml("You")}
          <input id="comment-input" placeholder="Add a comment..." required />
          <button type="submit">Comment</button>
        </form>
        <div id="comment-list">
          ${[...v.comments].reverse().map((c) => `
            <div class="comment">
              ${avatarHtml(c.author)}
              <div>
                <div class="comment-head"><b>@${esc(c.author)}</b> &middot; ${timeAgo(c.createdAt)}</div>
                <div class="comment-text">${esc(c.text)}</div>
              </div>
            </div>`).join("")}
        </div>
      </div>
    </div>

    <aside class="watch-side">
      ${recs.map((r) => `
      <div class="rec-card" onclick="location.hash='#/watch?id=${r.id}'">
        <div class="rec-thumb">
          <img src="/thumbs/${r.id}.svg" alt="" loading="lazy"/>
          ${r.duration ? `<span class="duration">${esc(r.duration)}</span>` : ""}
        </div>
        <div>
          <div class="rec-title">${esc(r.title)}</div>
          <div class="rec-sub">${esc(r.channel)}</div>
          <div class="rec-sub">${fmtViews(r.views)} views &middot; ${timeAgo(r.uploadedAt)}</div>
        </div>
      </div>`).join("")}
    </aside>
  </div>`;

  /* count a view once, on first play */
  const player = document.getElementById("player");
  player.addEventListener("play", async () => {
    if (viewCounted) return;
    viewCounted = true;
    try { const r = await api(`/api/videos/${id}/view`, { method: "POST" }); } catch {}
  });

  /* likes */
  const likeBtn = document.getElementById("like-btn");
  likeBtn.addEventListener("click", async () => {
    if (liked) return;
    liked = true;
    likeBtn.classList.add("liked");
    try {
      const r = await api(`/api/videos/${id}/like`, { method: "POST" });
      document.getElementById("like-count").textContent = fmtViews(r.likes);
    } catch {}
  });

  /* new comment */
  document.getElementById("comment-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    const input = document.getElementById("comment-input");
    const text = input.value.trim();
    if (!text) return;
    try {
      const c = await api(`/api/videos/${id}/comments`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ author: "You", text }),
      });
      input.value = "";
      const list = document.getElementById("comment-list");
      list.insertAdjacentHTML("afterbegin", `
        <div class="comment">
          ${avatarHtml("You")}
          <div>
            <div class="comment-head"><b>@You</b> &middot; just now</div>
            <div class="comment-text">${esc(c.text)}</div>
          </div>
        </div>`);
    } catch (err) { alert(err.message); }
  });
}

function viewUpload() {
  document.title = "Upload - CppTube";
  let file = null;

  $content.innerHTML = `
  <div class="upload-page">
    <h1 class="section-title">Upload a video</h1>

    <div class="upload-hero" id="drop-zone">
      <svg viewBox="0 0 24 24"><path d="M12 4l7 8h-4v8H9v-8H5l7-8z" transform="rotate(180 12 12)"/></svg>
      <h2>Drag and drop a video file</h2>
      <p>MP4, WebM, MKV, MOV, AVI or OGG &mdash; your video is saved on the C++ server</p>
      <p class="file-name" id="file-name"></p>
      <button class="btn-primary" id="pick-btn">Select file</button>
    </div>

    <form id="upload-form">
      <div class="field">
        <label for="up-title">Title</label>
        <input id="up-title" placeholder="Give your video a title" maxlength="100" />
      </div>
      <div class="field">
        <label for="up-channel">Channel name</label>
        <input id="up-channel" placeholder="You" maxlength="40" />
      </div>
      <div class="field">
        <label for="up-desc">Description</label>
        <textarea id="up-desc" placeholder="Tell viewers about your video" maxlength="2000"></textarea>
      </div>

      <div class="progress-wrap" id="progress-wrap" hidden>
        <div class="progress-bar"><div class="progress-fill" id="progress-fill"></div></div>
        <div class="progress-text" id="progress-text">0%</div>
      </div>
      <div class="upload-error" id="upload-error"></div>
      <button type="submit" class="btn-primary" id="submit-btn" disabled>Upload</button>
    </form>
  </div>`;

  const dropZone = document.getElementById("drop-zone");
  const fileName = document.getElementById("file-name");
  const input = document.createElement("input");
  input.type = "file";
  input.accept = "video/*,.mkv";

  const setFile = (f) => {
    file = f;
    fileName.textContent = f ? f.name : "";
    document.getElementById("submit-btn").disabled = !f;
  };

  document.getElementById("pick-btn").addEventListener("click", (e) => {
    e.preventDefault(); input.click();
  });
  input.addEventListener("change", () => setFile(input.files[0]));

  ;["dragover", "dragenter"].forEach((ev) =>
    dropZone.addEventListener(ev, (e) => { e.preventDefault(); dropZone.classList.add("drag"); }));
  ;["dragleave", "drop"].forEach((ev) =>
    dropZone.addEventListener(ev, (e) => { e.preventDefault(); dropZone.classList.remove("drag"); }));
  dropZone.addEventListener("drop", (e) => {
    if (e.dataTransfer.files.length) setFile(e.dataTransfer.files[0]);
  });

  document.getElementById("upload-form").addEventListener("submit", (e) => {
    e.preventDefault();
    if (!file) return;

    const fd = new FormData();
    fd.append("video", file);
    fd.append("title", document.getElementById("up-title").value.trim());
    fd.append("channel", document.getElementById("up-channel").value.trim());
    fd.append("description", document.getElementById("up-desc").value.trim());

    const wrap = document.getElementById("progress-wrap");
    const fill = document.getElementById("progress-fill");
    const text = document.getElementById("progress-text");
    const errBox = document.getElementById("upload-error");
    const btn = document.getElementById("submit-btn");
    wrap.hidden = false; errBox.textContent = ""; btn.disabled = true;

    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/upload");
    xhr.upload.addEventListener("progress", (ev) => {
      if (!ev.lengthComputable) return;
      const pct = Math.round((ev.loaded / ev.total) * 100);
      fill.style.width = pct + "%";
      text.textContent = `Uploading... ${pct}%`;
    });
    xhr.addEventListener("load", () => {
      if (xhr.status === 201) {
        text.textContent = "Upload complete!";
        location.hash = `#/watch?id=${JSON.parse(xhr.responseText).id}`;
      } else {
        let msg = "Upload failed";
        try { msg = JSON.parse(xhr.responseText).error || msg; } catch {}
        errBox.textContent = msg;
        btn.disabled = false;
      }
    });
    xhr.addEventListener("error", () => {
      errBox.textContent = "Network error during upload";
      btn.disabled = false;
    });
    xhr.send(fd);
  });
}

/* ============================================================
   Router
   ============================================================ */
async function route() {
  const hash = location.hash || "#/";
  const [pathPart, queryPart] = hash.slice(1).split("?");
  const params = new URLSearchParams(queryPart || "");
  const q = params.get("q") || "";
  if (q) $searchInput.value = q; else if (pathPart !== "results") $searchInput.value = "";

  try {
    if (pathPart === "/" || pathPart === "") await viewHome("");
    else if (pathPart === "/results") await viewHome(q);
    else if (pathPart === "/watch") await viewWatch(params.get("id"));
    else if (pathPart === "/upload") viewUpload();
    else await viewHome("");
  } catch (err) {
    $content.innerHTML = `<div class="empty-state">${esc(err.message)}</div>`;
  }
  window.scrollTo(0, 0);
}

document.getElementById("search-form").addEventListener("submit", (e) => {
  e.preventDefault();
  const q = $searchInput.value.trim();
  location.hash = q ? `#/results?q=${encodeURIComponent(q)}` : "#/";
});

document.getElementById("menu-btn").addEventListener("click", () => {
  document.getElementById("sidebar").classList.toggle("hidden");
});

window.addEventListener("hashchange", route);
route();
