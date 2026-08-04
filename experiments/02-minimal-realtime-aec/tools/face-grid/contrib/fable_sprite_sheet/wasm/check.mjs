// Byte-identical verification: the WASM build must reproduce the
// native renderer exactly. Compares (a) aggregate scenario CRCs from
// tests/golden_crcs.txt and (b) raw RGB565 frames dumped by the
// native harness (`build/test_sprite_face --raw build/raw`).
import { readFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..");

const createSpriteFaceModule = (
  await import(join(root, "build", "wasm", "sprite-face.mjs"))
).default;
const module_ = await createSpriteFaceModule();

const atlasCount = module_._sprite_wasm_atlas_count();
const frameBytes = module_._sprite_wasm_frame_bytes();
const names = [];
for (let index = 0; index < atlasCount; ++index) {
  names.push(
    module_.UTF8ToString(module_._sprite_wasm_atlas_name(index)),
  );
}

let failures = 0;

// (a) aggregate CRCs against the native golden file.
const goldenText = readFileSync(
  join(root, "tests", "golden_crcs.txt"),
  "utf8",
);
const golden = new Map();
for (const line of goldenText.trim().split("\n")) {
  const [name, crc] = line.trim().split(/\s+/);
  golden.set(name, parseInt(crc, 16) >>> 0);
}
for (let index = 0; index < atlasCount; ++index) {
  const crc = module_._sprite_wasm_scenario_crc(index) >>> 0;
  const want = golden.get(names[index]);
  if (want === undefined) {
    console.log(`FAIL ${names[index]}: missing from golden_crcs.txt`);
    ++failures;
  } else if (crc !== want) {
    console.log(
      `FAIL ${names[index]}: wasm scenario crc ${crc
        .toString(16)
        .padStart(8, "0")} != native ${want
        .toString(16)
        .padStart(8, "0")}`,
    );
    ++failures;
  } else {
    console.log(
      `ok   ${names[index]}: scenario crc ${crc
        .toString(16)
        .padStart(8, "0")} matches native`,
    );
  }
}

// (b) raw frame byte comparison, if the native dump exists.
const rawDir = join(root, "build", "raw");
const rawFrames = [0, 100, 250];
let compared = 0;
for (let index = 0; index < atlasCount; ++index) {
  for (const frame of rawFrames) {
    const path = join(
      rawDir,
      `${names[index]}_f${String(frame).padStart(3, "0")}.bin`,
    );
    if (!existsSync(path)) {
      continue;
    }
    const native = readFileSync(path);
    const pointer = module_._sprite_wasm_scenario_frame(index, frame);
    const wasmBytes = Buffer.from(
      module_.HEAPU8.buffer,
      pointer,
      frameBytes,
    );
    if (native.length !== frameBytes) {
      console.log(`FAIL ${path}: native dump has wrong size`);
      ++failures;
      continue;
    }
    if (!native.equals(wasmBytes)) {
      let first = -1;
      for (let byte = 0; byte < frameBytes; ++byte) {
        if (native[byte] !== wasmBytes[byte]) {
          first = byte;
          break;
        }
      }
      console.log(
        `FAIL ${names[index]} frame ${frame}: bytes differ at ` +
          `offset ${first}`,
      );
      ++failures;
    } else {
      ++compared;
    }
  }
}
console.log(
  compared > 0
    ? `ok   ${compared} raw frames byte-identical to native`
    : "note: no raw native dumps found " +
        "(run build/test_sprite_face --raw build/raw)",
);

if (failures > 0) {
  console.log(`\n${failures} FAILURE(S)`);
  process.exit(1);
}
console.log("\nOK: WASM output is byte-identical to native");
