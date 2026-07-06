// Extract deterministic reference vectors from the JS source-of-truth
// (docs/reference/kemuri_generator.js) for the C++ port's G3 gate.
// The JS relies on Max/MSP globals (LiveAPI, post, outlet); we stub them
// and evaluate the file in a vm context, then call the pure functions.

const fs = require('fs');
const vm = require('vm');
const path = require('path');

// リポジトリルートは本スクリプト (tools/reference/) から 2 階層上。
const REPO = path.resolve(__dirname, '..', '..');
const SRC = path.join(REPO, 'docs/reference/kemuri_generator.js');
const OUT = path.join(REPO, 'tests/reference');

fs.mkdirSync(OUT, { recursive: true });

const src = fs.readFileSync(SRC, 'utf8');

const sandbox = {
  Math: Math, JSON: JSON, parseInt, parseFloat, isNaN, post: () => {},
  outlet: () => {}, inlet: 0,
  LiveAPI: function () { return { get: () => [0], call: () => {}, set: () => {}, unquotedpath: '' }; },
};
vm.createContext(sandbox);
vm.runInContext(src, sandbox);

const write = (name, obj) => {
  fs.writeFileSync(path.join(OUT, name), JSON.stringify(obj, null, 2) + '\n');
  console.log('wrote', name);
};

// ── 1. Pitch token resolution (all tokens, several contexts) ───────
{
  const numeric = [];
  for (let n = -5; n <= 12; n++) numeric.push(n);
  const strTokens = ['3rd', '4th', '5th', '6th', 'b6', 'b7', 'low5', 'octave',
                     'p2', 'p3', 'p4', 'p5', 'approach-1', 'approach+1', 'approach-2'];
  const tokens = numeric.concat(strTokens);
  const rows = [];
  const ctxRoots = [0, 3, 5, 7, 11];
  const nextRoots = [0, 2, 5, 9];
  for (const cr of ctxRoots) {
    for (const cm of [false, true]) {
      const ctx = sandbox.makeKeyContext(cr, cm ? 'min' : 'maj');
      for (const nr of nextRoots) {
        for (const nm of [false, true]) {
          const nctx = sandbox.makeKeyContext(nr, nm ? 'min' : 'maj');
          for (const t of tokens) {
            rows.push({
              token: t, ctxRoot: cr, ctxMinor: cm,
              nextRoot: nr, nextMinor: nm,
              midi: sandbox._resolvePitch(t, ctx, nctx),
            });
          }
        }
      }
    }
  }
  write('pitch_tokens.json', rows);
}

// ── 2. Key context construction ────────────────────────────────────
{
  const rows = [];
  for (let r = 0; r < 12; r++) {
    for (const q of ['maj', 'min']) {
      const c = sandbox.makeKeyContext(r, q);
      rows.push({
        root: r, minor: c.isMinor, lowAnchor: c.lowAnchor, midAnchor: c.midAnchor,
        scale: c.scale, chord: c.chord, penta: c.penta,
      });
    }
  }
  write('key_context.json', rows);
}

// ── 3. clampBass / snapNear ─────────────────────────────────────────
{
  const rows = [];
  for (let m = 10; m <= 70; m += 3) rows.push({ fn: 'clampBass', in: m, anchor: 0, out: sandbox.clampBass(m) });
  const anchors = [24, 31, 36, 43, 47];
  for (const a of anchors) for (let m = 20; m <= 60; m += 5) rows.push({ fn: 'snapNear', in: m, anchor: a, out: sandbox.snapNear(m, a) });
  write('clamp_snap.json', rows);
}

// ── 4. Phrase-development flags (mirrors buildNotes loop) ───────────
// These are inline in buildNotes; reproduce the exact expressions here
// (copied verbatim from the JS) so the reference stays tied to the source.
{
  const rows = [];
  for (const bars of [4, 8, 16]) {
    for (const fill of [0, 20, 50, 100]) {
      const fillBars = (fill <= 0) ? 0 : Math.min(4, Math.ceil(fill / 25.0));
      const half = Math.floor(bars / 2);
      for (let bar = 0; bar < bars; bar++) {
        const barInPhr = bar % 4;
        const inDevHalf = (bars <= 4) || (bar >= half);
        const isLastOfPhrase = inDevHalf && (barInPhr === 3);
        const isLastBarTotal = (bar === bars - 1);
        const isDevelopment = (bars >= 8) && isLastBarTotal;
        const midDevelopment = (bars >= 16) && (bar === half + 3);
        const isFill = inDevHalf && fillBars > 0 && barInPhr >= (4 - fillBars);
        rows.push({
          bars, fill, bar, fillBars, barInPhrase: barInPhr, inDevHalf,
          isLastOfPhrase, isDevelopment, midDevelopment, isFill,
          // as passed to generateBar:
          paramIsDevelopment: isDevelopment || midDevelopment,
          paramIsFinalClimax: isDevelopment,
        });
      }
    }
  }
  write('phrase_flags.json', rows);
}

// ── 5. Chord progression detection ─────────────────────────────────
{
  sandbox.g_root = 0; sandbox.g_mode = 0;
  const cases = [];
  // Case A: C major triad (whole bar), then A minor triad (whole bar)
  const A = [];
  for (const p of [48, 52, 55]) A.push({ pitch: p, start: 0, duration: 4 });
  for (const p of [45, 48, 52]) A.push({ pitch: p, start: 4, duration: 4 });
  cases.push({ name: 'C_then_Am', notes: A, clipLen: 8, segBeats: 4 });
  // Case B: G major then D major, half-bar segments
  const B = [];
  for (const p of [43, 47, 50]) B.push({ pitch: p, start: 0, duration: 2 });
  for (const p of [50, 54, 57]) B.push({ pitch: p, start: 2, duration: 2 });
  cases.push({ name: 'G_D_half', notes: B, clipLen: 4, segBeats: 2 });
  // Case C: empty second segment inherits first
  const C = [];
  for (const p of [50, 53, 57]) C.push({ pitch: p, start: 0, duration: 4 }); // D minor
  cases.push({ name: 'Dm_then_empty', notes: C, clipLen: 8, segBeats: 4 });
  const rows = [];
  for (const c of cases) {
    const prog = sandbox._detectProgression(c.notes, c.clipLen, c.segBeats);
    rows.push({
      name: c.name,
      notes: c.notes.map(n => [n.pitch, n.start, n.duration]),
      clipLen: c.clipLen, segBeats: c.segBeats,
      defaultRoot: 0, defaultMode: 0,
      prog,
    });
  }
  write('chord_detect.json', rows);
}

// ── 6. Key detection (K-S correlation + 24-candidate pick) ─────────
{
  const pick = (hist) => {
    let total = 0;
    for (let i = 0; i < 12; i++) total += hist[i];
    const norm = [];
    for (let i = 0; i < 12; i++) norm.push(total > 0 ? hist[i] / total : 0);
    let bestScore = -Infinity, bestRoot = 0, bestMode = 0;
    for (let root = 0; root < 12; root++) {
      const mj = sandbox._ksCorr(norm, sandbox.KS_MAJOR, root);
      const mn = sandbox._ksCorr(norm, sandbox.KS_MINOR, root);
      if (mj > bestScore) { bestScore = mj; bestRoot = root; bestMode = 0; }
      if (mn > bestScore) { bestScore = mn; bestRoot = root; bestMode = 1; }
    }
    return { bestRoot, bestMode, bestScore };
  };
  const rows = [];
  // C major scale emphasis
  const cmaj = [0,0,0,0,0,0,0,0,0,0,0,0];
  for (const pc of [0,2,4,5,7,9,11]) cmaj[pc] = 1;
  cmaj[0] += 2; cmaj[7] += 1;
  rows.push({ name: 'C_major_scale', hist: cmaj, ...pick(cmaj) });
  // A natural minor
  const amin = [0,0,0,0,0,0,0,0,0,0,0,0];
  for (const pc of [9,11,0,2,4,5,7]) amin[pc] = 1;
  amin[9] += 2; amin[4] += 1;
  rows.push({ name: 'A_minor_scale', hist: amin, ...pick(amin) });
  // Single pitch class C
  const single = [0,0,0,0,0,0,0,0,0,0,0,0]; single[0] = 5;
  rows.push({ name: 'single_C', hist: single, ...pick(single) });
  write('key_detect.json', rows);
}

// ── 7. Interaction score ────────────────────────────────────────────
{
  const rows = [];
  const hists = {
    downbeats: (() => { const h = new Array(16).fill(0); h[0]=1; h[4]=1; h[8]=1; h[12]=1; return h; })(),
    offbeats:  (() => { const h = new Array(16).fill(0); h[2]=1; h[6]=1; h[10]=1; h[14]=1; return h; })(),
    dense:     new Array(16).fill(1),
  };
  const libs = { premier: sandbox.PREMIER_PATTERNS, dilla: sandbox.DILLA_PATTERNS,
                 ninth: sandbox.NINTH_PATTERNS, pete: sandbox.PETEROCK_PATTERNS };
  for (const [hname, h] of Object.entries(hists)) {
    sandbox.g_onsetHist = h;
    for (const [lname, lib] of Object.entries(libs)) {
      for (let i = 0; i < lib.length; i++) {
        rows.push({ histName: hname, hist: h.slice(), lib: lname, pattern: lib[i].name,
                    score: sandbox._interactionScore(lib[i]) });
      }
    }
  }
  sandbox.g_onsetHist = null;
  write('interaction.json', rows);
}

// ── 8. Loop detection ───────────────────────────────────────────────
{
  const mk = (roots) => roots.map(r => ({ startBeat: 0, durationBeats: 4, root: r, quality: 'maj' }));
  const cases = [
    { name: 'period4', prog: mk([0,5,7,2, 0,5,7,2]) },
    { name: 'period8', prog: mk([0,5,7,2, 3,8,10,5, 0,5,7,2, 3,8,10,5]) },
    { name: 'noperiod', prog: mk([0,1,2,3,4,5]) },
    { name: 'short', prog: mk([0,5]) },
  ];
  const rows = cases.map(c => ({
    name: c.name,
    prog: c.prog.map(seg => [seg.root, seg.quality]),
    loopBars: sandbox._detectLoopBars(c.prog),
  }));
  write('loop_detect.json', rows);
}

console.log('done');
