// vibeslideshow — single-binary beat-slideshow server.
// vibeslideshow <video-dir> [--port N]

import fsp from "node:fs/promises";
import { statSync } from "node:fs";
import { join, dirname } from "node:path";
import { homedir } from "node:os";
import os from "node:os";

const EXTS = new Set([".mp4", ".webm", ".mov", ".mkv", ".m4v"]);
const CT: Record<string, string> = {
  ".mp4": "video/mp4", ".m4v": "video/mp4", ".webm": "video/webm",
  ".mov": "video/quicktime", ".mkv": "video/x-matroska",
};
const MAX_UPLOAD = 100 * 1024 * 1024;

type Vid = { id: number; path: string; duration: number; ext: string; tod: number | null; day: string | null; score: number; size: number; w?: number; h?: number; lrf?: string; lrfDur?: number; lrfSize?: number; mv?: number; dk?: number };

const ext = (p: string) => p.slice(p.lastIndexOf(".")).toLowerCase();

// Capture timestamp parsed from a 14-digit YYYYMMDDHHMMSS stamp in the filename
// (DJI-style, stored in UTC): day as YYYY-MM-DD and time-of-day in hours; nulls when absent.
function stampOf(p: string): { day: string; tod: number } | null {
  const m = p.match(/\d{14}/);
  if (!m) return null;
  const s = m[0];
  const h = parseInt(s.slice(8, 10), 10);
  const mi = parseInt(s.slice(10, 12), 10);
  if (h > 23 || mi > 59) return null;
  const y = parseInt(s.slice(0, 4), 10), mo = parseInt(s.slice(4, 6), 10), d = parseInt(s.slice(6, 8), 10);
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return null;
  return {
    day: `${s.slice(0, 4)}-${s.slice(4, 6)}-${s.slice(6, 8)}`,
    tod: h + mi / 60,
  };
}

try {
  if ((await Bun.spawn(["ffprobe", "-version"], { stdout: "ignore" }).exited) !== 0) throw new Error();
} catch {
  console.error("ffprobe not found/usable — install ffmpeg (required for indexing and beat analysis)");
  process.exit(1);
}

// ---------- 1. CLI + video index ----------

let videoDirs: string[] = [];
let port = 8080;
const argv = process.argv.slice(2);
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === "--port") { port = parseInt(argv[++i], 10); }
  else if (a.startsWith("--port=")) { port = parseInt(a.slice(7), 10); }
  else if (a === "--help" || a === "-h") { break; }
  else videoDirs.push(a);
}

function usage() {
  console.error("usage: vibeslideshow <video-dir>... [--port N]");
  process.exit(1);
}

if (!videoDirs.length) usage();
for (const dir of videoDirs.slice()) {
  try { if (!statSync(dir).isDirectory()) throw new Error(); } catch { usage(); }
}

const paths: string[] = [];
// DJI proxy twins: same stem+directory with a .LRF extension (low-res proxies).
const lrfFor = new Map<string, string>();
const stem = (p: string) => p.slice(0, p.lastIndexOf(".")).toLowerCase();
for (const dir of videoDirs) {
  const dirents = await fsp.readdir(dir, { withFileTypes: true, recursive: true });
  for (const d of dirents) {
    if (!d.isFile()) continue;
    const p = (d.parentPath ?? dir) + (d.parentPath?.endsWith("/") ? "" : "/") + d.name;
    if (ext(d.name) === ".lrf") lrfFor.set(stem(p), p);
    else if (EXTS.has(ext(d.name))) paths.push(p);
  }
}
paths.sort();

const videos: Vid[] = [];
for (let i = 0; i < paths.length; i += 12) {
  await Promise.all(paths.slice(i, i + 12).map(async p => {
    try {
      const proc = Bun.spawn(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", p],
      );
      const out = (await new Response(proc.stdout).text()).trim();
      const d = parseFloat(out);
      if ((await proc.exited) !== 0 || !isFinite(d) || d <= 0) throw new Error("bad duration");
      const stamp = stampOf(p);
      const v: Vid = { id: 0, path: p, duration: d, ext: ext(p), size: statSync(p).size, tod: stamp ? stamp.tod : null, day: stamp ? stamp.day : null, score: 0 };
      // aspect ratio for the client's ratio filter; absent when unreadable
      try {
        const ap = Bun.spawn(
          ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height", "-of", "csv=p=0", p],
        );
        const mo = /(\d+)\D+(\d+)\s*$/.exec((await new Response(ap.stdout).text()).trim());
        if (mo) { v.w = +mo[1]; v.h = +mo[2]; }
      } catch { /* dims stay absent: ratio filter just won't match */ }
      const lrf = lrfFor.get(stem(p));
      if (lrf) {
        try {
          const lp = Bun.spawn(
            ["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", lrf],
          );
          const ld = parseFloat((await new Response(lp.stdout).text()).trim());
          if ((await lp.exited) === 0 && isFinite(ld) && ld > 0) { v.lrf = lrf; v.lrfDur = ld; v.lrfSize = statSync(lrf).size; }
        } catch { /* proxy not readable: fall back to the original file */ }
      }
      videos.push(v);
    } catch {
      console.error(`skipping (no duration): ${p}`);
    }
  }));
}
videos.sort((a, b) => a.path.localeCompare(b.path));
videos.forEach((v, i) => (v.id = i));
if (!videos.length) {
  console.error("no playable videos found in " + videoDirs.join(", "));
  process.exit(1);
}
console.log(`indexed ${videos.length} video(s)`);

// ---- 1b. Interest scoring for train-window footage: moving scenery, colour
// (sky/landscape vs tunnel grey), sane exposure and variety along the trip.
// Per-video-dir score cache, loaded merged (paths are dir-relative keys already);
// each dir gets its own ".vibeslideshow-score-cache.json". Read-only mounts
// (camera SD cards) fall back to ~/.cache/vibeslideshow.
const cachePaths: Record<string, string> = {}; // dir -> effective cache file
let cache: Record<string, { s: number; m: number; ls?: number; v: number; mv?: number; dk?: number; ver?: number }> = {};
for (const dir of videoDirs) {
  const fb = join(homedir(), ".cache", "vibeslideshow", `cache-${dir.replace(/[^A-Za-z0-9.]+/g, "_")}.json`);
  for (const p of [dir + "/.vibeslideshow-score-cache.json", fb]) {
    try { Object.assign(cache, JSON.parse(await fsp.readFile(p, "utf8"))); break; } catch { /* try next */ }
  }
  cachePaths[dir] = dir + "/.vibeslideshow-score-cache.json";
}

async function grabGray(p: string, t: number): Promise<Uint8Array> {
  const proc = Bun.spawn([
    "ffmpeg", "-v", "error", "-ss", String(t), "-i", p,
    "-frames:v", "1", "-vf", "scale=32:18", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1",
  ]);
  const buf = await new Response(proc.stdout).arrayBuffer();
  if ((await proc.exited) !== 0) throw new Error("sample fail");
  const arr = new Uint8Array(buf);
  if (arr.length < 32 * 18 * 3) throw new Error("short frame");
  return arr;
}

async function scoreVideo(p: string, dur: number): Promise<{ score: number; mv: number; dk: number }> {
  const want = Math.min(12, Math.max(3, Math.ceil(dur / 4)));
  const samples: Uint8Array[] = [];
  for (let s = 0; s < want; s++) {
    const t = Math.round(((dur / (want + 1)) * (s + 1)) * 10) / 10;
    try { samples.push(await grabGray(p, t)); } catch { /* skip failed seek */ }
  }
  if (samples.length < Math.min(3, want)) throw new Error("unreadable");
  let expSum = 0, colSum = 0, motSum = 0;
  const means: number[] = [];
  const px = 32 * 18;
  for (let q = 0; q < samples.length; q++) {
    const f = samples[q];
    let lumSum = 0, lumSq = 0, rgSum = 0, rgSq = 0, ybSum = 0, ybSq = 0;
    for (let j = 0; j < px; j++) {
      const r = f[j * 3], g = f[j * 3 + 1], b = f[j * 3 + 2];
      const lum = (r + g + b) / 3;
      lumSum += lum; lumSq += lum * lum;
      const rg = r - g, yb = (r + g) / 2 - b;
      rgSum += rg; rgSq += rg * rg; ybSum += yb; ybSq += yb * yb;
    }
    const lum = lumSum / px;
    means.push(lum);
    const mean = lum / 255;
    const sd = Math.sqrt(Math.max(0, lumSq / px - lum * lum)) / 255;
    // exposure: dead dark (tunnel), blown white, or empty tonal range read as boring
    const exp = mean <= 0.06 || mean >= 0.92
      ? 0
      : Math.max(0, 1 - Math.max(0, mean < 0.2 ? 0.2 - mean : mean - 0.65) * 6);
    // colourfulness (Hasler–Süsstrunk style): sky and landscape beat tunnel grey
    const rgM = rgSum / px, ybM = ybSum / px;
    const col = Math.sqrt(rgSq / px - rgM * rgM + ybSq / px - ybM * ybM) +
      0.3 * Math.sqrt(rgM * rgM + ybM * ybM);
    colSum += Math.min(1, col / 50);
    expSum += exp;
    if (q > 0) {
      const g0 = samples[q - 1];
      let dm = 0;
      for (let j = 0; j < px; j++) dm += Math.abs(f[j * 3] - g0[j * 3]) + Math.abs(f[j * 3 + 1] - g0[j * 3 + 1]) + Math.abs(f[j * 3 + 2] - g0[j * 3 + 2]);
      motSum += Math.min(1, dm / (px * 3) / 255 / 0.07);
    }
  }
  const n = samples.length;
  // variety along the trip: parked/train-stuck frames repeat; a journey drifts through scenes
  const vm = means.reduce((a, b) => a + b, 0) / n;
  const vsd = Math.sqrt(means.reduce((a, b) => a + (b - vm) * (b - vm), 0) / n);
  const variety = Math.min(1, vsd / 25);
  // mv: raw fast-motion metric (0-100), saturated per sample pair at ~7% frame change —
  // standing-still or parked shots score low, vehicles driving fast score near the top.
  // dk: fraction of sampled frames in darkness (tunnels) — mostly-dark clips read as
  // black flashes in the show, so the pool can drop them entirely.
  return {
    score: Math.round(100 * (0.35 * (motSum / (n - 1 || 1)) + 0.25 * (colSum / n) + 0.25 * (expSum / n) + 0.15 * variety)),
    mv: Math.round(100 * (motSum / (n - 1 || 1))),
    dk: means.reduce((a, m) => a + (m < 10 ? 1 : 0), 0) / n,
  };
}

let hits = 0, doneCount = 0;
// Progress for the client's live scoring bar; scoreTotal -1 = nothing running.
let scoreTotal = -1, scoreDone = 0;
// Scores fill in AFTER the server starts listening (first runs don't block the
// endpoint; /videos serves progressively better scores as this completes).
(async () => {
scoreTotal = videos.length;
for (let i = 0; i < videos.length; i += 12) {
  await Promise.all(videos.slice(i, i + 12).map(async v => {
    const st = statSync(v.path);
    const stL = v.lrf ? statSync(v.lrf) : null;
    const hit = cache[v.path];
    if (hit && hit.ver === 2 && hit.mv !== undefined && hit.dk !== undefined && hit.s === st.size && hit.m === Math.round(st.mtimeMs) &&
        (!v.lrf || hit.ls === stL!.size)) { v.score = hit.v; v.mv = hit.mv; v.dk = hit.dk; hits++; }
    else {
      try { const r2 = await scoreVideo(v.lrf ?? v.path, v.lrf ? v.lrfDur! : v.duration); v.score = r2.score; v.mv = r2.mv; v.dk = r2.dk; } catch { v.score = 0; v.mv = 0; v.dk = 1; }
      cache[v.path] = { s: st.size, m: Math.round(st.mtimeMs), ls: stL ? stL.size : undefined, v: v.score, mv: v.mv, dk: v.dk, ver: 2 };
    }
    doneCount++;
    scoreDone = doneCount;
    if (doneCount === videos.length || doneCount % 10 === 0)
      console.log(`scoring ${doneCount}/${videos.length} (${hits} cached)`);
    if (doneCount % 120 === 0) await writeCaches(); // don't lose partial progress on restart
  }));
}
async function writeCaches() {
  let cacheWritesFailed = 0;
  for (const dir of videoDirs) {
    const own: Record<string, { s: number; m: number; ls?: number; v: number; mv?: number; dk?: number; ver?: number }> = {};
    for (const k in cache) if (k.startsWith(dir)) own[k] = cache[k];
    if (!Object.keys(own).length) continue;
    try { await fsp.writeFile(cachePaths[dir], JSON.stringify(own)); continue; } catch { /* read-only etc: fall through */ }
    try {
      const fb = join(homedir(), ".cache", "vibeslideshow", `cache-${dir.replace(/[^A-Za-z0-9.]+/g, "_")}.json`);
      await fsp.mkdir(dirname(fb), { recursive: true });
      await fsp.writeFile(fb, JSON.stringify(own));
      console.log(`note: score cache written to ${fb} (video dir not writable)`);
    } catch { cacheWritesFailed++; }
  }
  return cacheWritesFailed;
}
const cf = await writeCaches();
if (cf)
  console.error(`note: ${cf} score cache(s) not writable — clips rescore on each start`);
console.log(`scored ${videos.length} clips (${hits} cached)`);
scoreTotal = -1;
})();

// ---------- 2. Beat analysis ----------

const SR = 22050;
class BadAudioError extends Error { constructor() { super(); this.name = "BadAudioError"; } }

async function decodeAudio(path: string): Promise<Float32Array> {
  const p = Bun.spawn(
    ["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", String(SR), "-f", "f32le", "pipe:1"],
  );
  const buf = await new Response(p.stdout).arrayBuffer();
  if ((await p.exited) !== 0) throw new BadAudioError();
  return new Float32Array(buf);
}

type Pattern = { offsets: number[]; at: number[] };

function analyzeSong(pcm: Float32Array, sr: number): { beats: number[]; bpm?: number; energy: number[]; beatPattern?: Pattern; bright: number[]; sections: { s: number; e: number; b: number; kind: string }[]; mood: string } {
  const win = 1024, hop = 512;
  const n = Math.max(1, Math.floor((pcm.length - win) / hop) + 1);
  const e = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let sum = 0;
    const off = i * hop;
    for (let j = 0; j < win; j++) { const v = pcm[off + j] || 0; sum += v * v; }
    e[i] = Math.sqrt(sum / win);
  }
  const nov = new Float32Array(n);
  for (let i = 1; i < n; i++) nov[i] = Math.max(0, e[i] - e[i - 1]);
  const sm = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let s = 0, c = 0;
    for (let k = i - 1; k <= i + 1; k++) if (k >= 0 && k < n) { s += nov[k]; c++; }
    sm[i] = s / c;
  }
  const strengths: number[] = [];
  const beatTimes: number[] = [];
  const R = 43;
  let last = -1;
  for (let i = 1; i < n - 1; i++) {
    if (!(sm[i] >= sm[i - 1] && sm[i] >= sm[i + 1])) continue;
    let s = 0, c = 0;
    for (let k = Math.max(0, i - R); k <= Math.min(n - 1, i + R); k++) { s += sm[k]; c++; }
    if (sm[i] <= (s / c) * 1.5) continue;
    const t = (i * hop) / sr;
    if (last >= 0 && t - last < 0.25) continue;
    beatTimes.push(t);
    strengths.push(sm[i]);
    last = t;
  }
  const beats = beatTimes.map(t => Math.round(t * 1000) / 1000);
  if (!beats.length || beats[0] > 0.25) { beats.unshift(0); strengths.unshift(0); }

  // ---- pulse snap: grid anchored at the strongest onset; onsets within 14% of
  // the median interval snap to exact pulse multiples (cuts land on the beat);
  // sub-beat hits and section tempo changes keep their real onsets.
  if (beats.length >= 4) {
    const gaps2: number[] = [];
    for (let i2 = 1; i2 < beats.length; i2++) gaps2.push(beats[i2] - beats[i2 - 1]);
    const gsort = [...gaps2].sort((a, b) => a - b);
    const med = gsort[Math.floor(gsort.length / 2)];
    if (med >= 0.15 && med <= 1.5) {
      let am = 0;
      for (let i2 = 1; i2 < strengths.length; i2++) if (strengths[i2] > strengths[am]) am = i2;
      const A = beats[am];
      const sB: number[] = [], sS: number[] = [];
      for (let i2 = 0; i2 < beats.length; i2++) {
        const kk = Math.round((beats[i2] - A) / med);
        const target = A + kk * med;
        sB.push(Math.abs(beats[i2] - target) <= 0.14 * med && beats[i2] !== A ? Math.round(target * 1000) / 1000 : beats[i2]);
        sS.push(strengths[i2]);
      }
      // two onsets snapped to the same slot: keep the stronger
      const oB: number[] = [], oS: number[] = [];
      for (let i2 = 0; i2 < sB.length; i2++) {
        if (oB.length && sB[i2] === oB[oB.length - 1]) {
          if (sS[i2] > oS[oS.length - 1]) oS[oS.length - 1] = sS[i2];
          continue;
        }
        oB.push(sB[i2]); oS.push(sS[i2]);
      }
      beats.length = 0; strengths.length = 0;
      beats.push(...oB); strengths.push(...oS);
    }
  }

  let bpm: number | undefined;
  if (beats.length >= 2) {
    const gaps = beats.slice(1).map((b, i) => b - beats[i]).sort((a, b) => a - b);
    bpm = Math.round((60 / gaps[Math.floor(gaps.length / 2)]) * 10) / 10;
  }
  const maxStrength = Math.max(...strengths, 1e-9);
  const energy = strengths.map(s => Math.round((s / maxStrength) * 1000) / 1000);

  // ---- mood: per-beat brightness (zero-crossing rate inside the beat span) and
  // song sections from the smoothed energy curve. Sections drive the cut style:
  // chill gets long soft crossfades and longer holds, loud/drops get hard cuts.
  const bright: number[] = [];
  if (beats.length >= 2) {
    const zs: number[] = [];
    let maxZ = 0;
    for (let bi = 0; bi < beats.length; bi++) {
      const a = Math.floor(beats[bi] * sr);
      const b2 = Math.min(pcm.length, Math.floor((beats[bi + 1] ?? beats[bi] + 0.5) * sr));
      let flips = 0;
      for (let k = a + 1; k < b2; k++) if ((pcm[k] >= 0) !== (pcm[k - 1] >= 0)) flips++;
      const z = flips / Math.max(1, b2 - a);
      zs.push(z);
      if (z > maxZ) maxZ = z;
    }
    for (const z of zs) bright.push(Math.round((z / (maxZ || 1)) * 1000) / 1000);
  }
  const sections: { s: number; e: number; b: number; kind: string }[] = [];
  if (beats.length >= 8) {
    // classify per song: a section is "loud" relative to the song's own median
    // energy (masters with little dynamics make max-normalisation useless here)
    const pos2 = energy.slice(1).filter(x => x > 0).sort((a, b) => a - b);
    const medE = pos2[Math.floor(pos2.length / 2)] || 0.25;
    const W = 8;
    const rows: { s: number; e: number; b: number; ra: number }[] = [];
    for (let bi = 0; bi + 1 < beats.length; bi += W) {
      let sum = 0, bs = 0, c = 0;
      for (let k = bi; k < Math.min(beats.length, bi + W); k++) { sum += energy[k]; bs += bright[k] ?? 0.5; c++; }
      if (c) { const e = sum / c; rows.push({ s: beats[bi], e, b: bs / c, ra: e / medE, bi }); }
    }
    for (let i2 = 0; i2 < rows.length; i2++) {
      const bR = rows[Math.max(0, i2 - 1)].ra, aR = rows[Math.min(rows.length - 1, i2 + 1)].ra;
      const r = rows[i2];
      let kind: string;
      if (r.ra - bR > 0.3 && aR > r.ra) kind = 'build';
      else if (r.ra >= 1.35 && bR <= r.ra - 0.3) kind = 'drop';
      else if (r.ra < 0.65) kind = 'chill';
      else if (r.ra >= 1.35) kind = 'loud';
      else kind = 'mid';
      sections.push({ s: Math.round(r.s * 100) / 100, e: Math.round(r.e * 1000) / 1000, b: Math.round(r.b * 1000) / 1000, kind, bi: r.bi });
    }
  }
  let mood = 'mixed';
  if (sections.length) {
    const lvl = Math.round(1000 * e.reduce((a, b) => a + b, 0) / n) / 1000; // absolute loudness
    const calmFrac = sections.filter(x => x.e < 0.3).length / sections.length;
    mood = lvl >= 0.25 && (bpm ?? 0) >= 150 ? 'aggressive'
      : lvl >= 0.15 && sections.some(x => x.kind === 'drop') ? 'energetic'
      : lvl < 0.08 || calmFrac >= 0.5 ? 'calm'
      : 'mid';
  }

  // ---- stylistic stutter replication: find off-beat hits that recur at a
  // consistent relative position inside the beat interval (e.g. chopped-vocal
  // doubles at half-beat) and report where they appear, per beat index.
  let beatPattern: Pattern | undefined;
  if (beats.length >= 10) {
    const pos = strengths.filter(s => s > 0).sort((a, b) => a - b);
    const med = pos[Math.floor(pos.length / 2)] || 1;
    const micro: Record<number, number[]> = {}; // beat interval index -> offsets
    let bi = 0;
    for (let f = 1; f < n - 1; f++) {
      if (!(sm[f] >= sm[f - 1] && sm[f] >= sm[f + 1])) continue;
      if (sm[f] < 0.35 * med) continue;
      const t = (f * hop) / sr;
      while (bi + 1 < beats.length && beats[bi + 1] <= t) bi++;
      if (bi + 1 >= beats.length) continue; // no interval end after the last beat
      const off = (t - beats[bi]) / (beats[bi + 1] - beats[bi]);
      if (off < 0.04 || off > 0.96) continue;
      (micro[bi] ||= []).push(Math.round(off * 20) / 20);
    }
    const sigs = new Map<string, { count: number; beats: number[] }>();
    for (const k of Object.keys(micro)) {
      const uniq = [...new Set(micro[+k])].sort((a, b) => a - b);
      if (!uniq.length) continue;
      const key = uniq.join("|");
      const cur = sigs.get(key) ?? { count: 0, beats: [] };
      cur.count++;
      cur.beats.push(+k);
      sigs.set(key, cur);
    }
    const best = [...sigs.entries()]
      .map(([key, s]) => ({ ...s, offsets: key.split("|").map(Number) }))
      .filter(s => s.count >= 5)
      .sort((a, b) => b.count - a.count)[0];
    if (best) beatPattern = { offsets: best.offsets, at: best.beats };
  }
  return { beats, bpm: bpm ? Math.round(bpm * 10) / 10 : undefined, energy, beatPattern, bright, sections, mood };
}

// ---------- 3. HTTP server + 4.PAGE ----------

const PAGE = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Beat slideshow</title>
<style>
  * { box-sizing: border-box; }
  [hidden] { display: none !important; }
  body { margin: 0; font-family: system-ui, sans-serif; background: #111; color: #eee; }
  #form { max-width: 520px; margin: 12vh auto; display: flex; flex-direction: column; gap: 14px; }
  h1 { font-size: 1.6rem; margin: 0; color: #fff; }
  #nvids { margin: -8px 0 0; color: #999; font-size: .9rem; }
  #sprogWrap { margin-top: 6px; }
  #sprogTrack { position: relative; height: 6px; background: #2a2a2e; border-radius: 3px; overflow: visible; margin-bottom: 18px; }
  #sprog { position: absolute; inset: 0; width: 0%; background: #1f6feb; border-radius: 3px; transition: width .5s linear; }
  #sprogText { position: absolute; right: 8px; top: 9px; font-size: .78rem; color: #999; white-space: nowrap; }
  input[type=file] { color: #aaa; }
  input[type=file]::file-selector-button { background: #26262b; border: 1px solid #444; color: #fff;
    border-radius: 8px; padding: 10px 14px; margin-right: 12px; cursor: pointer; font-size: 1rem; }
  #towRow { transition: opacity .2s; }
  #towRow:has(#towOn:not(:checked)) { opacity: .55; }
  #towRow input[type=time], #dayRow input[type=date] { background: #111; color: #eee; border: 1px solid #444;
                             border-radius: 6px; padding: 4px 6px; color-scheme: dark; }
  #dayRow { transition: opacity .2s; }
  #dayRow:has(#dayOnly:not(:checked)) { opacity: .55; }
  #towList .trow { display: flex; align-items: center; gap: 7px; margin: 7px 0 0 22px; }
  #towList .trow .rm { background: #26262b; border: 1px solid #444; border-radius: 6px;
                       color: #aaa; padding: 3px 9px; cursor: pointer; font-size: .85rem; }
  #towAdd { background: #26262b; border: 1px solid #444; color: #ccc; font-size: .85rem;
            border-radius: 6px; padding: 4px 10px; cursor: pointer; margin: 8px 0 0 22px; }
  #towHint { color: #777; font-size: .85rem; }
  button { background: #1f6feb; color: #fff; border: 0; border-radius: 8px; padding: 10px 16px; font-size: 1rem; cursor: pointer; }
  button:disabled { background: #333; color: #777; cursor: default; }
  #result { display: flex; align-items: center; gap: 12px; }
  #status { min-height: 1.2em; color: #fc0; font-size: .95rem; }
  #stage { position: fixed; inset: 0; background: #000; }
  #stage video { position: absolute; inset: 0; width: 100%; height: 100%; object-fit: contain;
                 opacity: 0; transition: opacity 120ms linear; }
  #stop { position: absolute; top: 14px; right: 14px; background: rgba(255,255,255,.12);
          color: #fff; border: 1px solid #555; border-radius: 6px; padding: 6px 11px; }
  #gear2 { position: absolute; top: 14px; right: 62px; background: rgba(255,255,255,.12);
           color: #fff; border: 1px solid #555; border-radius: 6px; padding: 5px 11px; font-size: 1rem; }
  .hrow { display: flex; justify-content: space-between; align-items: center; }
  .hrow button { background: #26262b; border: 1px solid #444; border-radius: 8px; padding: 8px 14px; }
  #panel { position: fixed; inset: 0; background: rgba(0,0,0,.45); display: none;
           align-items: center; justify-content: center; z-index: 20; }
  #panel.open { display: flex; }
  #panelBox { background: #1b1b1f; border: 1px solid #333; border-radius: 12px;
              padding: 18px 22px; width: min(500px, 94vw); max-height: 82vh; overflow: auto; }
  #panelBox h2 { margin: 0 0 10px; font-size: 1.05rem; color: #fff; }
  .srow { display: flex; align-items: center; justify-content: space-between; gap: 10px;
          margin: 6px 0; font-size: .88rem; color: #ccc; }
  .srow input[type=number] { width: 5.5em; background: #111; color: #eee; border: 1px solid #444;
                             border-radius: 6px; padding: 5px 7px; }
  #panelFoot { display: flex; justify-content: space-between; margin-top: 12px; }
  #reset { background: #333; }
  #profRow { display: flex; gap: 8px; margin: 12px 0 4px; align-items: center; }
  #profRow select { flex: 1; background: #111; color: #eee; border: 1px solid #444; border-radius: 6px; padding: 5px 7px; }
  #profRow button { background: #26262b; border: 1px solid #444; border-radius: 8px; padding: 8px 12px; }
  #edit { position: fixed; inset: 0; background: #0b0b0e; z-index: 18; display: flex;
          flex-direction: column; padding: 12px; gap: 10px; }
  #ebar { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
  #ebar button, #esel button { background: #26262b; border: 1px solid #444; color: #fff;
          border-radius: 8px; padding: 7px 12px; font-size: .9rem; cursor: pointer; }
  #ebar button:disabled, #esel button:disabled { background: #222; color: #666; cursor: default; }
  #etitle { color: #fff; font-weight: 600; }
  #estats, #einfo { color: #999; font-size: .85rem; margin-right: auto; margin-left: 8px; }
  #estrip { flex: 1; display: flex; align-items: stretch; gap: 2px; overflow-x: auto;
            padding: 6px 2px; border: 1px solid #222; border-radius: 8px; background: #101014; }
  .ecel { flex: 0 0 auto; min-width: 7px; border-radius: 4px; cursor: pointer; opacity: .92; }
  .ecel:hover { opacity: 1; }
  .ecel.sel { outline: 2px solid #ffd400; }
  #esel { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
  #ctl { position: fixed; left: 50%; bottom: 16px; transform: translateX(-50%); width: min(760px, 94vw);
         display: flex; gap: 10px; align-items: center; background: rgba(15,15,18,.82); border: 1px solid #333;
         border-radius: 10px; padding: 10px 14px; z-index: 15; opacity: 1; transition: opacity .3s; }
  #ctl.off { opacity: 0; pointer-events: none; }
  #ctl button { background: #26262b; color: #fff; border: 1px solid #444; border-radius: 8px;
                padding: 6px 12px; cursor: pointer; font-size: 1rem; }
  #ctl span { color: #ccc; font-size: .85rem; min-width: 3.4em; text-align: center; font-variant-numeric: tabular-nums; }
  #tbarWrap { position: relative; flex: 1; height: 18px; display: flex; align-items: center; }
  #tbuf { position: absolute; left: 0; right: 0; top: 7px; height: 4px; border-radius: 2px;
          background-color: #3a3a40; pointer-events: none; }
  #tbar { -webkit-appearance: none; appearance: none; width: 100%; background: transparent; margin: 0; height: 18px; }
  #tbar::-webkit-slider-runnable-track { height: 4px; border-radius: 2px; background: transparent; }
  #tbar::-webkit-slider-thumb { -webkit-appearance: none; width: 12px; height: 12px;
          border-radius: 50%; background: #1f6feb; margin-top: -4px; }
</style>
</head>
<body>
<div id="form">
  <div class="hrow">
    <h1>Beat slideshow</h1>
    <button id="gear" title="Slideshow settings">⚙</button>
  </div>
  <p id="nvids">loading…</p>
  <div id="sprogWrap" hidden><div id="sprogTrack"><div id="sprog"></div><span id="sprogText"></span></div></div>
  <div id="towRow" style="display:none">
    <label><input type="checkbox" id="towOn"> only clips taken in these ranges</label>
    <div id="towList"></div>
    <button type="button" id="towAdd" title="Add another time range">+ add range</button>
  </div>
  <label id="dayRow" style="display:none">
    <input type="checkbox" id="dayOnly"> only clips taken on
    <input type="date" id="daySel">
  </label>
  <label id="scoreRow" style="display:none">
    <input type="checkbox" id="scoreOnly"> only the most interesting clips — keep top
    <input type="number" id="scorePct" min="10" max="100" step="10" value="50" style="width:3.5em"> %
  </label>
  <label id="arRow" style="display:none">
    <input type="checkbox" id="arOnly"> only
    <select id="arSel">
      <option value="1.78">Landscape (16:9)</option>
      <option value="0.56">Portrait (9:16)</option>
      <option value="1">Square (1:1)</option>
    </select> clips
  </label>
  <label id="mvRow" style="display:none">
    <input type="checkbox" id="mvOnly"> only fast-moving (driving) shots — keep top
    <input type="number" id="mvPct" min="10" max="100" step="10" value="50" style="width:3.5em"> %
  </label>
  <span id="fcount"></span>
  <input type="file" id="file" accept="audio/*">
  <button id="analyze" disabled>Analyze</button>
  <p id="status"></p>
  <div id="result" hidden>
    <span id="summary"></span>
    <button id="start">▶ Start slideshow</button>
    <button id="editBtn" title="Edit the cut timeline">✂ Edit</button>
  </div>
</div>
<div id="stage" hidden>
  <video id="v0" muted playsinline preload="auto"></video>
  <video id="v1" muted playsinline preload="auto"></video>
  <button id="gear2" title="Slideshow settings">⚙</button>
  <button id="stop">✕</button>
  <div id="ctl">
    <button id="pp" title="Play / pause (space)">⏸</button>
    <span id="tcur">0:00</span>
    <div id="tbarWrap"><div id="tbuf"></div><input type="range" id="tbar" min="0" max="1000" value="0"></div>
    <span id="tdur">0:00</span>
  </div>
</div>
<div id="edit" hidden>
  <div id="ebar">
    <span id="etitle">✂ Timeline editor</span>
    <span id="estats"></span>
    <button id="eSeek" title="Fresh in-points for every segment">Re-seek all</button>
    <button id="eShuffle" title="Regenerate the edit with a bumped seed (new in-points everywhere, same song timing)">Reshuffle</button>
    <button id="eRefit" title="Discard edits, rebuild from current settings/filters">Rebuild</button>
    <button id="ePlay">▶ Preview</button>
    <button id="eDone">Done</button>
  </div>
  <div id="estrip"></div>
  <div id="esel" hidden>
    <span id="einfo"></span>
    <button id="oShort" title="Shorten this segment by 0.25 s (later segments shift earlier)">−¼s</button>
    <button id="oLong" title="Lengthen this segment by 0.25 s (later segments shift later)">+¼s</button>
    <button id="oChop" title="Split this segment and insert a fresh clip for the second half">Chop</button>
    <button id="oSeek" title="Pick a new in-point in the same clip">New in-point</button>
    <button id="oSwap" title="Swap in a different clip">Swap clip</button>
    <button id="oDel" title="Delete this segment (its time is absorbed by the last one)">Delete</button>
  </div>
</div>
<div id="panel">
  <div id="panelBox">
    <h2>Slideshow settings</h2>
    <div id="srows"></div>
    <div id="profRow">
      <select id="profSel"><option value="">No profiles saved</option></select>
      <button id="profSave">Save…</button>
      <button id="profDel">Delete</button>
    </div>
    <div id="panelFoot">
      <button id="reset">Reset defaults</button>
      <button id="closePanel">Done</button>
    </div>
  </div>
</div>
<script>
var vids = [];
var towOn = false, towActive = []; // time-window (list of ranges, hour floats); off when towOn is false or none valid
var dayOn = false, daySel = ''; // day filter: only clips stamped on this YYYY-MM-DD when enabled
var scoreMin = null, arT = null; // top-% score and aspect-ratio (w/h) filters; null = off
var mvT = null; // fast-motion (vehicle) top-% filter; null = off
var file = null, beats = null, bpm = null, energy = [];
var beatPattern = { offsets: [], at: [] };
var moodData = { bright: [], sections: [], mood: 'mixed' };
var $ = function (id) { return document.getElementById(id); };

// ---- Generation parameters (all user-editable in the settings panel) ----
var DEFAULTS = {
  holdMax: 3, energyHi: 0.67, energyLo: 0.33,
  aggro: 1,
  split: 3, stutter: true, stutterMaxBpm: 140, stutterMax: 12, stutterGap: 3.5,
  stutterMinHold: 0.9, stutterRamp: 0.4, stutterAccent: 0.85,
  fadeMs: 120,
  darkMean: 3, darkBrightPct: 1, darkRetries: 4,
  useLrf: true,
  precache: true,
  mood: true,
  fancy: true,
  shuffle: false,
  skipJunk: true,
  replicate: true,
  aggrThreshold: 0.85, aggrHalfRun: 3, aggrQuarterRun: 6,
  reactive: true, reactiveMs: 250, reactiveCool: 1.5,
  gridSec: 0.5,
  seed: 0,
  skipWindows: '2026-08-14±1',
};
var P = Object.assign({}, DEFAULTS);
try { Object.assign(P, JSON.parse(localStorage.getItem('vs-params') || '{}')); } catch (e) { /* corrupt settings: defaults */ }
function saveP() { try { localStorage.setItem('vs-params', JSON.stringify(P)); } catch (e) { /* storage unavailable */ } }

var SCHEMA = [
  { k: 'holdMax',        l: 'Longest hold (beats per clip)', min: 1, max: 6, step: 1 },
  { k: 'aggro',          l: 'Aggressivity (0 = long holds, 2 = always on-beat)', min: 0, max: 2, step: 0.25 },
  { k: 'energyHi',       l: 'Cut-every-beat energy ≥',       min: 0.5, max: 0.95, step: 0.05 },
  { k: 'energyLo',       l: 'Max-hold energy ≤',             min: 0.05, max: 0.45, step: 0.05 },
  { k: 'split',          l: 'Stutter split (segments)',      min: 2, max: 5, step: 1 },
  { k: 'stutter',        l: 'Drop stutters (double splits)', type: 'check' },
  { k: 'stutterMaxBpm',  l: 'Stutters only below bpm',       min: 60, max: 180, step: 5 },
  { k: 'stutterMax',     l: 'Stutter cap per song',          min: 0, max: 30, step: 1 },
  { k: 'stutterGap',     l: 'Min seconds between stutters',  min: 0, max: 10, step: 0.5 },
  { k: 'stutterMinHold', l: 'Min segment length to split (s)', min: 0.3, max: 2, step: 0.1 },
  { k: 'stutterRamp',    l: 'Drop ramp threshold',           min: 0.05, max: 0.9, step: 0.05 },
  { k: 'stutterAccent',  l: 'Accent energy threshold',       min: 0.5, max: 1, step: 0.05 },
  { k: 'fadeMs',         l: 'Cut crossfade (ms)',            min: 0, max: 500, step: 10 },
  { k: 'darkMean',       l: 'Dark chunk mean luma (0-255)',  min: 0, max: 40, step: 1 },
  { k: 'darkBrightPct',  l: 'Dark chunk: max % bright pixels', min: 0, max: 10, step: 0.5 },
  { k: 'darkRetries',    l: 'Dark chunk re-rolls',           min: 0, max: 8, step: 1 },
  { k: 'useLrf',         l: 'Play low-res proxies (LRF)',    type: 'check' },
  { k: 'precache',       l: 'Precache whole clips in RAM',   type: 'check' },
  { k: 'skipJunk',       l: 'Skip mostly-dark clips (tunnels)', type: 'check' },
  { k: 'mood',           l: 'Mood matching (cuts and fades follow the music)', type: 'check' },
  { k: 'fancy',          l: 'Indie transitions (colour mixes, crush, punch)', type: 'check' },
  { k: 'shuffle',        l: 'Shuffle clip order (breaks day/chronology)', type: 'check' },
  { k: 'replicate',      l: "Replicate the song's own stutter cuts", type: 'check' },
  { k: 'aggrThreshold',  l: 'Aggressive section energy ≥',   min: 0.6, max: 1.05, step: 0.05 },
  { k: 'aggrHalfRun',    l: 'Half-beat cuts for run ≥ beats', min: 2, max: 8, step: 1 },
  { k: 'aggrQuarterRun', l: 'Quarter-beat cuts for run ≥ beats', min: 4, max: 16, step: 1 },
  { k: 'reactive',       l: 'Early-cut on black screen',     type: 'check' },
  { k: 'reactiveMs',     l: 'Black time before early cut (ms)', min: 100, max: 2000, step: 100 },
  { k: 'reactiveCool',   l: 'Min seconds between early cuts', min: 0, max: 5, step: 0.5 },
  { k: 'gridSec',        l: 'No-beat fallback grid (s)',     min: 0.2, max: 2, step: 0.1 },
  { k: 'seed',           l: 'Edit seed (0 = shuffle, fixed = reproducible edit)', min: 0, max: 99999, step: 1 },
  { k: 'skipWindows',    l: 'Skip clips within date windows, e.g. 2026-08-14±1, 2026-09-01', type: 'text' },
];

// Date-window excludes: "2026-08-14±1" skips that day and ±1 around it.
// No regexes here: the page source is a template literal and would eat backslashes.
// Clips without a readable stamp are never excluded.
function parseWindows(spec) {
  var out = [];
  String(spec || '').split(',').forEach(function (w) {
    w = w.trim();
    if (!w) return;
    var sep = w.indexOf('±');
    var base = sep < 0 ? w : w.slice(0, sep).trim();
    var rad = sep < 0 ? 0 : (parseInt(w.slice(sep + 1).trim(), 10) || 0);
    if (base.length !== 10 || base.charCodeAt(4) !== 45 || base.charCodeAt(7) !== 45) return;
    var y = parseInt(base.slice(0, 4), 10), mo = parseInt(base.slice(5, 7), 10), dd = parseInt(base.slice(8, 10), 10);
    if (!(y > 0) || mo < 1 || mo > 12 || dd < 1 || dd > 31) return;
    var t = Date.parse(base + 'T00:00:00Z');
    if (!isFinite(t)) return;
    out.push({ t: t, r: 864e5 * rad });
  });
  return out;
}

function inSkipWindows(day, wins) {
  if (!day) return false;
  var t = Date.parse(day + 'T00:00:00Z');
  return wins.some(function (w) { return Math.abs(t - w.t) <= w.r; });
}

(function buildPanel() {
  var host = $('srows');
  SCHEMA.forEach(function (f) {
    var row = document.createElement('div');
    row.className = 'srow';
    var lab = document.createElement('span');
    lab.textContent = f.l;
    var inp = document.createElement('input');
    inp.id = 'p_' + f.k;
    if (f.type === 'check') { inp.type = 'checkbox'; inp.checked = !!P[f.k]; }
    else if (f.type === 'text') { inp.type = 'text'; inp.value = P[f.k] || ''; }
    else { inp.type = 'number'; inp.min = f.min; inp.max = f.max; inp.step = f.step; inp.value = P[f.k]; }
    var handleChange = function () {
      var v = f.type === 'check' ? inp.checked
        : f.type === 'text' ? inp.value
        : Math.max(f.min, Math.min(f.max, +inp.value || f.min));
      P[f.k] = v;
      // keep the energy thresholds ordered; nudge the other one when they collide
      if (P.energyLo >= P.energyHi) {
        if (f.k === 'energyHi') P.energyLo = Math.max(0.05, P.energyHi - 0.05);
        else P.energyHi = Math.min(0.95, P.energyLo + 0.05);
      }
      applyParams(inp.id);
    };
    ['input', 'change'].forEach(function (ev) { inp.addEventListener(ev, handleChange); });
    row.appendChild(lab);
    row.appendChild(inp);
    host.appendChild(row);
  });
})();

// ---- Built-in profiles, tuned from slow ambient holds to dense club edits ----
var PRESETS = [
  { n: 'Anything (defaults)', P: {} },
  { n: 'Slow melancholic',    P: { aggro: 0, holdMax: 6, energyHi: 0.85, energyLo: 0.15, stutter: false, gridSec: 1, fadeMs: 200 } },
  { n: 'Ambient drift',       P: { aggro: 0, holdMax: 6, energyHi: 0.95, energyLo: 0.05, stutter: false, split: 2, gridSec: 2, fadeMs: 300 } },
  { n: 'Cinematic montage',   P: { aggro: 0.5, holdMax: 4, energyHi: 0.75, energyLo: 0.25, stutterMax: 6, fadeMs: 150, gridSec: 0.5 } },
  { n: 'Club driver',         P: { aggro: 1.5, holdMax: 2, energyHi: 0.45, energyLo: 0.35, aggrFloor: 0.4, aggrThreshold: 0.5, insMax: 150, stutterMaxBpm: 220, fadeMs: 80, gridSec: 0.5 } },
  { n: 'Techno 185 adrenaline', P: { aggro: 2, holdMax: 1, energyHi: 0.35, energyLo: 0.3, aggrFloor: 0.3, aggrThreshold: 0.35, aggrHalfRun: 2, aggrQuarterRun: 4, insMax: 400, stutterMaxBpm: 300, split: 4, stutterRamp: 0.25, stutterAccent: 0.7, fadeMs: 0, gridSec: 0.3 } },
  { n: 'Bull on cocaine', P: { aggro: 2, holdMax: 1, energyHi: 0.2, energyLo: 0.05, aggrFloor: 0.12, aggrThreshold: 0.1, aggrHalfRun: 2, aggrQuarterRun: 4, split: 5, stutterMaxBpm: 300, stutterMax: 99, stutterGap: 0, stutterMinHold: 0.2, stutterRamp: 0.05, stutterAccent: 0.5, insMax: 2000, fadeMs: 0, gridSec: 0.2, darkRetries: 8, reactiveMs: 300 } },
];

// ---- Profiles: named setting snapshots in localStorage['vs-profiles'] ----
var profiles = {};
try { profiles = JSON.parse(localStorage.getItem('vs-profiles') || '{}'); } catch (e) { profiles = {}; }
function saveProfiles() { try { localStorage.setItem('vs-profiles', JSON.stringify(profiles)); } catch (e) { /* storage unavailable */ } }
function refreshProfSel(selName) {
  var sel = $('profSel'), opt = document.createElement('option');
  sel.innerHTML = '';
  opt.value = '';
  opt.textContent = 'Load profile…';
  sel.appendChild(opt);
  var og = document.createElement('optgroup');
  og.label = 'built-in';
  PRESETS.forEach(function (p) {
    var o = document.createElement('option');
    o.value = '!:' + p.n;
    o.textContent = p.n;
    og.appendChild(o);
  });
  sel.appendChild(og);
  var names = Object.keys(profiles).sort();
  if (names.length) {
    var og2 = document.createElement('optgroup');
    og2.label = 'saved';
    names.forEach(function (n) {
      var o = document.createElement('option');
      o.value = n;
      o.textContent = n;
      og2.appendChild(o);
    });
    sel.appendChild(og2);
  }
  var isPreset = selName && selName.charAt(0) === '!';
  var known = (isPreset && PRESETS.some(function (p) { return '!:' + p.n === selName; })) || !!profiles[selName];
  sel.value = known ? selName : '';
  $('profDel').disabled = !known || isPreset; // built-ins can't be deleted
}
$('profSave').addEventListener('click', function () {
  var name = prompt('Profile name:');
  name = name && name.trim();
  if (!name) return;
  // as requested: profiles store aggro 2 and 0.5 s no-beat grid regardless of the live values
  profiles[name] = Object.assign({}, P, { aggro: 2, gridSec: 0.5 });
  saveProfiles();
  refreshProfSel(name);
});
$('profSel').addEventListener('change', function () {
  var name = this.value;
  if (!name) return;
  if (name.charAt(0) === '!') {
    // built-in: fresh tune over the defaults
    for (var i2 = 0; i2 < PRESETS.length; i2++) if ('!:' + PRESETS[i2].n === name) { P = Object.assign({}, DEFAULTS, PRESETS[i2].P); break; }
  } else {
    if (!profiles[name]) return;
    P = Object.assign({}, profiles[name]);
  }
  refreshProfSel(name);
  applyParams();
});
$('profDel').addEventListener('click', function () {
  var name = $('profSel').value;
  if (!name || name.charAt(0) === '!') return;
  delete profiles[name];
  saveProfiles();
  refreshProfSel();
});
refreshProfSel();

function applyParams(skipId) {
  SCHEMA.forEach(function (f) {
    var inp = $('p_' + f.k);
    if (inp.id === skipId && document.activeElement === inp) return; // don't clobber fields being typed
    if (f.type === 'check') inp.checked = !!P[f.k];
    else inp.value = P[f.k] ?? '';
  });
  stages.forEach(function (s) { s.style.transition = 'opacity ' + P.fadeMs + 'ms linear'; });
  saveP();
  if (!$('stage').hidden && cuts.length) rebuildSchedule();
}

// Apply new params to a running slideshow: rebuild the schedule, keep pace with the song.
function rebuildSchedule() {
  var ct = audio.currentTime;
  if (tl) { cuts = tlCuts(); cutFades = cuts.map(function () { return P.fadeMs; }); }
  else if (beats && beats.length > 1) cuts = buildCuts(beats, energy, +audio.duration.toFixed(2));
  else cuts = fallbackGrid(+audio.duration.toFixed(2));
  cutIdx = 0;
  var next = -1;
  for (var c = cuts.length - 1; c >= 0; c--) if (cuts[c] <= ct) { next = c; break; }
  cutIdx = next + 1;
  waitingSwap = false; // any in-flight clip swap still lands; pending is rebuilt on demand
  pending = null;
}

function openPanel() { $('panel').classList.add('open'); }
function closePanel() { $('panel').classList.remove('open'); }
$('gear').addEventListener('click', openPanel);
$('gear2').addEventListener('click', openPanel);
$('closePanel').addEventListener('click', closePanel);
$('panel').addEventListener('click', function (e) { if (e.target === this) closePanel(); });
$('reset').addEventListener('click', function () {
  P = Object.assign({}, DEFAULTS);
  try { localStorage.removeItem('vs-params'); } catch (e) { /* no-op */ }
  applyParams();
});

// Active pool: full list, or clips passing every enabled filter. Clips without a
// readable timestamp are always included by the time filters.
function poolFor() {
  var wins = parseWindows(P.skipWindows);
  var tow = towOn && towActive.length;
  var dayF = dayOn && daySel;
  var junk = P.skipJunk && vids.some(function (v) { return (v.dk ?? 0) >= 0.5; });
  if (!tow && !dayF && scoreMin === null && arT === null && mvT === null && !junk && !wins.length) return vids;
  return vids.filter(function (v) {
    if (P.skipJunk && (v.dk ?? 0) >= 0.5) return false; // mostly-dark junk: black flash source
    if (inSkipWindows(v.day, wins)) return false;
    // "only this day" is exclusive: a clip with no readable stamp is not from it
    if (dayF && v.day !== daySel) return false;
    if (scoreMin !== null && !(v.score >= scoreMin)) return false;
    if (mvT !== null && !((v.mv ?? 0) >= mvT)) return false; // fast-motion filter
    // ratio filter: clips without readable dimensions never match it
    if (arT !== null && !(v.ar && Math.abs(v.ar - arT) / arT < 0.06)) return false;
    if (v.tod === null) return true; // clips without a readable stamp always pass
    if (!tow) return true;
    return towActive.some(function (r) {
      return r.wrap ? (v.tod >= r.a || v.tod < r.b) : (v.tod >= r.a && v.tod < r.b);
    });
  });
}

// Top-% cutoff: score at the rank boundary (e.g. top 50% of scores ≥ that score).
function computeScoreMin() {
  var pct = Math.max(5, Math.min(100, +$('scorePct').value || 50));
  var sorted = vids.map(function (v) { return v.score; }).sort(function (a, b) { return b - a; });
  scoreMin = sorted[Math.max(0, Math.ceil(sorted.length * pct / 100) - 1)];
}
function refreshScore() {
  if (!$('scoreOnly').checked) { scoreMin = null; refreshCount(); scheduleCount(); return; }
  computeScoreMin();
  refreshCount(); scheduleCount();
}

function refreshCount() {
  var n = poolFor().length;
  $('fcount').textContent = '(' + n + ' of ' + vids.length + ')';
}
// The server keeps scoring in the background, so its list is fresher than the
// page's: whenever a filter moves, re-fetch and recompute the percentiles.
function refetchVids() {
  return fetch('/videos').then(function (r) { return r.json(); }).then(function (list) { vids = list; }).catch(function () { /* keep the list we have */ });
}
var countTimer = 0;
function scheduleCount() {
  clearTimeout(countTimer);
  countTimer = setTimeout(function () {
    refetchVids().then(function () {
      if ($('scoreOnly').checked) computeScoreMin();
      if ($('mvOnly').checked) computeMvT();
      refreshCount();
    });
  }, 150);
}

fetch('/videos').then(function (r) { return r.json(); }).then(function (list) {
  vids = list;
  $('nvids').textContent = vids.length + ' videos indexed';
  var timed = vids.some(function (v) { return v.tod !== null; });
  if (timed) { $('towRow').style.display = ''; restoreTow(); }
  if (vids.some(function (v) { return v.day !== null; })) { $('dayRow').style.display = ''; restoreDay(); }
  if (vids.some(function (v) { return v.ar; })) $('arRow').style.display = '';
  $('scoreRow').style.display = '';
  if (vids.some(function (v) { return (v.mv ?? 0) > 0; })) $('mvRow').style.display = '';
  refreshCount();
}).catch(function () { $('nvids').textContent = 'failed to load video list'; });

// Live scoring progress: thin bar under the video count; also keeps the stats
// and percentiles fresh while the indexer works in the background.
(function pollScore() {
  fetch('/scoreProgress').then(function (r) { return r.json(); }).then(function (j) {
    if (j.total > 0 && j.done < j.total) {
      var p = Math.round(100 * j.done / j.total);
      $('nvids').textContent = vids.length + ' videos indexed — scoring ' + p + '% (' + j.done + '/' + j.total + ')';
      $('sprogWrap').hidden = false;
      $('sprog').style.width = p + '%';
      $('sprogText').textContent = p + '%';
      scheduleCount();
      setTimeout(pollScore, 2000);
    } else {
      $('nvids').textContent = vids.length + ' videos indexed — fully scored';
      $('sprogWrap').hidden = true;
      refetchVids().then(function () {
        if (vids.some(function (v) { return (v.mv ?? 0) > 0; })) $('mvRow').style.display = '';
        refreshCount();
      });
    }
  }).catch(function () { setTimeout(pollScore, 4000); });
})();

// Time-window filter (list of ranges): each range is native time inputs;
// a range with from ≥ to crosses midnight. Persisted in localStorage['vs-tow'].
var towRanges = []; // [{f: 'HH:MM', t: 'HH:MM'}] — mirrors the DOM rows
function parseT(v) { return v && v.length === 5 ? +v.slice(0, 2) + (+v.slice(3, 5) || 0) / 60 : null; }
function refreshTow() {
  towOn = $('towOn').checked;
  towRanges = [].map.call(document.querySelectorAll('#towList .trow'), function (r) {
    return { f: r.querySelector('.twF').value, t: r.querySelector('.twT').value };
  });
  towActive = [];
  towRanges.forEach(function (r) {
    var a = parseT(r.f), b = parseT(r.t);
    if (a !== null && b !== null) towActive.push({ a: a, b: b, wrap: a >= b });
  });
  try { localStorage.setItem('vs-tow', JSON.stringify({ on: towOn, l: towRanges })); } catch (e) { /* storage unavailable */ }
  refreshCount();
}
function towRowEl(r, i) {
  var d = document.createElement('div');
  d.className = 'trow';
  var a = document.createElement('input');
  a.type = 'time'; a.value = r.f; a.className = 'twF';
  var b = document.createElement('input');
  b.type = 'time'; b.value = r.t; b.className = 'twT';
  var hint = document.createElement('span');
  hint.className = 'towHint';
  var sync = function () { hint.textContent = parseT(a.value) >= parseT(b.value) ? '(crosses midnight)' : ''; refreshTow(); };
  a.addEventListener('input', sync);
  a.addEventListener('change', sync);
  b.addEventListener('input', sync);
  b.addEventListener('change', sync);
  var x = document.createElement('button');
  x.type = 'button'; x.className = 'rm'; x.textContent = '✕'; x.title = 'Remove this range';
  x.addEventListener('click', function () {
    towRanges.splice(i, 1);
    renderTow();
    refreshTow();
  });
  d.appendChild(a);
  d.appendChild(document.createTextNode('–'));
  d.appendChild(b);
  d.appendChild(hint);
  d.appendChild(x);
  return d;
}
function renderTow() {
  var host = $('towList');
  host.innerHTML = '';
  towRanges.forEach(function (r, i) { host.appendChild(towRowEl(r, i)); });
}
function restoreTow() {
  try {
    var s = JSON.parse(localStorage.getItem('vs-tow') || 'null');
    if (s) { towOn = !!s.on; towRanges = Array.isArray(s.l) && s.l.length ? s.l.map(function (r) { return { f: r.f || '08:00', t: r.t || '20:00' }; }) : [{ f: '08:00', t: '20:00' }]; }
  } catch (e) { towRanges = [{ f: '08:00', t: '20:00' }]; }
  if (!towRanges.length) towRanges = [{ f: '08:00', t: '20:00' }];
  $('towOn').checked = towOn;
  renderTow();
  refreshTow();
}

// Day filter: one native date input + master toggle; persisted in localStorage['vs-day'].
function refreshDay() {
  dayOn = $('dayOnly').checked;
  daySel = $('daySel').value;
  try { localStorage.setItem('vs-day', JSON.stringify({ on: dayOn, d: daySel })); } catch (e) { /* storage unavailable */ }
  refreshCount();
}
function restoreDay() {
  var s = null;
  try { s = JSON.parse(localStorage.getItem('vs-day') || 'null'); } catch (e) { /* storage unavailable */ }
  if (!s || !s.d) {
    // prefill with the day that has the most clips so the toggle is instantly usable
    var counts = {};
    vids.forEach(function (v) { if (v.day) counts[v.day] = (counts[v.day] || 0) + 1; });
    var best = '', bn = 0;
    for (var k in counts) if (counts[k] > bn) { bn = counts[k]; best = k; }
    s = { on: false, d: best };
  }
  dayOn = !!s.on;
  daySel = s.d || '';
  $('dayOnly').checked = dayOn;
  $('daySel').value = daySel;
  $('dayOnly').addEventListener('change', refreshDay);
  $('daySel').addEventListener('input', refreshDay);
  $('daySel').addEventListener('change', refreshDay);
  refreshCount();
}
$('towOn').addEventListener('change', refreshTow);
$('towAdd').addEventListener('click', function () {
  towRanges.push({ f: '08:00', t: '20:00' });
  renderTow();
  refreshTow();
});
function refreshAr() { arT = $('arOnly').checked ? +$('arSel').value : null; refreshCount(); }
$('arOnly').addEventListener('change', refreshAr);
$('arSel').addEventListener('change', refreshAr);
$('scoreOnly').addEventListener('change', refreshScore);
$('scorePct').addEventListener('change', refreshScore);

// Fast-motion filter: top-% of the mv metric (moving-vehicle detector); clips
// without a computed mv (0) never qualify.
function computeMvT() {
  var pct = Math.max(10, Math.min(100, +$('mvPct').value || 50));
  var sorted = vids.map(function (v) { return v.mv ?? 0; }).sort(function (a, b) { return b - a; });
  mvT = sorted[Math.max(0, Math.ceil(sorted.length * pct / 100) - 1)];
}
function refreshMv() {
  if (!$('mvOnly').checked) { mvT = null; refreshCount(); scheduleCount(); return; }
  computeMvT();
  refreshCount(); scheduleCount();
}
$('mvOnly').addEventListener('change', refreshMv);
$('mvPct').addEventListener('change', refreshMv);
$('arOnly').addEventListener('change', refreshAr);
$('arSel').addEventListener('change', refreshAr);

$('file').addEventListener('change', function (e) {
  file = e.target.files[0] || null;
  $('analyze').disabled = !file;
});

$('analyze').addEventListener('click', async function () {
  $('start').disabled = true;
  $('result').hidden = true;
  $('status').textContent = 'Analyzing…';
  try {
    var res = await fetch('/analyze', { method: 'POST', body: file });
    var j = await res.json().catch(function () { return null; });
    if (!res.ok || !j || !j.beats) {
      $('status').textContent = (j && j.error) || ('error ' + res.status);
      return;
    }
    beats = j.beats; bpm = j.bpm; energy = j.energy || [];
    beatPattern = j.beatPattern || { offsets: [], at: [] };
    moodData = { bright: j.bright || [], sections: j.sections || [], mood: j.mood || 'mixed' };
    $('status').textContent = '';
    $('summary').textContent = (bpm ? bpm + ' bpm, ' : '') + beats.length + ' beats' + (P.mood && moodData.sections.length ? ', ' + moodData.mood : '');
    $('result').hidden = false;
    $('start').disabled = false;
  } catch (err) {
    $('status').textContent = 'error: ' + err;
  }
});

// --- slideshow ---
var audio = null, cuts = [], cutIdx = 0, visible = 0, currentId = -1, rafId = 0;
var cutFades = []; // per-cut crossfade ms (mood-driven), parallel to cuts
var cutPick = []; // per-cut clip id override (-1 = walk): the drop holds its best clip
var stages = [$('v0'), $('v1')];
var startUrl = '';
applyParams();

// One clip per cut; hold = how many beats one clip spans: every beat at high
// energy, every 2–3 beats when the song is quiet. Inside held segments the
// song's own recurring stutter pattern (detected off-beat hits) is replicated
// beat-accurately; slow→loud ramps still get a taste split; gated by tempo and caps.
function buildCuts(bts, en, duration, bright, secs) {
  bright = bright || moodData.bright;
  secs = secs || moodData.sections;
  var cuts = [], ce = [], ci = [];
  var hi = Math.max(P.energyHi, P.energyLo + 0.05), lo = Math.min(P.energyLo, P.energyHi - 0.05);
  var secIdxAt = function (t) {
    for (var s3 = 0; s3 < secs.length; s3++) {
      if (t >= secs[s3].s && (s3 + 1 >= secs.length || t < secs[s3 + 1].s)) return s3;
    }
    return -1;
  };
  function fadeFor(t) {
    if (!P.mood) return P.fadeMs;
    var si5 = secIdxAt(t);
    var kind4 = si5 >= 0 ? secs[si5].kind : 'mid';
    if (kind4 === 'loud' || kind4 === 'drop') return 0;
    if (kind4 === 'build') return Math.round(P.fadeMs * 0.5);
    return P.fadeMs;
  }
  // drop statements: the drop section opens on one long, best-scored clip
  var dropT = {}, dropBest = [], dropRank = 0, cutPick2 = [];
  var dropIdx = {}; // drop-open beat index → exact walk landing
  if (P.mood && secs.length) {
    secs.forEach(function (x) {
      if (x.kind === 'drop') { dropT[x.s] = 1; if (x.bi !== undefined) dropIdx[x.bi] = 1; }
    });
    if (Object.keys(dropT).length)
      dropBest = poolFor().filter(function (x) { return (P.skipJunk ? (x.dk ?? 0) < 0.5 : true) && x.score >= 8; })
        .sort(function (a3, b3) { return b3.score - a3.score; }).slice(0, 12).map(function (x) { return x.id; });
  }
  // aggressivity scales holds across the board (and eases the aggressive-section
  // threshold): 0 = everything long, 1 = the energy mapping as tuned, 2 = dense
  var ag = Math.max(0, Math.min(2, P.aggro));
  var hiTh = Math.max(P.aggrFloor || 0.6, Math.min(1, P.aggrThreshold + (1 - ag) * 0.1));
  var MINCUT = 0.3; // never place cuts closer than this: shorter slices read as flash strobes
  var i = 0;
  while (i < bts.length && bts[i] < duration - 0.05) {
    if (cuts.length && bts[i] - cuts[cuts.length - 1] < MINCUT) { i++; continue; } // chatter half-hits: not a cut position
    var e = en.length === bts.length ? en[i] : 0.5;
    ce.push(e);
    cuts.push(bts[i]);
    ci.push(i);
    var f = e >= hi ? 0 : e <= lo ? 1 : (hi - e) / (hi - lo);
    var hold = Math.round(1 + f * (P.holdMax - 1));
    if (ag < 1) hold = Math.round(hold * (1 + (1 - ag) * 0.8));      // pull toward long holds
    else if (ag > 1) hold = Math.max(1, Math.round(hold / (1 + (ag - 1) * 1.5))); // pull toward every beat
    if (P.mood) {
      // intensity-coupled holds: chill lingers, loud speeds up, builds accelerate
      // into the drop, and the drop opener holds its most interesting clip long
      var siH = secIdxAt(bts[i]);
      if (siH >= 0) {
        var secH = secs[siH];
        var endH = siH + 1 < secs.length ? secs[siH + 1].s : duration;
        if (secH.kind === 'chill') hold = Math.min(P.holdMax, Math.round(hold * 1.35));
        else if (secH.kind === 'loud') hold = Math.max(1, Math.round(hold * 0.6));
        else if (secH.kind === 'build') {
          var pos3 = Math.max(0, Math.min(1, (bts[i] - secH.s) / Math.max(0.05, endH - secH.s)));
          hold = Math.max(1, Math.round(hold * (1.7 - 1.5 * pos3))); // accelerate toward the drop
        } else if (secH.kind === 'drop') {
          var nxt2 = (bts[i + 1] !== undefined ? bts[i + 1] - bts[i] : 0.5);
          // the drop is the statement moment: it may hold up to twice holdMax
          hold = Math.max(hold, Math.min(P.holdMax * 2, Math.round((endH - bts[i]) / Math.max(0.05, nxt2))));
        }
      }
    }
    var dropHere = dropT[bts[i]] === 1 && dropBest.length;
    cutPick2.push(dropHere ? dropBest[dropRank++ % dropBest.length] : -1);
    // land the walk exactly on the next drop's opening beat (hold steps skip it otherwise)
    var i2 = i + hold >= bts.length ? bts.length : i + hold;
    if (Object.keys(dropIdx).length) {
      var dbest = Infinity;
      for (var k4 in dropIdx) { var b4 = +k4; if (b4 > i && b4 < i2 && b4 < dbest) dbest = b4; }
      if (dbest !== Infinity) i2 = dbest;
    }
    if (i2 >= bts.length) i = bts.length;
    else i = i2;
  }
  if (!cuts.length) { cuts.push(0); cutPick2.push(-1); }

  var at = {};
  (beatPattern.at || []).forEach(function (b) { at[b] = 1; });
  var offs = (P.replicate ? (beatPattern.offsets || []) : []).slice();
  if (offs.length) {
    offs = [...new Set(offs)].filter(function (o) { return o > 0.04 && o < 0.96; }).sort(function (a, b) { return a - b; });
  }
  var out = [], lastStutter = -1e9, count = 0, insTotal = 0, insMax = P.insMax || 48;
  var insSet = {}; // inserted micro-cut times get hard cuts (except in chill sections)
  for (var k = 0; k < cuts.length; k++) {
    var t0 = cuts[k], segEnd = k + 1 < cuts.length ? cuts[k + 1] : duration;
    out.push(t0);
    if (k + 1 >= cuts.length && segEnd <= t0) continue;
    // pattern cuts: micro cut at the song's own off-beat hit positions inside this segment
    var inserted = [];
    if (offs.length) {
      var jMax = k + 1 < cuts.length ? ci[k + 1] : bts.length - 1;
      for (var j = ci[k]; j <= jMax; j++) {
        if (!at[j]) continue;
        var jEnd = j + 1 < bts.length ? bts[j + 1] : duration;
        if (jEnd <= bts[j] || jEnd >= segEnd + 1e-9) continue;
        for (var q2 = 0; q2 < offs.length; q2++) {
          var pt = bts[j] + offs[q2] * (jEnd - bts[j]);
          if (pt > t0 + 0.08 && pt < segEnd - 0.08 && inserted.indexOf(pt) < 0 && insTotal < insMax) {
            inserted.push(pt);
            insSet[pt] = 1;
            insTotal++;
          }
        }
      }
    }
    // aggressive sections: split each strong beat into half- or quarter-beat cuts.
    // Run length scans the global beat list — segments are per-beat at high energy,
    // so a local scan would never see the run. No bpm cap: fast tracks need this
    // path for their density; only the taste stutter below is tempo-gated.
    if (k + 1 < cuts.length && insTotal < insMax && !dropT[t0]) {
      var b0 = ci[k], b1 = ci[k + 1];
      for (var jg = b0; jg < b1 && insTotal < insMax; jg++) {
        if (!((en.length === bts.length ? en[jg] : 0.5) >= hiTh)) continue;
        var runLen = 1, back = jg - 1;
        while (back >= 0 && (en.length === bts.length ? en[back] : 0.5) >= hiTh) { runLen++; back--; }
        var fwd = jg + 1;
        while (fwd < bts.length && (en.length === bts.length ? en[fwd] : 0.5) >= hiTh) { runLen++; fwd++; }
        var jgEnd = jg + 1 < bts.length ? bts[jg + 1] : duration;
        var int2 = jgEnd - bts[jg];
        var of2 = runLen >= P.aggrQuarterRun ? [0.25, 0.5, 0.75] : runLen >= P.aggrHalfRun ? [0.5] : [];
        for (var og = 0; og < of2.length; og++) {
          var pt2 = bts[jg] + of2[og] * int2;
          if (pt2 > t0 + 0.08 && pt2 < segEnd - 0.08 && inserted.indexOf(pt2) < 0) { inserted.push(pt2); insSet[pt2] = 1; insTotal++; }
        }
      }
    }
    // otherwise taste split on slow→loud ramps and strong accents
    if (!inserted.length && k + 1 < cuts.length && !dropT[t0]) {
      var gap = segEnd - t0;
      if (P.stutter && bpm < P.stutterMaxBpm && gap >= P.stutterMinHold && count < P.stutterMax &&
          segEnd - lastStutter > P.stutterGap && (ce[k + 1] - ce[k] >= P.stutterRamp || ce[k + 1] >= P.stutterAccent)) {
        for (var q3 = 1; q3 < P.split; q3++) { var pst = t0 + gap * q3 / P.split; inserted.push(pst); insSet[pst] = 1; }
        lastStutter = segEnd;
        count++;
      }
    }
    inserted.sort(function (a, b) { return a - b; });
    // flash guard: a slice shorter than ~9 frames reads as a strobe — none allowed,
    // no matter the insert source (pattern micro-cuts, aggressive quarters, stutters)
    var MINSLICE = 0.3;
    var dedup = [];
    for (var ii = 0; ii < inserted.length; ii++) {
      var lastIns = dedup.length ? dedup[dedup.length - 1] : t0;
      if (inserted[ii] - lastIns >= MINSLICE && segEnd - inserted[ii] >= MINSLICE) dedup.push(inserted[ii]);
    }
    inserted = dedup;
    out = out.concat(inserted);
  }
  // per-cut crossfade (mood-driven): drops/loud = hard instant cuts, chill = long soft
  // dissolves, build = shorter than base; inserted micro-cuts are hard except in chill.
  cutPick = cutPick2;
  cutFades = out.map(function (t) {
    if (P.mood && secIdxAt(t) >= 0 && secs[secIdxAt(t)].kind === 'chill')
      return Math.min(400, Math.max(150, Math.round(P.fadeMs * 2.5)));
    if (!P.mood) return P.fadeMs;
    if (insSet[t]) return 0;
    return fadeFor(t);
  });
  return out;
}

function enterStage() {
  $('form').hidden = true;
  $('stage').hidden = false;
  showCtl();
  if (startUrl) URL.revokeObjectURL(startUrl);
  startUrl = URL.createObjectURL(file);
  audio = new Audio(startUrl);
  audio.addEventListener('ended', exitStage);
  audio.addEventListener('loadedmetadata', function () {
    if (tl) { cuts = tlCuts(); cutFades = cuts.map(function () { return P.fadeMs; }); }
    else if (beats && beats.length > 1) cuts = buildCuts(beats, energy, audio.duration);
    else cuts = fallbackGrid(audio.duration);
    if (!beats || beats.length <= 1)
      $('summary').textContent = 'no beats detected — cutting every 0.5 s';
    else
      $('summary').textContent = (bpm ? bpm + ' bpm, ' : '') + beats.length + ' beats, ' + cuts.length + ' cuts' + (P.mood && moodData.sections.length ? ', ' + moodData.mood : '');
    cutIdx = 0; visible = 0; currentId = -1; waitingSwap = false; orderPos = -1;
    reseed();
    // Wait for the first clip's frame before starting the song, so the show never opens on black.
    waitingSwap = true;
    cut(cutIdx++, true);
  });
}

function fallbackGrid(duration) {
  var g = [];
  if (isFinite(duration) && duration > 0)
    for (var t = 0; t < duration; t += P.gridSec) g.push(Math.round(t * 1000) / 1000);
  if (!g.length) g.push(0);
  return g;
}

// ---- NLE: edited timeline ----
// tl = null → the classic generated schedule. When set, segments tile the song:
// each { id, s, len } plays clip id from seek s for len seconds of song time.
// Invariant: the last segment ends at audio duration (edits ripple into the last
// segment); cuts are prefix sums, so playback code needs no changes beyond
// choosing tl[idx] instead of walking the pool.
var tl = null, tlSel = -1, tlEdits = 0;
var TL_MIN = 0.05, TL_STEP = 0.25;
function tlRng() { return RNG ? RNG() : Math.random(); }
function dur2seek(v, len, r) {
  var d2 = (P.useLrf && v.lrfDur) ? v.lrfDur : v.duration;
  return r * Math.max(0, d2 - len - 0.1);
}
function tlCuts() {
  var c = [], at = 0;
  for (var i = 0; i < tl.length; i++) { c.push(Math.round(at * 1000) / 1000); at += tl[i].len; }
  return c;
}
function materializeTl() {
  if (!audio || !isFinite(audio.duration)) return null;
  var dur = audio.duration;
  var cc = beats && beats.length > 1 ? buildCuts(beats, energy, dur) : fallbackGrid(dur);
  if (cc.length < 2) return null;
  var base = poolFor().slice().sort(chronoCmp);
  if (!base.length) base = vids.slice().sort(chronoCmp);
  if (P.shuffle) shuffleFor(base); // breaks chronology on purpose; fixed seed keeps the run reproducible
  var segs = cc.map(function (t, i) {
    return { id: -1, s: 0, len: Math.max(TL_MIN, (cc[i + 1] !== undefined ? cc[i + 1] : dur) - t) };
  });
  var pos = 0;
  for (var i = 0; i < segs.length; i++) {
    var v = base[pos % base.length];
    // near-black clips score ~0: roll on, deterministically (matches play-time filter)
    if (v.score < 8 && pos <= base.length * 2) { pos++; i--; continue; }
    segs[i].id = v.id;
    segs[i].s = dur2seek(v, segs[i].len, tlRng());
    pos++;
  }
  return segs;
}
function tlPick(k) {
  var pool = poolFor(), bad = {};
  bad[tl[k].id] = 1;
  if (tl[k - 1]) bad[tl[k - 1].id] = 1;
  if (tl[k + 1]) bad[tl[k + 1].id] = 1;
  var rnd = Math.random;
  for (var tr = 0; tr < 10; tr++) {
    var v = pool[Math.floor(rnd() * pool.length)];
    if (v && v.score >= 8 && !bad[v.id]) return v;
  }
  var rest = pool.filter(function (x) { return x.id !== tl[k].id && x.score >= 8; });
  return rest[Math.floor(rnd() * rest.length)] || pool[0];
}
function renderTl() {
  renderStrip();
  var k = tlSel;
  if (k < 0 || k >= tl.length) { tlSel = k = -1; }
  var sel = $('esel');
  if (k < 0) {
    sel.hidden = true;
  } else {
    sel.hidden = false;
    var s2 = tl[k], v = vids[s2.id];
    var src = v ? (v.day ? v.day + (v.tod !== null ? ' ' + fmtTod(v.tod) + ' utc' : '') : 'no stamp') : 'missing';
    $('einfo').textContent = '#' + (k + 1) + ' of ' + tl.length + ' · ' + src +
      ' · ' + s2.len.toFixed(2) + ' s · in-point ' + s2.s.toFixed(2) + ' s';
  }
  $('estats').textContent = tl.length + ' segments · ' + tlEdits + ' edits';
}
function renderStrip() {
  var pps = 24, host = $('estrip'), h = [];
  for (var i = 0; i < tl.length; i++) {
    var s2 = tl[i];
    h.push('<div class="ecel' + (i === tlSel ? ' sel' : '') + '" data-i="' + i + '" title="#' + (i + 1) +
      ' clip ' + s2.id + ' ' + s2.len.toFixed(2) + 's in @' + s2.s.toFixed(2) + 's" style="width:' +
      Math.max(7, s2.len * pps).toFixed(1) + 'px;background:' + ECOLS[s2.id % ECOLS.length] + '"></div>');
  }
  host.innerHTML = h.join('');
}
var ECOLS = ['#2a4d69', '#4b5d54', '#5d4037', '#455a64', '#5e4b6b', '#37524f', '#6b4b3f', '#544a6b'];

// ---- NLE edit operations ----
function tlShort() {
  var k = tlSel;
  if (k === tl.length - 1) tl[k].len = Math.max(TL_MIN, tl[k].len - TL_STEP);
  else { var d2 = Math.min(TL_STEP, tl[k].len - TL_MIN); tl[k].len -= d2; tl[tl.length - 1].len += d2; }
}
function tlLong() {
  var k = tlSel;
  if (k === tl.length - 1) tl[k].len += TL_STEP;
  else { var d2 = Math.min(TL_STEP, tl[tl.length - 1].len - TL_MIN); tl[k].len += d2; tl[tl.length - 1].len -= d2; }
}
function tlChop() {
  var k = tlSel, half = tl[k].len / 2;
  var v = tlPick(k);
  tl.splice(k + 1, 0, { id: v.id, s: dur2seek(v, half, Math.random()), len: tl[k].len - half });
  tl[k].len = half;
}
function tlDelete() {
  if (tl.length <= 2) return;
  var k = tlSel, ln = tl[k].len;
  tl.splice(k, 1);
  tl[tl.length - 1].len += ln;
  tlSel = Math.min(k, tl.length - 1);
}
$('estrip').addEventListener('click', function (e) {
  var c = e.target.closest ? e.target.closest('.ecel') : null;
  if (!c) return;
  tlSel = +c.dataset.i;
  renderTl();
});
$('oShort').addEventListener('click', function () { if (tlSel >= 0) { tlShort(); tlEdits++; renderTl(); } });
$('oLong').addEventListener('click', function () { if (tlSel >= 0) { tlLong(); tlEdits++; renderTl(); } });
$('oChop').addEventListener('click', function () { if (tlSel >= 0) { tlChop(); tlEdits++; renderTl(); } });
$('oSeek').addEventListener('click', function () {
  if (tlSel >= 0) { var s2 = tl[tlSel]; s2.s = dur2seek(vids[s2.id], s2.len, Math.random()); tlEdits++; renderTl(); }
});
$('oSwap').addEventListener('click', function () {
  if (tlSel >= 0) { var s2 = tl[tlSel], v = tlPick(tlSel); s2.id = v.id; s2.s = dur2seek(v, s2.len, Math.random()); tlEdits++; renderTl(); }
});
$('oDel').addEventListener('click', function () { if (tlSel >= 0) { tlDelete(); tlEdits++; renderTl(); } });
$('eSeek').addEventListener('click', function () {
  if (!tl) return;
  for (var i = 0; i < tl.length; i++) tl[i].s = dur2seek(vids[tl[i].id], tl[i].len, tlRng());
  tlEdits++; renderTl();
});
$('eRefit').addEventListener('click', function () { tl = materializeTl(); tlSel = -1; tlEdits = 0; renderTl(); });
// Reshuffle: regenerate the whole edit under a different seed — new in-points and
// new junk-rolls everywhere, segment structure (cut times) untouched. Same song +
// same seed still reproduces exactly this edit later.
$('eShuffle').addEventListener('click', function () {
  if (!tl) return;
  if (P.seed) P.seed = (P.seed % 99999) + 1; // seeded mode: walk forward; seed 0 stays on shuffle
  $('p_seed').value = String(P.seed);
  saveP();
  reseed();
  tl = materializeTl();
  tlSel = -1; tlEdits = 0;
  renderTl();
});
$('eDone').addEventListener('click', function () { $('edit').hidden = true; $('form').hidden = false; });

// ---- Editor entry: probe the song, materialize (or keep) the edited timeline ----
function buildEdit() {
  $('form').hidden = true;
  $('edit').hidden = false;
  if (!audio) {
    startUrl = URL.createObjectURL(file);
    audio = new Audio(startUrl);
    audio.addEventListener('ended', exitStage);
  }
  var go = function () {
    if (!tl) { reseed(); tl = materializeTl(); tlEdits = 0; tlSel = -1; }
    if (!tl) { $('estats').textContent = 'not enough material to build a timeline'; return; }
    renderTl();
  };
  if (audio.duration && isFinite(audio.duration)) go();
  else { $('estats').textContent = 'loading song…'; audio.addEventListener('loadedmetadata', go, { once: true }); }
}
$('editBtn').addEventListener('click', buildEdit);
$('ePlay').addEventListener('click', function () {
  $('edit').hidden = true;
  $('stage').hidden = false;
  showCtl();
  cuts = tlCuts();
  cutIdx = 0; visible = 0; currentId = -1; waitingSwap = false; orderPos = -1;
  if (cuts.length) { waitingSwap = true; cut(cutIdx++, true); }
});

// ---- Player bar: pause/resume, seek (seek re-syncs the cut schedule), keyboard ----
var scrubbing = false;
function fmtT(s) {
  s = Math.max(0, s || 0);
  return Math.floor(s / 60) + ':' + String(Math.floor(s % 60)).padStart(2, '0');
}
function fmtTod(h) { // hour-of-day float (UTC stamp) -> "18:03"
  var hh = Math.floor(h), mm = Math.round((h - hh) * 60);
  if (mm >= 60) { hh = (hh + 1) % 24; mm = 0; }
  return String(hh).padStart(2, '0') + ':' + String(mm).padStart(2, '0');
}
var ctlTimer = 0;
function showCtl() {
  $('ctl').classList.remove('off');
  clearTimeout(ctlTimer);
  ctlTimer = setTimeout(function () { $('ctl').classList.add('off'); }, 2500);
}
function togglePlay() {
  if (!audio) return;
  if (!audio.paused) {
    audio.pause();
    stages[visible].pause();
    $('pp').textContent = '▶';
  } else {
    audio.play();
    if (stages[visible].readyState >= 2) stages[visible].play().catch(function () {});
    $('pp').textContent = '⏸';
  }
  showCtl();
}
$('pp').addEventListener('click', togglePlay);
$('stage').addEventListener('click', function (e) {
  if (e.target === this || e.target.tagName === 'VIDEO') togglePlay();
});
$('tbar').addEventListener('input', function () {
  scrubbing = true;
  if (audio) $('tcur').textContent = fmtT(this.value / 1000 * audio.duration);
  showCtl();
});
$('tbar').addEventListener('change', function () {
  scrubbing = false;
  if (!audio) return;
  audio.currentTime = this.value / 1000 * audio.duration;
  rebuildSchedule();
  showCtl();
});
document.addEventListener('keydown', function (e) {
  if (!audio || !$('edit').hidden || $('panel').classList.contains('open')) return;
  if (e.code === 'Space') { e.preventDefault(); togglePlay(); }
  else if (e.code === 'ArrowRight') { audio.currentTime = Math.min(audio.duration || 0, audio.currentTime + 5); rebuildSchedule(); showCtl(); }
  else if (e.code === 'ArrowLeft') { audio.currentTime = Math.max(0, audio.currentTime - 5); rebuildSchedule(); showCtl(); }
});
$('stage').addEventListener('mousemove', showCtl);

var lastVisT = -1, stallSince = 0, lastVisCheck = 0, lastFix = 0;

function loop() {
  if ($('stage').hidden || audio.ended) return;
  if (!scrubbing) {
    var dur = audio.duration;
    $('tbar').value = String(dur && isFinite(dur) ? Math.round(audio.currentTime / dur * 1000) : 0);
    $('tcur').textContent = fmtT(audio.currentTime);
    $('tdur').textContent = fmtT(dur || 0);
    // buffered overlay (debug): map the visible clip's buffered ranges (its own
    // timeline) 1:1 onto the song bar — amber band = video buffered ahead here
    var tb = $('tbuf'), vw = stages[visible];
    if (tb && vw && vw.readyState > 0 && dur && isFinite(dur)) {
      var segStart = cutIdx > 0 && cuts[cutIdx - 1] !== undefined ? cuts[cutIdx - 1] : 0;
      var segEnd = cuts[cutIdx] !== undefined ? cuts[cutIdx] : dur;
      var vstart = +vw.dataset.start || 0;
      var stops = [];
      var pct = function (t) { return Math.min(100, Math.max(0, t / dur * 100)).toFixed(1); };
      var band = function (a, b, col) { if (b > a) stops.push(col + ' ' + pct(a) + '%,' + col + ' ' + pct(b) + '%'); };
      band(0, audio.currentTime, '#1f6feb');
      for (var bi = 0; bi < vw.buffered.length; bi++) {
        var bs = Math.max(segStart, segStart + vw.buffered.start(bi) - vstart);
        var be = Math.min(segEnd, segStart + vw.buffered.end(bi) - vstart);
        band(Math.max(bs, audio.currentTime), be, 'rgba(255,170,60,.55)');
      }
      tb.style.backgroundImage = stops.length ? 'linear-gradient(to right,' + stops.join(',') + ')' : 'none';
    }
  }
  // self-heal: the on-screen clip plays whenever the song plays — covers pause
  // races (swap resumed it) and clips whose decode wasn't ready at pause/resume
  if (!audio.paused) {
    if (stages[visible].paused && stages[visible].readyState >= 2) stages[visible].play().catch(function () {});
  } else if (!stages[visible].paused) stages[visible].pause();
  if (!waitingSwap && cutIdx < cuts.length && audio.currentTime >= cuts[cutIdx]) {
    waitingSwap = true;
    cut(cutIdx++);
  } else if (waitingSwap && cutIdx < cuts.length && audio.currentTime >= cuts[cutIdx] + 0.4) {
    // incoming clip didn't make its cut: skip it instead of freezing the show on
    // the current (possibly dark) one — the next boundary reloads fresh
    waitingSwap = false;
  } else if (!waitingSwap && !audio.paused && cutIdx < cuts.length - 1 && P.reactive) {
    // on-screen frame went (near-)black while playing: buffer stall or the chunk
    // drifts into darkness — cut early to the preloaded next clip instead of sitting
    // black. Just after a swap the reaction is immediate (no flash may linger).
    var v = stages[visible];
    var hot = Date.now() - (+v.dataset.swapAt || 0) < 300;
    if (hot || Date.now() - lastReactive > P.reactiveCool * 1000) {
      if (isDark(v)) {
        darkSince = darkSince || Date.now();
        if (Date.now() - darkSince > (hot ? 80 : P.reactiveMs)) { darkSince = 0; lastReactive = Date.now(); waitingSwap = true; cut(cutIdx++); return; }
      } else {
        darkSince = 0;
      }
    }
  }
  // freeze watchdog: a playing clip whose time stops advancing (buffer under-
  // flow on a slow link) is rewound to its chunk start so the frame keeps moving
  var now = Date.now();
  if (now - lastVisCheck > 120) {
    lastVisCheck = now;
    var vis = stages[visible];
    if (!vis.paused && vis.readyState > 0) {
      var t2 = vis.currentTime;
      if (lastVisT >= 0 && t2 === lastVisT) {
        stallSince = stallSince || now;
        if (now - stallSince > P.reactiveMs && now - lastFix > P.reactiveCool * 1000) {
          lastFix = now;
          stallSince = 0;
          vis.currentTime = +vis.dataset.start || 0;
          vis.play().catch(function () {});
        }
      } else {
        stallSince = 0;
      }
      lastVisT = t2;
    }
  }
  rafId = requestAnimationFrame(loop);
}

// Preload/dark-filter state: the next beat's clip loads pre-seeked & paused on the
// currently hidden element, so the swap at the beat is instant from network/LAN storage.
var pending = null;
var cutSeq = 0; // increments per cut: stale abandoned loaders must not swap
var darkSince = 0, lastReactive = 0;
var orderPos = -1; // walk position through the sorted (or shuffled) pool
// Indie transitions applied to the incoming clip (colour-mix / crush / punch / ...),
// animated back to identity over the crossfade. Picked along the seeded stream.
var FXS = [
  { f: 'hue-rotate(130deg) saturate(2.4) contrast(1.5)', t: 'none' },        // colour mix
  { f: 'contrast(2.2) saturate(0.2) brightness(1.3)', t: 'none' },          // bitcrush look
  { f: 'blur(16px) saturate(1.7)', t: 'none' },                              // dream blur
  { f: 'none', t: 'scale(1.45)' },                                           // punch-in
  { f: 'none', t: 'translateX(36px)' },                                      // slide
  { f: 'invert(1) hue-rotate(160deg) saturate(2)', t: 'none' },              // negative flash
];
function shuffleFor(arr) { // Fisher-Yates on the edit's RNG stream: fixed seed ⇒ same shuffle
  for (var i = arr.length - 1; i > 0; i--) {
    var j = Math.floor((RNG ? RNG() : Math.random()) * (i + 1));
    var t = arr[i]; arr[i] = arr[j]; arr[j] = t;
  }
  return arr;
}

// ---- Edit seed: 0 = Math.random for every seek; a fixed seed reproduces the exact
// same edit for the same song (clip walk, cut times and seek stream are all
// deterministic). Re-seeded per slideshow start; mid-show seed changes apply then.
var RNG = null;
function reseed() {
  if (!P.seed) { RNG = null; return; }
  var s0 = P.seed >>> 0;
  RNG = function () { // mulberry32
    s0 = (s0 + 0x6D2B79F5) >>> 0;
    var z = s0;
    z = Math.imul(z ^ (z >>> 15), z | 1);
    z ^= z + Math.imul(z ^ (z >>> 7), z | 61);
    return ((z ^ (z >>> 14)) >>> 0) / 4294967296;
  };
}
// same-day clips grouped and in order, days chronological; unstamped clips go last
var chronoCmp = function (a, b) {
  var da = a.day || '9999-12-31', db = b.day || '9999-12-31';
  if (da !== db) return da < db ? -1 : 1;
  return (a.tod ?? 1e9) - (b.tod ?? 1e9) || a.id - b.id;
};

function isDark(el) {
  if (el.readyState < 2) return false; // no decoded frame yet — never judge (nor swap) by nothing
  try {
    var c = document.createElement('canvas');
    c.width = 64; c.height = 36;
    var ctx = c.getContext('2d');
    ctx.drawImage(el, 0, 0, c.width, c.height);
    var d = ctx.getImageData(0, 0, c.width, c.height).data;
    var sum = 0, bright = 0, n = d.length / 4;
    for (var i = 0; i < d.length; i += 4) {
      var luma = 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];
      sum += luma;
      if (luma >= 16) bright++;
    }
    return (sum / n) < P.darkMean || (bright / n) < (P.darkBrightPct / 100);
  } catch (e) { return false; }
}

// Begin loading the clip for cuts[idx] into el; resolves once it is seeked to a
// non-dark chunk with a decoded frame ready (skips near-black chunks).
// Whole-clip precache: clips ≤32 MB (proxy size) are fully fetched to a blob URL,
// so their playback never waits on the (SMB-backed) disk again for this show.
var blobCache = {}; // clipId/p|f -> objectURL (null while fetching)
var blobOrder = []; // FIFO eviction
var blobBusy = false; // one in-flight whole-clip fetch — parallel fetches would starve playback
var PRECACHE_MAX = 96 * 1024 * 1024;
function blobKey(id, proxy) { return id + (proxy ? 'p' : 'f'); }
function evictBlobs(keepKey) {
  for (var i2 = 0; i2 < blobOrder.length && blobOrder.length > 6; i2++) {
    var k2 = blobOrder[i2];
    if (k2 === keepKey) continue;
    var u2 = blobCache[k2];
    if (!u2) continue; // in-flight fetch: leave it alone
    if (stages.some(function (s) { return s.src === u2; })) continue; // live on a stage
    blobOrder.splice(i2, 1);
    URL.revokeObjectURL(u2);
    delete blobCache[k2];
    i2--;
  }
}
function fetchBlobClip(id, proxy, el, st) {
  var v = vids[id];
  var sz = v ? (proxy ? v.lsz : v.sz) : 0;
  if (!v || !P.precache || !sz || sz > PRECACHE_MAX) return;
  var key = blobKey(id, !!proxy);
  if (blobCache[key]) return blobCache[key];
  if (blobCache[key] === null || blobBusy) return; // already fetching
  blobCache[key] = null;
  blobBusy = true;
  blobOrder.push(key);
  fetch('/video/' + id + (proxy ? '?proxy=1' : ''))
    .then(function (r2) { if (!r2.ok) throw 0; return r2.blob(); })
    .then(function (b2) {
      blobBusy = false;
      if (b2.size > PRECACHE_MAX) { delete blobCache[key]; return; }
      var u = URL.createObjectURL(b2);
      blobCache[key] = u;
      evictBlobs(key);
      // element still streaming this same clip over Range (still loading data):
      // switch it to the finished blob and reload locally, no restart of the seek
      if (st && !st.settled && el.readyState < 2 &&
          el.dataset.clip === String(id) && el.dataset.proxy === String(proxy ? 1 : 0)) {
        el.src = u;
        el.load();
      }
    })
    .catch(function () { blobBusy = false; delete blobCache[key]; });
}
function loadClip(el, idx) {
  var L = Math.max(0.1, (cuts[idx + 1] !== undefined ? cuts[idx + 1] : audio.duration) - cuts[idx]);
  var st = { el: el, idx: idx, id: -1, s: 0, done: null, settled: false };
  var r = Promise.withResolvers();
  st.done = r.promise;
  var resolveSt = function () { if (st.settled) return; st.settled = true; r.resolve(); };
  var rejectSt = function () { if (st.settled) return; st.settled = true; r.reject(new Error("clip load timeout")); };
  var attemptDark = function () {
    if (st.settled) return;
    attempts++;
    if (tl) { resolveSt(); return; } // edited timeline: play what the user placed, dark or not
    if (attempts < P.darkRetries && isDark(el)) choose();
    else resolveSt();
  };
  var attempts = 0;
  var choose = function () {
    if (tl) {
      // edited timeline: play exactly what the user laid down; no re-rolls
      var sg = tl[Math.min(idx, tl.length - 1)];
      var v2 = vids[sg.id];
      if (!v2) { resolveSt(); return; } // clip vanished; show previous state
      var proxy2 = P.useLrf && v2.lrfDur;
      st.id = sg.id;
      st.dur = proxy2 ? v2.lrfDur : v2.duration; // seek bounds must match the served file
      st.s = Math.max(0, Math.min(sg.s, st.dur - L - 0.1));
      st.proxy = !!proxy2;
      var cached2 = fetchBlobClip(st.id, !!proxy2, el, st);
      if (el.dataset.clip === String(st.id) && el.dataset.proxy === String(st.proxy ? 1 : 0) && el.readyState >= 1) el.currentTime = st.s;
      else { el.dataset.clip = String(st.id); el.dataset.proxy = String(st.proxy ? 1 : 0); el.src = cached2 || ('/video/' + st.id + (proxy2 ? '?proxy=1' : '')); el.load(); }
      return;
    }
    var base = poolFor().slice().sort(chronoCmp);
    if (!base.length) base = vids.slice().sort(chronoCmp);
    if (P.shuffle) shuffleFor(base); // RNG stream: fixed seed → same shuffle every time
    // drop opener: this cut is scheduled to show the drop's best clip; first
    // attempt only (re-rolls fall back to the walk so a dark pick never sticks)
    var v, proxy;
    var pick = idx !== undefined && !attempts && (cutPick[idx] ?? -1) >= 0 ? cutPick[idx] : -1;
    if (pick >= 0 && vids[pick] && (!P.skipJunk || (vids[pick].dk ?? 0) < 0.5)) {
      v = vids[pick];
      proxy = P.useLrf && v.lrfDur; // orderPos parked: the chronology resumes after the drop
    } else {
      for (var tr = 0; tr < 3; tr++) {
        orderPos = (orderPos + 1) % base.length;
        v = base[orderPos];
        // a still-loading clip re-rolled onto itself would restart the load for ever
        if (!(el.readyState < 2 && el.dataset.clip === String(v.id))) break;
      }
      proxy = P.useLrf && v.lrfDur;
    }
    st.id = v.id;
    st.dur = proxy ? v.lrfDur : v.duration; // seek bounds must match the served file
    st.s = (RNG ? RNG() : Math.random()) * Math.max(0, st.dur - L - 0.1);
    st.proxy = !!proxy;
    var cached = fetchBlobClip(st.id, !!proxy, el, st);
    if (el.dataset.clip === String(st.id) && el.dataset.proxy === String(st.proxy ? 1 : 0) && el.readyState >= 1) el.currentTime = st.s;
    else { el.dataset.clip = String(st.id); el.dataset.proxy = String(st.proxy ? 1 : 0); el.src = cached || ('/video/' + st.id + (proxy ? '?proxy=1' : '')); el.load(); }
  };
  el.onseeked = function () {
    if (st.settled) return;
    // 'seeked' can fire before the frame is decoded (data still trickling) —
    // only settle once HAVE_CURRENT_DATA, via canplay if needed.
    if (el.readyState >= 2) attemptDark();
    else el.oncanplay = function () { el.oncanplay = null; attemptDark(); };
  };
  el.onerror = function () {
    if (st.settled) return;
    attempts++;
    if (attempts < P.darkRetries) choose();
    else resolveSt(); // video unplayable; show last rolled state rather than stall
  };
  el.onloadedmetadata = function () { el.currentTime = st.s; };
  choose();
  var wdStart = Date.now();
  var wd = setInterval(function () { // hung load: reject so the cut is skipped, old clip keeps playing
    if (st.settled) clearInterval(wd);
    else if (el.readyState >= 2) { clearInterval(wd); attemptDark(); }
    else if (Date.now() - wdStart > 25000) { clearInterval(wd); rejectSt(); }
  }, 2000);
  return st;
}

var waitingSwap = false;

// If the on-screen clip hits its end while the next one is still loading,
// loop it back to its start instead of going black.
stages.forEach(function (v) {
  v.addEventListener('ended', function () {
    if (stages[visible] !== v) return;
    if (audio && audio.paused) return; // don't auto-replay while the song is paused
    v.currentTime = +v.dataset.start || 0;
    v.play().catch(function () {});
  });
});

function cut(idx, startAudio) {
  var showing = stages[visible], next = stages[1 - visible];
  var seq = ++cutSeq; // abandoned loaders must never swap after this (stall-skip)
  var st = (pending && pending.idx === idx) ? pending : loadClip(next, idx);
  pending = null;
  st.done.then(function () {
    if (seq !== cutSeq) return; // a newer cut took over this element
    if ($('stage').hidden) return;
    waitingSwap = false;
    next.dataset.start = st.s;
    // honour a pause that happened while this swap was in flight: the clip lands
    // frozen on its first frame; it plays again when the song resumes
    if (!audio.paused) next.play().catch(function () {});
    visible = 1 - visible;
    currentId = st.id;
    showing.style.opacity = '0';
    showCtl();
    var fades = (cutFades[idx] !== undefined ? cutFades[idx] : P.fadeMs);
    // Indie transition: the incoming clip appears already-affected and eases back
    // to identity as it fades in. The FX must be committed statically while the
    // element is still invisible — transitioned changes animate from the element's
    // previous computed value (none), not the newly assigned one, so otherwise the
    // browser animates none→fx→none in two frames at ~0 opacity: invisible.
    var fx = P.fancy ? FXS[Math.floor((RNG ? RNG() : Math.random()) * FXS.length)] : null;
    next.style.transition = 'none';
    next.style.opacity = '0';
    next.style.filter = fx ? fx.f : 'none';
    next.style.transform = fx ? fx.t : 'none';
    void next.offsetWidth; // flush: make the FX state the transition origin
    var fxDur = fades > 0 ? fades : 180; // hard cuts still flash the effect
    next.dataset.swapAt = String(Date.now());
    next.style.transition = 'opacity ' + fades + 'ms linear' + (fx ? ', filter ' + fxDur + 'ms ease-out, transform ' + fxDur + 'ms ease-out' : '');
    next.style.opacity = '1';
    if (fx) { next.style.filter = 'none'; next.style.transform = 'none'; }
    if (startAudio) audio.play().catch(function () {}); // audio starts on the first visible frame
    // once faded out, pause the old clip and preload the beat after this one into it
    setTimeout(function () {
      showing.pause();
      if (cuts[idx + 1] !== undefined) pending = loadClip(showing, idx + 1);
    }, 160);
    if (startAudio) loop();
  }).catch(function () {
    waitingSwap = false; // cut skipped; current clip keeps playing
    if (startAudio) exitStage(); // can't even open the first clip — return to the form
  });
}

function exitStage() {
  cancelAnimationFrame(rafId);
  if (audio) { audio.pause(); audio = null; }
  URL.revokeObjectURL(startUrl);
  stages.forEach(function (v) { v.pause(); v.removeAttribute('src'); delete v.dataset.clip; v.style.opacity = '0'; });
  $('stage').hidden = true;
  $('form').hidden = false;
}

$('start').addEventListener('click', enterStage);
$('stop').addEventListener('click', exitStage);
document.addEventListener('keydown', function (e) {
  if (e.key !== 'Escape') return;
  if ($('panel').classList.contains('open')) closePanel(); // close settings first
  else if (audio && !$('stage').hidden) exitStage();
  else if (!$('edit').hidden) { $('edit').hidden = true; $('form').hidden = false; }
});
</script>
</body>
</html>`;

const json = (x: unknown, status = 200) =>
  new Response(JSON.stringify(x), { status, headers: { "content-type": "application/json" } });

async function handle(req: Request): Promise<Response> {
  const url = new URL(req.url);
  const path = url.pathname;

  if (req.method === "GET" && path === "/")
    return new Response(PAGE, { headers: { "content-type": "text/html; charset=utf-8" } });

  if (req.method === "GET" && path === "/scoreProgress")
    return json({ done: scoreDone, total: scoreTotal });
  if (req.method === "GET" && path === "/videos")
    return json(videos.map(v => {
      const e: Record<string, unknown> = { id: v.id, duration: v.duration, tod: v.tod, day: v.day, score: v.score, sz: v.size, mv: v.mv ?? 0, dk: v.dk ?? 0 };
      if (v.w && v.h) e.ar = +(v.w / v.h).toFixed(2);
      if (v.lrf) e.lrfDur = v.lrfDur;
      if (v.lrfSize) e.lsz = v.lrfSize;
      return e;
    }));

  const m = path.match(/^\/video\/(\d+)$/);
  if (req.method === "GET" && m) {
    const v = videos[+m[1]];
    if (!v) return json({ error: "no such video" }, 404);
    // ?proxy=1 serves the LRF twin when the client wants the lightweight proxies
    const wantProxy = url.searchParams.get("proxy") === "1";
    const srcPath = wantProxy && v.lrf ? v.lrf : v.path;
    const ct = CT[v.ext];
    const size = statSync(srcPath).size;
    const range = (req.headers.get("range") || "").match(/bytes=(\d*)-(\d*)/);
    if (range && (range[1] || range[2])) {
      const start = range[1] ? parseInt(range[1], 10) : 0;
      // Cap open-ended ranges: without a cap every seek streams GBs over
      // storage (e.g. SMB), starving concurrent range requests behind it.
      const end = range[2]
        ? Math.min(parseInt(range[2], 10), size - 1)
        : Math.min(size - 1, start + 64 * 1024 * 1024 - 1);
      if (start > end || start >= size)
        return new Response(null, { status: 416, headers: { "content-range": `bytes */${size}` } });
      return new Response(Bun.file(srcPath).slice(start, end + 1), {
        status: 206,
        headers: {
          "content-range": `bytes ${start}-${end}/${size}`,
          "accept-ranges": "bytes",
          "content-type": ct,
        },
      });
    }
    return new Response(Bun.file(srcPath), { headers: { "accept-ranges": "bytes", "content-type": ct } });
  }

  if (req.method === "POST" && path === "/analyze") {
    const buf = Buffer.from(await req.arrayBuffer());
    if (buf.byteLength === 0) return json({ error: "empty body" }, 400);
    if (buf.byteLength > MAX_UPLOAD) return json({ error: "upload too large (100 MB cap)" }, 413);
    const dir = await fsp.mkdtemp(os.tmpdir() + "/vibeslideshow-");
    try {
      const songPath = dir + "/song";
      await fsp.writeFile(songPath, buf);
      const pcm = await decodeAudio(songPath);
      return json(analyzeSong(pcm, SR));
    } catch (err) {
      if (err instanceof BadAudioError) return json({ error: "could not decode audio" }, 422);
      return json({ error: String(err instanceof Error ? err.message : err) }, 500);
    } finally {
      await fsp.rm(dir, { recursive: true, force: true });
    }
  }

  return json({ error: "not found" }, 404);
}

Bun.serve({
  port,
  fetch: req => handle(req).catch((err: unknown) => json({ error: String(err instanceof Error ? err.message : err) }, 500)),
});
console.log(`listening on http://localhost:${port}`);