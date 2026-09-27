#!/usr/bin/env node
// capture-decode.mjs — FW-41 (round 23): the UDS read-out of a sampled-waveform capture -> CSV. Node, no dependencies.
//
//   node tools/capture-decode.mjs blocks.hex > capture.csv
//
// Input: the positive responses to ReadDataByIdentifier 0xFD41 — 62 FD 41 <capture id> <block BE16> <image bytes> —
// one per line as hex (spaces optional, '#' starts a comment), in any order; a block repeated with the same bytes (a
// retransmission) is accepted. Output: '# key=value' lines, the column row, one row per record: k (the index relative to
// the trigger record), t_rel_us (its time relative to the trigger record, wrap-safe), then every channel of the image's
// own channel list as value = raw x scale ("NaN": no value), the flags in hex. Exit 1, with the reason on stderr, when
// the blocks are not one whole capture (a block missing or conflicting, two capture ids, a short block inside the image,
// an image the header does not account for) or not a format-1 image. The image format: docs/firmware-contract.md §10i;
// the service tool (tool/) reads the same format.
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

const BLOCK_BYTES = 56;
const REASON = ['none', 'fault', 'command', 'level'];
const RAW_NONE = -32768;

export function assemble(text) {
  const blocks = new Map();
  let id = -1;
  text.split(/\r?\n/).forEach((raw, i) => {
    const line = raw.replace(/#.*/, '').replace(/\s+/g, '');
    if (line === '') return;
    if (!/^(?:[0-9a-fA-F]{2})+$/.test(line)) throw new Error(`line ${i + 1}: not hex bytes`);
    const b = Buffer.from(line, 'hex');
    if (b.length < 7 || b[0] !== 0x62 || b[1] !== 0xfd || b[2] !== 0x41) throw new Error(`line ${i + 1}: not a 62 FD 41 response`);
    if (id < 0) id = b[3];
    else if (b[3] !== id) throw new Error(`line ${i + 1}: capture ${b[3]}, the lines before it capture ${id}`);
    const k = b.readUInt16BE(4);
    const data = b.subarray(6);
    const prev = blocks.get(k);
    if (prev && !prev.equals(data)) throw new Error(`block ${k} twice with different bytes`);
    blocks.set(k, data);
  });
  if (blocks.size === 0) throw new Error('no blocks');
  const n = Math.max(...blocks.keys()) + 1;
  const parts = [];
  for (let k = 0; k < n; k++) {
    const d = blocks.get(k);
    if (!d) throw new Error(`block ${k} missing`);
    if (k < n - 1 && d.length !== BLOCK_BYTES) throw new Error(`block ${k}: ${d.length} bytes, not ${BLOCK_BYTES}`);
    parts.push(d);
  }
  return { id, image: Buffer.concat(parts) };
}

export function decode(img) {
  if (img.length < 40 || img.toString('latin1', 0, 4) !== 'TICP') throw new Error('not a capture image');
  if (img[4] !== 1) throw new Error(`image format ${img[4]}; this decoder reads format 1`);
  const hdrBytes = img.readUInt16LE(6);
  const recBytes = img[35];
  const nch = img[36];
  const descBytes = img[37];
  const n = img.readUInt16LE(20);
  if (descBytes < 20 || hdrBytes < 40 + nch * descBytes) throw new Error('header too short for its channel list');
  if (img.length !== hdrBytes + n * recBytes) {
    throw new Error(`image ${img.length} bytes; its header says ${hdrBytes} + ${n} x ${recBytes}`);
  }
  const meta = {
    format: img[4],
    flags_layout: img[5],
    fw_id: img.readUInt32LE(8),
    capture_id: img[12],
    reason: REASON[img[13]] ?? String(img[13]),
    trigger_dtc: img.readUInt16LE(14),
    trigger_rows: img.readUInt16LE(16),
    trigger_index: img.readUInt16LE(18),
    records: n,
    pre: img.readUInt16LE(22),
    rate_hz: img.readUInt32LE(24),
    sources: img[28],
    level_channel: img[29] & 0x7f,
    level_falling: img[29] >> 7,
    level_raw: img.readInt16LE(30),
    level_hys_raw: img.readUInt16LE(32),
    sku: img[34],
  };
  if (n === 0 || meta.trigger_index >= n) throw new Error(`trigger index ${meta.trigger_index} outside ${n} records`);
  const text = (o, len) => img.toString('latin1', o, o + len).replace(/\0[\s\S]*$/, '');
  const ch = [];
  for (let i = 0; i < nch; i++) {
    const d = 40 + i * descBytes;
    const c = { name: text(d, 8), unit: text(d + 8, 6), type: img[d + 14], off: img[d + 15], scale: img.readFloatLE(d + 16) };
    if (c.off + (c.type === 0 ? 4 : 2) > recBytes) throw new Error(`channel ${c.name} outside the record`);
    ch.push(c);
  }
  const recs = [];
  for (let j = 0; j < n; j++) {
    const o = hdrBytes + j * recBytes;
    recs.push(ch.map((c) => (c.type === 0 ? img.readUInt32LE(o + c.off) : img.readInt16LE(o + c.off))));
  }
  return { meta, ch, recs };
}

const hex = (v, w) => `0x${v.toString(16).toUpperCase().padStart(w, '0')}`;

export function toCsv({ meta, ch, recs }) {
  const shown = { ...meta, fw_id: hex(meta.fw_id, 8), trigger_rows: hex(meta.trigger_rows, 4), sources: hex(meta.sources, 2) };
  const out = Object.entries(shown).map(([k, v]) => `# ${k}=${v}`);
  const iT = ch.findIndex((c) => c.name === 't_us');
  if (iT < 0) throw new Error('no t_us channel');
  const tTrig = recs[meta.trigger_index][iT];
  out.push(['k', 't_rel_us', ...ch.map((c) => `${c.name}[${c.unit}]`)].join(','));
  recs.forEach((r, j) => {
    const cols = [j - meta.trigger_index, (r[iT] - tTrig) | 0];
    ch.forEach((c, i) => {
      if (c.type === 0) cols.push(c.unit === 'bits' ? hex(r[i], 8) : r[i]);
      else cols.push(r[i] === RAW_NONE ? 'NaN' : Number((r[i] * c.scale).toPrecision(7)));
    });
    out.push(cols.join(','));
  });
  return `${out.join('\n')}\n`;
}

function main(args) {
  if (args.length !== 1) {
    console.error('usage: node tools/capture-decode.mjs <blocks.hex>');
    return 2;
  }
  try {
    const { id, image } = assemble(readFileSync(args[0], 'utf8'));
    const d = decode(image);
    if (d.meta.capture_id !== id) throw new Error(`blocks of capture ${id}, the image says ${d.meta.capture_id}`);
    process.stdout.write(toCsv(d));
    return 0;
  } catch (e) {
    console.error(`capture-decode: ${e.message}`);
    return 1;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) process.exitCode = main(process.argv.slice(2));
