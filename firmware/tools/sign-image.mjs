#!/usr/bin/env node
// sign-image.mjs — the FW-38 image container (firmware/src/boot/image.h): a 128-byte header, then the payload.
//
//   node tools/sign-image.mjs --in app.bin --out app.tifw --key <ed25519-private.pem | test> \
//        --target <1..4 | 8xx-sic | 8xx-igbt | 4xx-igbt | 4xx-sic> --fw-id <0x0A0F0015> --sec-ver <n>
//   node tools/sign-image.mjs --verify app.tifw --key <ed25519-public.pem | private.pem | test>
//   node tools/sign-image.mjs --pubkey <ed25519-public.pem | private.pem | test>   the 32 bytes as a C list
//
// Header (little-endian): magic "TIFW", format 1 (u16), header length 128 (u16), target = the SKU (u32, ti_sku_t:
// 1 8XX SiC, 2 8XX IGBT, 3 4XX IGBT, 4 4XX SiC), payload length (u32), TI_FW_ID (u32), security version (u32,
// anti-rollback: raise it with every release that must never be downgraded from), 8 reserved zero bytes, SHA-256 of
// the payload (32), Ed25519 signature over bytes 0..63 (64).
//
// Keys. `--key test` is derived from a public string (TEST_SEED below): it is the host tests' key, TEST ONLY — its
// public half is in the host build's key table (src/boot/verify.c) and never in a target build. The RELEASE key pair
// is generated and kept offline (an HSM or an air-gapped signing station: `openssl genpkey -algorithm ed25519`); its
// private half never enters this repository or a build machine. The release build passes the public half with
// -DTI_FW38_PUBKEY=<the --pubkey output> (or the HSE/OTP copy: docs/target-bringup.md, FW-38). An HSM signs the
// same 64 bytes this tool signs; this file is the reference of the format.
//
// Node standard library only (crypto: Ed25519, SHA-256); no dependencies.
import { createHash, createPrivateKey, createPublicKey, sign, verify } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';

const MAGIC = 'TIFW';
const FORMAT = 1;
const HDR_LEN = 128;
const TBS_LEN = 64; // the signed prefix: every field + the payload hash
const TARGETS = { '8xx-sic': 1, '8xx-igbt': 2, '4xx-igbt': 3, '4xx-sic': 4 };
const TEST_SEED = createHash('sha256').update('TI FW-38 TEST KEY - NOT FOR RELEASE').digest();

function die(msg) {
  console.error(`sign-image: ${msg}`);
  process.exit(2);
}

function args(argv) {
  const a = {};
  for (let i = 0; i < argv.length; i += 1) {
    if (!argv[i].startsWith('--')) die(`unexpected argument ${argv[i]}`);
    a[argv[i].slice(2)] = argv[i + 1];
    i += 1;
  }
  return a;
}

function u32(v, name) {
  const n = Number(v);
  if (!Number.isInteger(n) || n < 0 || n > 0xffffffff) die(`${name}: not a 32-bit unsigned integer: ${v}`);
  return n;
}

// RFC 8410 PKCS#8 wrapping of a raw 32-byte Ed25519 seed
function keyFromSeed(seed) {
  const der = Buffer.concat([Buffer.from('302e020100300506032b657004220420', 'hex'), seed]);
  return createPrivateKey({ key: der, format: 'der', type: 'pkcs8' });
}

function privateKey(spec) {
  if (spec === 'test') return keyFromSeed(TEST_SEED);
  const k = createPrivateKey(readFileSync(spec));
  if (k.asymmetricKeyType !== 'ed25519') die(`${spec}: not an Ed25519 private key`);
  return k;
}

function publicKey(spec) {
  if (spec === 'test') return createPublicKey(keyFromSeed(TEST_SEED));
  const pem = readFileSync(spec);
  const k = pem.includes('PRIVATE') ? createPublicKey(createPrivateKey(pem)) : createPublicKey(pem);
  if (k.asymmetricKeyType !== 'ed25519') die(`${spec}: not an Ed25519 key`);
  return k;
}

function rawPublic(k) {
  return k.export({ format: 'der', type: 'spki' }).subarray(-32); // SPKI = 12-byte prefix + the 32-byte point
}

function buildContainer(payload, { target, fwId, secVer, key }) {
  if (payload.length === 0) die('empty payload');
  const h = Buffer.alloc(HDR_LEN);
  h.write(MAGIC, 0, 'ascii');
  h.writeUInt16LE(FORMAT, 4);
  h.writeUInt16LE(HDR_LEN, 6);
  h.writeUInt32LE(target, 8);
  h.writeUInt32LE(payload.length, 12);
  h.writeUInt32LE(fwId, 16);
  h.writeUInt32LE(secVer, 20);
  createHash('sha256').update(payload).digest().copy(h, 32);
  sign(null, h.subarray(0, TBS_LEN), key).copy(h, TBS_LEN);
  return Buffer.concat([h, payload]);
}

function check(img, key) {
  const h = img.subarray(0, HDR_LEN);
  const len = h.readUInt32LE(12);
  const hash = createHash('sha256').update(img.subarray(HDR_LEN, HDR_LEN + len)).digest();
  return {
    magic: h.toString('ascii', 0, 4) === MAGIC && h.readUInt16LE(4) === FORMAT && h.readUInt16LE(6) === HDR_LEN,
    length: img.length === HDR_LEN + len,
    hash: hash.equals(h.subarray(32, 64)),
    signature: verify(null, h.subarray(0, TBS_LEN), key, h.subarray(TBS_LEN, HDR_LEN)),
    target: h.readUInt32LE(8), fwId: `0x${h.readUInt32LE(16).toString(16).padStart(8, '0')}`, secVer: h.readUInt32LE(20),
  };
}

function main() {
  const a = args(process.argv.slice(2));
  if (a.pubkey) {
    console.log([...rawPublic(publicKey(a.pubkey))].map((b) => `0x${b.toString(16).padStart(2, '0')}`).join(','));
    return;
  }
  if (a.verify) {
    const r = check(readFileSync(a.verify), publicKey(a.key ?? die('--key missing')));
    console.log(JSON.stringify(r));
    process.exit(r.magic && r.length && r.hash && r.signature ? 0 : 1);
  }
  for (const k of ['in', 'out', 'key', 'target', 'fw-id', 'sec-ver']) if (a[k] === undefined) die(`--${k} missing`);
  const target = TARGETS[a.target] ?? u32(a.target, 'target');
  if (target < 1 || target > 4) die(`target ${a.target}: 1..4 (ti_sku_t)`);
  const img = buildContainer(readFileSync(a.in), {
    target, fwId: u32(a['fw-id'], 'fw-id'), secVer: u32(a['sec-ver'], 'sec-ver'), key: privateKey(a.key),
  });
  writeFileSync(a.out, img);
  console.log(`${a.out}: ${img.length} bytes, target ${target}, fw-id ${a['fw-id']}, sec-ver ${a['sec-ver']}`);
}

try {
  main();
} catch (e) {
  die(e.message); // an unreadable file or key: exit 2, no stack trace
}
