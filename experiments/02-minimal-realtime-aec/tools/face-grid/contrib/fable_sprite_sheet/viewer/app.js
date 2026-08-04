// Interactive viewer for the FSPR sprite-face engine. Drives the
// exact WASM build the byte-identical check verifies.
import createSpriteFaceModule from "../build/wasm/sprite-face.mjs";

const module_ = await createSpriteFaceModule();

const canvas = document.getElementById("screen");
const context = canvas.getContext("2d");
const width = module_._sprite_wasm_frame_width();
const height = module_._sprite_wasm_frame_height();
const image = context.createImageData(width, height);

const atlasSelect = document.getElementById("atlas");
const atlasCount = module_._sprite_wasm_atlas_count();
for (let index = 0; index < atlasCount; ++index) {
  const option = document.createElement("option");
  option.value = String(index);
  option.textContent = module_.UTF8ToString(
    module_._sprite_wasm_atlas_name(index),
  );
  atlasSelect.appendChild(option);
}
atlasSelect.addEventListener("change", () => {
  module_._sprite_wasm_select(Number(atlasSelect.value));
});
module_._sprite_wasm_select(0);

const keyframePointer = module_._malloc(12);
const sliders = [
  "mouth_open", "mouth_width", "mouth_round", "mouth_press",
  "mouth_teeth", "eye_left_open", "eye_right_open",
  "look_x", "look_y", "brow", "expression",
];
const element = (id) => document.getElementById(id);

const FLAG_SPEAKING = 1;
const FLAG_BLINKING = 2;

function triangle(t, period) {
  const phase = t % period;
  const half = period / 2;
  return phase <= half
    ? Math.round((phase * 255) / half)
    : Math.round(((period - phase) * 255) / (period - half));
}

let frame = 0;
function currentKeyframe() {
  const values = {};
  for (const name of sliders) {
    values[name] = Number(element(name).value);
  }
  let flags = 0;
  if (element("speaking").checked) flags |= FLAG_SPEAKING;
  if (element("blinking").checked) flags |= FLAG_BLINKING;
  if (element("talk").checked) {
    values.mouth_open = triangle(frame * 5, 44);
    values.mouth_width = triangle(frame * 3 + 9, 60);
    values.mouth_round = triangle(frame * 2 + 30, 90);
    values.mouth_teeth = triangle(frame * 7 + 100, 210);
    values.mouth_press = triangle(frame * 11, 260);
    flags |= FLAG_SPEAKING;
  }
  const bytes = new Uint8Array(12);
  bytes[0] = values.mouth_open;
  bytes[1] = values.mouth_width;
  bytes[2] = values.mouth_round;
  bytes[3] = values.mouth_press;
  bytes[4] = values.mouth_teeth;
  bytes[5] = values.eye_left_open;
  bytes[6] = values.eye_right_open;
  bytes[7] = values.look_x & 0xff;
  bytes[8] = values.look_y & 0xff;
  bytes[9] = values.brow & 0xff;
  bytes[10] = values.expression;
  bytes[11] = flags;
  return bytes;
}

function draw() {
  const bytes = currentKeyframe();
  module_.HEAPU8.set(bytes, keyframePointer);
  const clock = Math.floor((frame * 16000) / 30);
  const pointer = module_._sprite_wasm_render(keyframePointer, clock);
  if (pointer !== 0) {
    const pixels = new Uint16Array(
      module_.HEAPU8.buffer, pointer, width * height,
    );
    for (let index = 0; index < pixels.length; ++index) {
      const value = pixels[index];
      image.data[index * 4] = ((value >> 11) & 31) * 255 / 31;
      image.data[index * 4 + 1] = ((value >> 5) & 63) * 255 / 63;
      image.data[index * 4 + 2] = (value & 31) * 255 / 31;
      image.data[index * 4 + 3] = 255;
    }
    context.putImageData(image, 0, 0);
  }
  ++frame;
}

setInterval(draw, 1000 / 30);
