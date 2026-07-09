// SNAPSHOT (algorithm reference for the C++ port — do not edit here)
// source: kemuri-stream-checker @ 02bc791 max_for_live/codec_filter.js
// taken:  2026-07-09

// codec_filter.js  (v2)
// KemuriStreamSim - measurement & control engine.
//
// All logic that previously lived in fragile Max object chains
// (expr / pack / clip / gate) now lives here:
//   - BS.1770 gated INTEGRATED loudness (absolute -70 + relative -10 gate)
//   - per-platform normalization policy (attenuate / boost / no-op)
//   - boost-only limiter threshold control
//   - codec EQ + K-weighting biquad coefficient generation
//
// Inlet messages:
//   int 0..5            platform index from live.tab
//                       (0=Off 1=Spotify 2=YouTube 3=Apple 4=TIDAL 5=SoundCloud)
//   "power <f>"         K-weighted mean-square power (L+R), every ~100 ms
//                       from [number~] after the 400 ms average~ window
//   "autoloud <0/1>"    Auto LUFS toggle state
//   "setsr <hz>"        sample rate from dspstate~
//   "reset"             clear measurement history (re-measure)
//   bang                re-emit everything (init)
//
// Outlets:
//   0  codec EQ coefficients          -> cascade~ L/R (audio path)
//   1  target LUFS (float, display)
//   2  true-peak ceiling dB (float, display)
//   3  preset label (symbol)          -> [prepend set] -> comment
//   4  platform name (symbol)
//   5  K-weighting coefficients       -> cascade~ L/R (measurement path)
//   6  integrated LUFS (float, display)
//   7  momentary LUFS (float, display)
//   8  correction dB (float, display)
//   9  gain ramp list [amp, ms]       -> line~ -> *~ (auto-normalization)
//  10  "threshold <db>"               -> limi~ (boost-only limiter)

autowatch = 1;
inlets    = 1;
outlets   = 11;

// ---------------------------------------------------------------------------
// presets
// ---------------------------------------------------------------------------
// boost      : does the platform turn quiet tracks UP?
// boost_cap  : max boost in dB (Spotify documents roughly +3..+5 with limiter)
// target     : null = no loudness normalization at all
var PRESETS = {
    "off": {
        eq: [], target: null, tp: 0.0, boost: false, boost_cap: 0,
        label: "Off (bypass codec EQ)"
    },
    "spotify": {
        eq: [
            ["peaking",    250.0, 1.0,  0.5],
            ["peaking",   4000.0, 1.0, -0.3],
            ["highshelf",16000.0, 0.7, -1.5]
        ],
        target: -14.0, tp: -1.0, boost: true, boost_cap: 3.0,
        label: "Spotify / OGG 128k / -14 LUFS / boost+limiter"
    },
    "youtube": {
        eq: [ ["highshelf", 18000.0, 0.7, -0.5] ],
        target: -14.0, tp: -1.0, boost: false, boost_cap: 0,
        label: "YouTube / Opus 128k / -14 LUFS / no boost"
    },
    "apple_music": {
        eq: [ ["highshelf", 17000.0, 0.7, -0.8] ],
        target: -16.0, tp: -2.0, boost: false, boost_cap: 0,
        label: "Apple Music / AAC 256k / -16 LUFS / no boost"
    },
    "tidal": {
        eq: [],                                   // FLAC lossless
        target: -14.0, tp: -1.0, boost: false, boost_cap: 0,
        label: "TIDAL / FLAC lossless / -14 LUFS / no boost"
    },
    "soundcloud": {
        eq: [
            ["peaking",    300.0, 1.0,  0.4],
            ["highshelf",15500.0, 0.7, -2.0]
        ],
        target: null, tp: 0.0, boost: false, boost_cap: 0,
        label: "SoundCloud / MP3 128k / no normalization"
    }
};

var PLATFORM_BY_INDEX = ["off", "spotify", "youtube", "apple_music", "tidal", "soundcloud"];

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------
var sample_rate   = 44100.0;
var current       = "off";
var autoloud      = 0;
var blocks        = [];      // K-weighted power per 400ms block (abs-gated)
var MAX_BLOCKS    = 18000;   // ~30 min at 10 blocks/s
var last_corr     = 0.0;     // last emitted correction (dB)
var last_thresh   = 0.0;     // last emitted limiter threshold (dB)
var ATTEN_FLOOR   = -24.0;   // max attenuation we simulate
var SILENCE_GATE  = -70.0;   // BS.1770 absolute gate

// ---------------------------------------------------------------------------
// inlet handlers
// ---------------------------------------------------------------------------
function msg_int(v)   { var n = PLATFORM_BY_INDEX[v]; if (n) apply_preset(n); }
function msg_float(v) { msg_int(Math.floor(v)); }

function bang() {
    emit_kweight();
    apply_preset(current);
    emit_gain(true);
}

function reset() {
    blocks = [];
    recompute(null);
}

function anything() {
    var sel  = messagename;
    var args = arrayfromarguments(arguments);

    if (sel === "power") {
        if (args.length >= 1) on_power(parseFloat(args[0]));
        return;
    }
    if (sel === "autoloud") {
        autoloud = (args.length >= 1 && parseFloat(args[0]) > 0.5) ? 1 : 0;
        recompute(null);
        return;
    }
    if (sel === "setsr") {
        if (args.length >= 1) {
            sample_rate = parseFloat(args[0]) || sample_rate;
            emit_kweight();
            apply_preset(current);
        }
        return;
    }
    // bare platform name ("spotify" etc.)
    if (PRESETS[sel]) apply_preset(sel);
}

// ---------------------------------------------------------------------------
// measurement (BS.1770 gated integrated loudness)
// ---------------------------------------------------------------------------
function power_to_lufs(p) {
    if (p <= 1e-10) return -80.0;
    return -0.691 + 10.0 * Math.log(p) / Math.LN10;
}

function on_power(p) {
    var momentary = power_to_lufs(p);

    // absolute gate: silence never enters the history -> the integrated
    // reading FREEZES during silence instead of decaying (this is the
    // structural fix for the silence->blast bug)
    if (momentary > SILENCE_GATE) {
        blocks.push(p);
        if (blocks.length > MAX_BLOCKS) blocks.shift();
    }

    recompute(momentary);
}

function integrated_lufs() {
    var n = blocks.length;
    if (n === 0) return null;

    // pass 1: mean of absolute-gated blocks
    var sum = 0.0, i;
    for (i = 0; i < n; i++) sum += blocks[i];
    var l1 = power_to_lufs(sum / n);

    // pass 2: relative gate at l1 - 10 LU
    var thr = l1 - 10.0;
    var sum2 = 0.0, m = 0;
    for (i = 0; i < n; i++) {
        if (power_to_lufs(blocks[i]) > thr) { sum2 += blocks[i]; m++; }
    }
    if (m === 0) return l1;
    return power_to_lufs(sum2 / m);
}

// ---------------------------------------------------------------------------
// correction policy
// ---------------------------------------------------------------------------
function recompute(momentary) {
    var p   = PRESETS[current];
    var li  = integrated_lufs();

    if (momentary !== null) outlet(7, momentary);
    outlet(6, (li === null) ? -80.0 : li);

    var corr = 0.0;
    if (p && p.target !== null && li !== null && autoloud === 1) {
        var diff = p.target - li;
        if (diff < 0.0) {
            corr = Math.max(diff, ATTEN_FLOOR);          // attenuate (everyone)
        } else if (p.boost) {
            corr = Math.min(diff, p.boost_cap);          // boost (Spotify only)
        }
        // platforms without boost: quiet tracks stay quiet (corr = 0)
    }

    outlet(8, corr);

    if (Math.abs(corr - last_corr) > 0.05) {
        last_corr = corr;
        emit_gain(false);
    }

    // boost-only limiter: real services only limit when they turn you UP
    var thresh = (corr > 0.0 && p && p.boost) ? p.tp : 0.0;
    if (thresh !== last_thresh) {
        last_thresh = thresh;
        outlet(10, "threshold", thresh);
    }
}

function emit_gain(force) {
    var amp = Math.pow(10.0, last_corr / 20.0);
    outlet(9, [amp, 2000]);     // -> line~ : 2 s smooth ramp
    if (force) {
        outlet(10, "threshold", last_thresh);
    }
}

// ---------------------------------------------------------------------------
// presets / coefficients
// ---------------------------------------------------------------------------
function apply_preset(name) {
    var p = PRESETS[name];
    if (!p) return;

    current = name;

    var coeffs = (p.eq.length === 0)
        ? [1.0, 0.0, 0.0, 0.0, 0.0]
        : compute_coeffs(p.eq);

    outlet(0, coeffs);
    outlet(1, (p.target === null) ? 0.0 : p.target);
    outlet(2, p.tp);
    outlet(3, p.label);
    outlet(4, name);

    recompute(null);
}

function emit_kweight() {
    // EBU R128 K-weighting:
    //   stage 1: high-shelf 1681.97 Hz, +3.99957 dB, Q 0.7071
    //   stage 2: high-pass    38.13 Hz, Q 0.5
    var s1 = biquad("highshelf", 1681.97, 0.7071, 3.99957);
    var s2 = biquad("highpass",    38.13, 0.5,    0.0);
    outlet(5, s1.concat(s2));
}

function compute_coeffs(filters) {
    var out = [];
    for (var i = 0; i < filters.length; i++) {
        var c = biquad(filters[i][0], filters[i][1], filters[i][2], filters[i][3]);
        for (var j = 0; j < c.length; j++) out.push(c[j]);
    }
    return out;
}

// RBJ Audio EQ Cookbook, normalized for cascade~ [b0 b1 b2 a1 a2]
function biquad(type, freq, Q, gain_db) {
    var A     = Math.pow(10.0, gain_db / 40.0);
    var w0    = 2.0 * Math.PI * freq / sample_rate;
    var cosw0 = Math.cos(w0);
    var sinw0 = Math.sin(w0);
    var alpha = sinw0 / (2.0 * Math.max(Q, 1e-6));
    var a0, a1, a2, b0, b1, b2;

    if (type === "peaking") {
        b0 = 1.0 + alpha * A;  b1 = -2.0 * cosw0;  b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;  a1 = -2.0 * cosw0;  a2 = 1.0 - alpha / A;
    } else if (type === "highshelf") {
        var sq = Math.sqrt(A);
        b0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sq * alpha);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
        b2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sq * alpha);
        a0 = (A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sq * alpha;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
        a2 = (A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sq * alpha;
    } else if (type === "lowshelf") {
        var sq2 = Math.sqrt(A);
        b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sq2 * alpha);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
        b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sq2 * alpha);
        a0 = (A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sq2 * alpha;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
        a2 = (A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sq2 * alpha;
    } else if (type === "highpass") {
        b0 = (1.0 + cosw0) / 2.0;  b1 = -(1.0 + cosw0);  b2 = (1.0 + cosw0) / 2.0;
        a0 = 1.0 + alpha;          a1 = -2.0 * cosw0;     a2 = 1.0 - alpha;
    } else if (type === "lowpass") {
        b0 = (1.0 - cosw0) / 2.0;  b1 = 1.0 - cosw0;      b2 = (1.0 - cosw0) / 2.0;
        a0 = 1.0 + alpha;          a1 = -2.0 * cosw0;     a2 = 1.0 - alpha;
    } else {
        return [1.0, 0.0, 0.0, 0.0, 0.0];
    }
    return [b0/a0, b1/a0, b2/a0, a1/a0, a2/a0];
}

function arrayfromarguments(args) {
    var out = [];
    for (var i = 0; i < args.length; i++) out.push(args[i]);
    return out;
}
