// Ambient lullaby under the cell. Default on; status-bar button mutes.
// Web Audio API — WebKitGTK blocks <audio> autoplay on custom protocols.

import { writable } from "svelte/store";
import minecraftMp3 from "../assets/audio/minecraft.mp3?url";

const KEY = "cell_lullaby_mute";
const VOL = 0.28;

type AudioWindow = {
  AudioContext?: typeof AudioContext;
  webkitAudioContext?: typeof AudioContext;
};

function readMuted(): boolean {
  try {
    return localStorage.getItem(KEY) === "1";
  } catch {
    return false;
  }
}

function writeMuted(muted: boolean) {
  try {
    localStorage.setItem(KEY, muted ? "1" : "0");
  } catch {
    // Storage can throw in locked-down webviews.
  }
}

export const cellBgmOn = writable(!readMuted());

let ctx: AudioContext | null = null;
let gain: GainNode | null = null;
let source: AudioBufferSourceNode | null = null;
let buffer: AudioBuffer | null = null;
let playing = false;
let loadP: Promise<AudioBuffer> | null = null;
let started = false;
let kickBound = false;

function makeCtx(): AudioContext {
  const W = window as unknown as AudioWindow;
  const AC = W.AudioContext || W.webkitAudioContext;
  if (!AC) throw new Error("no AudioContext");
  return new AC({ latencyHint: "playback" });
}

async function resumeCtx(): Promise<AudioContext | null> {
  try {
    if (!ctx) {
      ctx = makeCtx();
      gain = ctx.createGain();
      gain.gain.value = VOL;
      gain.connect(ctx.destination);
    }
    if (ctx.state === "suspended") await ctx.resume();
    return ctx;
  } catch {
    return null;
  }
}

async function loadBuffer(): Promise<AudioBuffer> {
  if (buffer) return buffer;
  if (loadP) return loadP;
  loadP = (async () => {
    const ac = await resumeCtx();
    if (!ac) throw new Error("audio ctx");
    const res = await fetch(minecraftMp3);
    if (!res.ok) throw new Error(`${res.status}`);
    const raw = await res.arrayBuffer();
    buffer = await ac.decodeAudioData(raw.slice(0));
    return buffer;
  })();
  try {
    return await loadP;
  } catch (err) {
    loadP = null;
    throw err;
  }
}

function stopSource() {
  if (source) {
    try { source.onended = null; source.stop(); } catch { /* already stopped */ }
    try { source.disconnect(); } catch { /* already disconnected */ }
    source = null;
  }
  playing = false;
}

function startSource(ac: AudioContext, buf: AudioBuffer) {
  if (!gain) return;
  stopSource();
  source = ac.createBufferSource();
  source.buffer = buf;
  source.loop = true;
  source.connect(gain);
  source.onended = () => {
    playing = false;
    source = null;
    if (!readMuted()) void tryPlay();
  };
  source.start();
  playing = true;
  gain.gain.setValueAtTime(VOL, ac.currentTime);
}

async function tryPlay() {
  if (readMuted()) { stopSource(); return; }
  try {
    const ac = await resumeCtx();
    if (!ac) return;
    const buf = await loadBuffer();
    if (readMuted()) return;
    if (ac.state === "suspended") await ac.resume();
    if (playing && source) return;
    startSource(ac, buf);
  } catch {
    // Next pointer/key event retries.
  }
}

function isBgmButton(target: EventTarget | null): boolean {
  return target instanceof Element && !!target.closest(".bgm-btn");
}

function bindKick() {
  if (kickBound) return;
  kickBound = true;
  const kick = (ev: Event) => {
    if (isBgmButton(ev.target)) return;
    if (!readMuted()) void tryPlay();
  };
  window.addEventListener("pointerdown", kick, true);
  window.addEventListener("keydown", kick, true);
}

export function initCellBgm() {
  cellBgmOn.set(!readMuted());
  bindKick();
  if (started) {
    if (!readMuted()) void tryPlay();
    return;
  }
  started = true;
  if (!readMuted()) void tryPlay();
}

export function setCellBgmOn(on: boolean) {
  // Write muted flag synchronously FIRST so any pending tryPlay() will bail.
  writeMuted(!on);
  cellBgmOn.set(on);
  if (on) {
    void resumeCtx().then(() => tryPlay());
  } else {
    stopSource();
  }
}

export function toggleCellBgm() {
  // Synchronous read+write so the pointerdown kick cannot race.
  const wasMuted = readMuted();
  setCellBgmOn(wasMuted);
}
