import createStackchanFaceModule from "./stackchan-face.mjs";

const SAMPLE_RATE = 16_000;
const WINDOW_SAMPLES = 160;
const RENDER_WIDTH = 160;
const RENDER_HEIGHT = 120;
const RENDER_PIXELS = RENDER_WIDTH * RENDER_HEIGHT;
const RENDER_INTERVAL_MS = 1000 / 30;
const RENDER_KEY_BYTES = 40;
const RENDER_KEY_MOUTH_OFFSET = 0;
const RENDER_KEY_ACTIVITY_OFFSET = 10;
const RENDER_KEY_FLAGS_OFFSET = 11;
const RENDER_KEY_VISEME_OFFSET = 12;
const RENDER_KEY_PHONEME_OFFSET = 13;
const RENDER_KEY_VISEME_WEIGHT_OFFSET = 14;
const RENDER_KEY_VISEME_BLEND_OFFSET = 18;
const RENDER_KEY_SPEECH_PHASE_OFFSET = 19;
const RENDER_KEY_SPEAKING_FLAG = 1;
const FACE_VISEME_SIL = 14;
const FACE_PHONEME_NONE = 255;
const FACE_SPEECH_IDLE = 0;
const FACE_SPEECH_ACTIVE = 2;
const METRICS_BYTES = 16;
const STAGE_CUE_BYTES = 32;
const MATRIX_FRAME_BUDGET_MS = 22;
const REVIEW_PAGE_SIZE = 12;
const FAVORITES_STORAGE_KEY = "stackchan.face-grid.favorites.v1";
const FAVORITES_SEED_REVISION_KEY =
  "stackchan.face-grid.favorites.seed-revision";
const FAVORITES_SEED_REVISION = 8;
const DENSITY_LAYOUTS = {
  matrix: { contactColumns: 10, tileWidth: 40, tileHeight: 30 },
  overview: { contactColumns: 16, tileWidth: 40, tileHeight: 30 },
  atlas: { contactColumns: 31, tileWidth: 20, tileHeight: 15 },
};

const ANALYSERS = new Map([
  [0, { name: "Envelope · lively", family: "envelope" }],
  [1, { name: "Envelope · smooth", family: "envelope" }],
  [2, { name: "Acoustic viseme · responsive", family: "viseme" }],
  [3, { name: "Acoustic viseme · smooth", family: "viseme" }],
  [4, { name: "Goertzel formants · responsive", family: "spectral" }],
  [5, { name: "Goertzel formants · smooth", family: "spectral" }],
]);
const ANALYSER_ORDER = [2, 3, 4, 5, 0, 1];

const VISEME_NAMES = [
  "aa",
  "E",
  "ih",
  "oh",
  "ou",
  "PP",
  "SS",
  "TH",
  "DD",
  "FF",
  "kk",
  "nn",
  "RR",
  "CH",
  "sil",
];

const MOUTH_KIND_NAMES = [
  "no mouth",
  "sprite mouth",
  "ellipse mouth",
  "polygon mouth",
  "line mouth",
  "segments",
  "distance field",
];

const STAGES = {
  neutral: {
    expression: 0,
    gesture: 0,
    gaze: 1,
    valence: 0,
    arousal: 72,
    dominance: 128,
  },
  warm: {
    expression: 1,
    gesture: 1,
    gaze: 1,
    valence: 62,
    arousal: 130,
    dominance: 142,
  },
  joy: {
    expression: 2,
    gesture: 5,
    gaze: 1,
    valence: 105,
    arousal: 190,
    dominance: 150,
    loop: true,
  },
  concern: {
    expression: 3,
    gesture: 0,
    gaze: 1,
    valence: -58,
    arousal: 122,
    dominance: 92,
  },
  surprise: {
    expression: 4,
    gesture: 3,
    gaze: 1,
    valence: 16,
    arousal: 238,
    dominance: 112,
  },
  thoughtful: {
    expression: 5,
    gesture: 3,
    gaze: 6,
    valence: 6,
    arousal: 88,
    dominance: 136,
  },
  skeptical: {
    expression: 6,
    gesture: 3,
    gaze: 6,
    valence: -24,
    arousal: 104,
    dominance: 170,
  },
  determined: {
    expression: 7,
    gesture: 4,
    gaze: 1,
    valence: 18,
    arousal: 164,
    dominance: 232,
  },
  sleepy: {
    expression: 8,
    gesture: 0,
    gaze: 5,
    valence: 8,
    arousal: 32,
    dominance: 76,
  },
  excited: {
    expression: 9,
    gesture: 5,
    gaze: 1,
    valence: 96,
    arousal: 248,
    dominance: 190,
    loop: true,
  },
  embarrassed: {
    expression: 10,
    gesture: 3,
    gaze: 6,
    valence: 28,
    arousal: 174,
    dominance: 72,
  },
};
const STAGE_ORDER = Object.keys(STAGES);
const STAGE_LABELS = {
  neutral: "Neutral / attentive",
  warm: "Warm · small nod",
  joy: "Joyful · buoyant",
  concern: "Concerned · stay with user",
  surprise: "Surprised · alert",
  thoughtful: "Thoughtful · glance aside",
  skeptical: "Sceptical · asymmetric tilt",
  determined: "Determined · focus",
  sleepy: "Sleepy · low arousal",
  excited: "Excited · bounce",
  embarrassed: "Embarrassed · glance away",
};
const STAGE_SHORT_LABELS = {
  neutral: "Neutral",
  warm: "Warm",
  joy: "Joy",
  concern: "Concern",
  surprise: "Surprise",
  thoughtful: "Thoughtful",
  skeptical: "Sceptical",
  determined: "Determined",
  sleepy: "Sleepy",
  excited: "Excited",
  embarrassed: "Embarrassed",
};
const REVIEW_ACTIVITIES = {
  auto: null,
  idle: 0,
  listening: 1,
  thinking: 2,
  speaking: 3,
};
const REVIEW_ACTIVITY_LABELS = {
  auto: "Auto activity",
  idle: "Idle",
  listening: "Listening",
  thinking: "Thinking",
  speaking: "Speaking",
};

const sourceNode = document.querySelector("#source");
const playNode = document.querySelector("#play");
const restartNode = document.querySelector("#restart");
const microphoneNode = document.querySelector("#microphone");
const fileNode = document.querySelector("#file");
const loopNode = document.querySelector("#loop");
const reviewAnalyserNode = document.querySelector("#review-analyser");
const reviewStageNode = document.querySelector("#review-stage");
const reviewActivityNode = document.querySelector("#review-activity");
const reviewGridNode = document.querySelector("#review-grid");
const reviewPreviousNode = document.querySelector("#review-previous");
const reviewNextNode = document.querySelector("#review-next");
const reviewPageNode = document.querySelector("#review-page");
const reviewFavoritesNode = document.querySelector("#review-favorites");
const reviewFavoriteCountNode = document.querySelector(
  "#review-favorite-count",
);
const statusNode = document.querySelector("#runtime-status");
const detailNode = document.querySelector("#runtime-detail");
const runtimeStateNode = document.querySelector(".runtime-state");
const clockNode = document.querySelector("#clock");
const sampleClockNode = document.querySelector("#sample-clock");
const metricsNode = document.querySelector("#runtime-metrics");
const fpsNode = document.querySelector("#fps");
const renderMsNode = document.querySelector("#render-ms");
const visibleCountNode = document.querySelector("#visible-count");
const waveformNode = document.querySelector("#waveform");
const waveformContext = waveformNode.getContext("2d");
const gridNode = document.querySelector("#grid");
const rendererLegendNode = document.querySelector("#renderer-legend");
const rendererCountNode = document.querySelector("#renderer-count");
const combinationCountNode = document.querySelector("#combination-count");
const densityControlNode = document.querySelector(".density-control");
const inspectorNode = document.querySelector("#tile-inspector");
const inspectorFaceNode = document.querySelector("#inspector-face");
const inspectorFaceContext = inspectorFaceNode.getContext("2d", {
  alpha: false,
  desynchronized: true,
});
const inspectorContextNode = document.querySelector("#inspector-context");
const inspectorTitleNode = document.querySelector("#inspector-title");
const inspectorScenarioNode = document.querySelector("#inspector-scenario");
const inspectorRendererNode = document.querySelector("#inspector-renderer");
const inspectorFamilyNode = document.querySelector("#inspector-family");
const inspectorMouthNode = document.querySelector("#inspector-mouth");
const inspectorTierNode = document.querySelector("#inspector-tier");
const inspectorStreamNode = document.querySelector("#inspector-stream");
const inspectorPositionNode = document.querySelector("#inspector-position");
const inspectorPreviousNode = document.querySelector("#inspector-previous");
const inspectorNextNode = document.querySelector("#inspector-next");

let wasm;
let modelPointer = 0;
let modelBytes = 0;
let pcmPointer = 0;
let renderKeyPointer = 0;
let framebufferPointer = 0;
let profileInfoPointer = 0;
let analysers = new Map();
let rendererProfiles = [];
let scenarioTiles = [];
let reviewTiles = [];
let reviewPage = 0;
let reviewFavoritesOnly = false;
let favoriteSlugs = loadFavoriteSlugs();
let reviewRenderMs = 0;
let stageCuePointers = new Map();
let audioContext;
let transport;
let activePcm;
let microphoneSession = null;
let inputSessionGeneration = 0;
let renderClock = 0;
let lastRenderTimestamp = 0;
let schedulerHandle = 0;
let latestMetrics = new Map();
let rgbaLookup;
let framebufferWords;
let framebufferBuffer;
let statsWindowStarted = performance.now();
let statsFrames = 0;
let measuredFps = 0;
let matrixSweepMs = 0;
let worstProfile = "—";
let firstFrameRendered = false;
let scenarioCursor = 0;
let sweepStartedAt = performance.now();
let scenariosSeenInSweep = new Set();
let priorityScenario = null;
let density = "overview";
let contactColumns = DENSITY_LAYOUTS.overview.contactColumns;
let contactTileWidth = DENSITY_LAYOUTS.overview.tileWidth;
let contactTileHeight = DENSITY_LAYOUTS.overview.tileHeight;
let inspectorScenario = null;
let inspectorRendererIndex = 0;
let inspectorImageData = inspectorFaceContext.createImageData(
  RENDER_WIDTH,
  RENDER_HEIGHT,
);
const inspectorRgba = new Uint32Array(inspectorImageData.data.buffer);

class WasmAnalyser {
  constructor(profile) {
    this.profile = profile;
    this.keyPointer = wasm._malloc(RENDER_KEY_BYTES);
    this.metricsPointer = wasm._malloc(METRICS_BYTES);
    this.handle = wasm._stackchan_wasm_create(
      profile,
      SAMPLE_RATE,
      modelPointer,
      modelBytes,
    );
    if (!this.handle) {
      wasm._free(this.metricsPointer);
      wasm._free(this.keyPointer);
      throw new Error(`C analyser rejected profile ${profile}`);
    }
  }

  push(samples) {
    let offset = 0;
    while (offset < samples.length) {
      const count = Math.min(WINDOW_SAMPLES, samples.length - offset);
      wasm.HEAP16.set(
        samples.subarray(offset, offset + count),
        pcmPointer >> 1,
      );
      wasm._stackchan_wasm_push_pcm(this.handle, pcmPointer, count);
      offset += count;
    }
  }

  snapshot() {
    if (
      !wasm._stackchan_wasm_snapshot(
        this.handle,
        this.keyPointer,
        this.metricsPointer,
      )
    ) {
      throw new Error("Unable to snapshot facial-action frame");
    }
    const view = new DataView(
      wasm.HEAPU8.buffer,
      this.metricsPointer,
      METRICS_BYTES,
    );
    const viseme = view.getUint8(10);
    return {
      frameIndex: view.getUint32(0, true),
      playoutSamples: view.getUint32(4, true),
      level: view.getUint16(8, true),
      viseme,
      visemeName: viseme < VISEME_NAMES.length ? VISEME_NAMES[viseme] : "—",
      confidence: view.getUint8(11),
      stateBytes: view.getUint16(12, true),
      modelBytes: view.getUint16(14, true),
    };
  }

  destroy() {
    if (this.handle) {
      wasm._stackchan_wasm_destroy(this.handle);
      this.handle = 0;
    }
    if (this.metricsPointer) {
      wasm._free(this.metricsPointer);
      this.metricsPointer = 0;
    }
    if (this.keyPointer) {
      wasm._free(this.keyPointer);
      this.keyPointer = 0;
    }
  }
}

class SharedPcmTransport {
  constructor(context, pcm, loop = true) {
    this.context = context;
    this.pcm = pcm;
    this.loop = loop;
    this.cursor = 0;
    this.anchorSample = 0;
    this.anchorTime = 0;
    this.playing = false;
    this.source = null;
    this.animation = 0;
    this.finishedSilence = 0;
    this.operationGeneration = 0;
  }

  async play() {
    if (this.playing) {
      this.pause();
      return;
    }
    const operationGeneration = ++this.operationGeneration;
    await this.context.resume();
    if (operationGeneration !== this.operationGeneration) {
      return;
    }
    if (this.cursor >= this.pcm.length) {
      this.cursor = 0;
    }
    if (this.cursor === 0) {
      resetAnalysers();
      renderClock = 0;
    }
    const buffer = this.context.createBuffer(
      1,
      this.pcm.length - this.cursor,
      SAMPLE_RATE,
    );
    const channel = buffer.getChannelData(0);
    for (let index = this.cursor; index < this.pcm.length; index += 1) {
      channel[index - this.cursor] = this.pcm[index] / 32768;
    }
    this.source = this.context.createBufferSource();
    this.source.buffer = buffer;
    this.source.connect(this.context.destination);
    this.anchorSample = this.cursor;
    this.anchorTime = this.context.currentTime + 0.045;
    this.source.start(this.anchorTime);
    this.playing = true;
    playNode.textContent = "Pause";
    statusNode.textContent = "Playing one PCM stream through every renderer";
    this.tick();
  }

  pause() {
    this.operationGeneration += 1;
    this.playing = false;
    try {
      this.source?.stop();
    } catch {
      // A source that naturally ended is already stopped.
    }
    this.source?.disconnect();
    this.source = null;
    cancelAnimationFrame(this.animation);
    playNode.textContent = "Resume";
    statusNode.textContent = "Paused · procedural idle motion continues";
  }

  restart() {
    this.pause();
    this.cursor = 0;
    this.finishedSilence = 0;
    resetAnalysers();
    renderClock = 0;
    updateClock(0);
    drawWaveform(this.pcm, 0);
    void this.play();
  }

  tick = () => {
    if (!this.playing) {
      return;
    }
    const elapsed = Math.max(0, this.context.currentTime - this.anchorTime);
    const target = Math.min(
      this.pcm.length,
      this.anchorSample + Math.floor(elapsed * SAMPLE_RATE),
    );
    feedUntil(target);
    drawWaveform(this.pcm, this.cursor);
    if (this.cursor >= this.pcm.length) {
      if (this.finishedSilence < SAMPLE_RATE / 3) {
        const silence = new Int16Array(WINDOW_SAMPLES);
        for (const analyserInstance of analysers.values()) {
          analyserInstance.push(silence);
        }
        this.finishedSilence += WINDOW_SAMPLES;
        this.animation = requestAnimationFrame(this.tick);
        return;
      }
      this.playing = false;
      this.source?.disconnect();
      this.source = null;
      if (this.loop) {
        this.cursor = 0;
        this.finishedSilence = 0;
        playNode.textContent = "Pause";
        statusNode.textContent = "Looping PCM · restarting from sample zero";
        void this.play();
        return;
      }
      playNode.textContent = "Replay";
      statusNode.textContent = "Playback complete · idle performance continues";
      return;
    }
    this.animation = requestAnimationFrame(this.tick);
  };
}

class StreamingDownsampler {
  constructor(inputRate, outputRate) {
    this.ratio = inputRate / outputRate;
    this.position = 0;
    this.previous = 0;
    this.started = false;
  }

  process(input) {
    if (!input.length) {
      return new Int16Array();
    }
    if (!this.started) {
      this.previous = input[0];
      this.started = true;
    }
    const extended = new Float32Array(input.length + 1);
    extended[0] = this.previous;
    extended.set(input, 1);
    const output = [];
    while (this.position < extended.length - 1) {
      const left = Math.floor(this.position);
      const fraction = this.position - left;
      const sample =
        extended[left] + (extended[left + 1] - extended[left]) * fraction;
      output.push(
        Math.max(-32768, Math.min(32767, Math.round(sample * 32767))),
      );
      this.position += this.ratio;
    }
    this.position -= extended.length - 1;
    this.previous = input[input.length - 1];
    return Int16Array.from(output);
  }
}

function readCString(pointer) {
  if (!pointer) {
    return "";
  }
  const bytes = wasm.HEAPU8;
  let end = pointer;
  while (bytes[end] !== 0) {
    end += 1;
  }
  return new TextDecoder().decode(bytes.subarray(pointer, end));
}

function readProfileInfo(profile) {
  if (!wasm._stackchan_wasm_render_profile_info(profile, profileInfoPointer)) {
    throw new Error(`Missing renderer metadata for profile ${profile}`);
  }
  const view = new DataView(wasm.HEAPU8.buffer, profileInfoPointer, 16);
  return {
    width: view.getUint16(0, true),
    height: view.getUint16(2, true),
    workWidth: view.getUint16(4, true),
    workHeight: view.getUint16(6, true),
    framebufferBytes: view.getUint16(8, true),
    family: view.getUint8(10),
    mouthKind: view.getUint8(11),
    flags: view.getUint8(12),
    estimatedOpsPerPixel: view.getUint16(14, true),
  };
}

function profileReviewTier(profile) {
  return {
    id: "active",
    label: "Active sprite atlas",
  };
}

function createRendererCatalog() {
  const count = wasm._stackchan_wasm_render_profile_count();
  const availableProfiles = [];
  rendererLegendNode.replaceChildren();
  for (let profile = 0; profile < count; profile += 1) {
    const info = readProfileInfo(profile);
    const name = readCString(wasm._stackchan_wasm_render_profile_name(profile));
    const slug = readCString(wasm._stackchan_wasm_render_profile_slug(profile));
    const familyName = readCString(
      wasm._stackchan_wasm_render_profile_family_name(profile),
    );
    const reviewTier = profileReviewTier(profile);
    availableProfiles.push({
      profile,
      name,
      slug,
      info,
      familyName,
      reviewTier,
      workbenchOrder: profile,
    });
  }
  rendererProfiles = availableProfiles.sort(
    (left, right) => left.workbenchOrder - right.workbenchOrder,
  );
  for (const renderer of rendererProfiles) {
    const item = document.createElement("li");
    item.dataset.tier = renderer.reviewTier.id;
    item.textContent =
      `${String(renderer.profile).padStart(2, "0")} · ${renderer.name} · ` +
      `${renderer.familyName} · ` +
      `${MOUTH_KIND_NAMES[renderer.info.mouthKind] ?? "custom"} · ` +
      `${renderer.reviewTier.label}`;
    rendererLegendNode.append(item);
  }
  rendererCountNode.textContent = String(rendererProfiles.length);
  combinationCountNode.textContent = (
    rendererProfiles.length *
    ANALYSER_ORDER.length *
    STAGE_ORDER.length
  ).toLocaleString();
}

function currentReviewScenario() {
  const analyserProfile = Number(reviewAnalyserNode.value);
  return {
    analyserProfile,
    analyserDefinition: ANALYSERS.get(analyserProfile),
    stageName: reviewStageNode.value,
    activityName: reviewActivityNode.value,
  };
}

function loadFavoriteSlugs() {
  try {
    const stored = localStorage.getItem(FAVORITES_STORAGE_KEY);
    if (stored === null) {
      return new Set();
    }
    const parsed = JSON.parse(stored);
    if (!Array.isArray(parsed)) {
      throw new Error("favourites are not an array");
    }
    const favorites = new Set(
      parsed.filter((slug) => typeof slug === "string"),
    );
    return favorites;
  } catch (error) {
    console.warn("Could not load persistent renderer favourites.", error);
    return new Set();
  }
}

function seedActiveSpriteFavorites() {
  const seedRevision = Number(
    localStorage.getItem(FAVORITES_SEED_REVISION_KEY) ?? 0,
  );
  if (seedRevision >= FAVORITES_SEED_REVISION) {
    return;
  }
  const availableSlugs = new Set(rendererProfiles.map(({ slug }) => slug));
  for (const renderer of rendererProfiles) {
    favoriteSlugs.add(renderer.slug);
  }
  for (const slug of favoriteSlugs) {
    if (!availableSlugs.has(slug)) {
      favoriteSlugs.delete(slug);
    }
  }
  persistFavoriteSlugs();
  localStorage.setItem(
    FAVORITES_SEED_REVISION_KEY,
    String(FAVORITES_SEED_REVISION),
  );
}

function persistFavoriteSlugs() {
  try {
    localStorage.setItem(
      FAVORITES_STORAGE_KEY,
      JSON.stringify([...favoriteSlugs].sort()),
    );
  } catch (error) {
    console.warn("Could not persist renderer favourites.", error);
  }
}

function reviewRendererIndexes() {
  return rendererProfiles.flatMap((renderer, rendererIndex) =>
    !reviewFavoritesOnly || favoriteSlugs.has(renderer.slug)
      ? [rendererIndex]
      : [],
  );
}

function updateFavoriteFilter() {
  const favoriteCount = rendererProfiles.filter((renderer) =>
    favoriteSlugs.has(renderer.slug),
  ).length;
  reviewFavoriteCountNode.textContent = String(favoriteCount);
  reviewFavoritesNode.setAttribute("aria-pressed", String(reviewFavoritesOnly));
  reviewFavoritesNode.classList.toggle("active", reviewFavoritesOnly);
  reviewFavoritesNode.title = reviewFavoritesOnly
    ? "Show every renderer"
    : "Show only saved favourites";
}

function toggleFavorite(slug) {
  if (favoriteSlugs.has(slug)) {
    favoriteSlugs.delete(slug);
  } else {
    favoriteSlugs.add(slug);
  }
  persistFavoriteSlugs();
  updateReviewPage(reviewPage);
}

function updateReviewPage(nextPage = reviewPage) {
  const visibleRendererIndexes = reviewRendererIndexes();
  const pageCount = Math.max(
    1,
    Math.ceil(visibleRendererIndexes.length / REVIEW_PAGE_SIZE),
  );
  reviewPage = Math.max(0, Math.min(pageCount - 1, nextPage));
  const firstRenderer = reviewPage * REVIEW_PAGE_SIZE;
  const lastRenderer = Math.min(
    visibleRendererIndexes.length,
    firstRenderer + REVIEW_PAGE_SIZE,
  );
  reviewTiles = [];
  reviewGridNode.replaceChildren();

  for (const rendererIndex of visibleRendererIndexes.slice(
    firstRenderer,
    lastRenderer,
  )) {
    const renderer = rendererProfiles[rendererIndex];
    const card = document.createElement("article");
    card.className = "review-card";
    const openButton = document.createElement("button");
    openButton.type = "button";
    openButton.className = "review-card-open";
    openButton.setAttribute(
      "aria-label",
      `${renderer.name}, renderer ${renderer.profile}. Open native inspector.`,
    );

    const canvas = document.createElement("canvas");
    canvas.width = RENDER_WIDTH;
    canvas.height = RENDER_HEIGHT;
    canvas.setAttribute("aria-hidden", "true");
    const context = canvas.getContext("2d", {
      alpha: false,
      desynchronized: true,
    });
    context.imageSmoothingEnabled = false;
    const imageData = context.createImageData(RENDER_WIDTH, RENDER_HEIGHT);
    const rgba = new Uint32Array(imageData.data.buffer);
    rgba.fill(0xff070502);

    const caption = document.createElement("span");
    caption.className = "review-card-caption";
    const title = document.createElement("span");
    title.className = "review-card-title";
    const name = document.createElement("strong");
    name.textContent = renderer.name;
    const family = document.createElement("small");
    family.textContent = renderer.familyName;
    title.append(name, family);
    const index = document.createElement("span");
    index.className = "review-card-index";
    index.textContent = String(renderer.profile).padStart(2, "0");
    openButton.append(canvas);

    const favoriteButton = document.createElement("button");
    favoriteButton.type = "button";
    favoriteButton.className = "review-card-favorite";
    const isFavorite = favoriteSlugs.has(renderer.slug);
    favoriteButton.classList.toggle("active", isFavorite);
    favoriteButton.setAttribute("aria-pressed", String(isFavorite));
    favoriteButton.setAttribute(
      "aria-label",
      `${isFavorite ? "Remove" : "Add"} ${renderer.name} ${
        isFavorite ? "from" : "to"
      } favourites`,
    );
    favoriteButton.textContent = isFavorite ? "★" : "☆";
    favoriteButton.addEventListener("click", () => {
      toggleFavorite(renderer.slug);
    });
    caption.append(title, index, favoriteButton);
    card.append(openButton, caption);

    const reviewTile = {
      rendererIndex,
      renderer,
      card,
      canvas,
      context,
      imageData,
      rgba,
    };
    openButton.addEventListener("click", () => {
      const reviewScenario = currentReviewScenario();
      const matrixScenario = scenarioTiles.find(
        (candidate) =>
          candidate.analyserProfile === reviewScenario.analyserProfile &&
          candidate.stageName === reviewScenario.stageName,
      );
      if (matrixScenario) {
        openInspector(
          {
            ...matrixScenario,
            activityName: reviewScenario.activityName,
          },
          rendererIndex,
        );
      }
    });
    reviewGridNode.append(card);
    reviewTiles.push(reviewTile);
  }

  if (!visibleRendererIndexes.length) {
    const empty = document.createElement("div");
    empty.className = "review-empty";
    empty.innerHTML =
      "<strong>No saved favourites yet.</strong>" +
      "<span>Switch back to all renderers, then star the faces you want here.</span>";
    reviewGridNode.append(empty);
  }

  const displayStart = visibleRendererIndexes.length ? firstRenderer + 1 : 0;
  const scopeLabel = reviewFavoritesOnly ? " favourites" : "";
  reviewPageNode.textContent =
    `Page ${reviewPage + 1} / ${pageCount} · ` +
    `${String(displayStart).padStart(2, "0")}–` +
    `${String(lastRenderer).padStart(2, "0")} of ` +
    `${visibleRendererIndexes.length}${scopeLabel}`;
  reviewPreviousNode.disabled = reviewPage === 0;
  reviewNextNode.disabled = reviewPage >= pageCount - 1;
  updateFavoriteFilter();
}

function createScenarioMatrix() {
  gridNode.replaceChildren();
  scenarioTiles = [];

  const corner = document.createElement("div");
  corner.className = "matrix-corner";
  corner.innerHTML =
    "<strong>PCM analyser ↓</strong><small>AI stage direction →</small>";
  gridNode.append(corner);

  for (const stageName of STAGE_ORDER) {
    const header = document.createElement("div");
    header.className = "stage-column-header";
    header.innerHTML =
      `<strong>${STAGE_SHORT_LABELS[stageName]}</strong>` +
      `<small>${STAGE_LABELS[stageName]}</small>`;
    gridNode.append(header);
  }

  for (const analyserProfile of ANALYSER_ORDER) {
    const analyserDefinition = ANALYSERS.get(analyserProfile);
    const rowHeader = document.createElement("div");
    rowHeader.className = "analyser-row-header";
    rowHeader.innerHTML =
      `<strong>${analyserDefinition.name}</strong>` +
      `<small>PCM profile ${analyserProfile} · all renderers</small>`;
    gridNode.append(rowHeader);

    for (const stageName of STAGE_ORDER) {
      const tile = document.createElement("button");
      tile.type = "button";
      tile.className = "scenario-card";
      tile.dataset.analyser = String(analyserProfile);
      tile.dataset.stage = stageName;
      tile.innerHTML = `
        <canvas aria-hidden="true"></canvas>
        <span class="scenario-probe">Move across the faces to identify one</span>
      `;
      tile.setAttribute(
        "aria-label",
        `${analyserDefinition.name}, ${STAGE_LABELS[stageName]}. ` +
          `${rendererProfiles.length} live C renderers. Click a face to inspect it.`,
      );
      const canvas = tile.querySelector("canvas");
      const probe = tile.querySelector(".scenario-probe");
      const context = canvas.getContext("2d", {
        alpha: false,
        desynchronized: true,
      });
      const scenario = {
        analyserProfile,
        analyserDefinition,
        stageName,
        tile,
        canvas,
        probe,
        context,
        imageData: null,
        rgba: null,
        width: 0,
        height: 0,
        hash: 0,
        hasVisibleFrame: false,
        renderMicros: 0,
        renders: 0,
      };
      configureScenarioCanvas(scenario);
      tile.addEventListener("mouseenter", () => {
        if (!inspectorNode.open) {
          priorityScenario = scenario;
        }
      });
      tile.addEventListener("mouseleave", () => {
        if (!inspectorNode.open && priorityScenario === scenario) {
          priorityScenario = null;
        }
      });
      tile.addEventListener("pointermove", (event) => {
        const rendererIndex = rendererIndexAtPoint(scenario, event);
        const renderer = rendererProfiles[rendererIndex];
        if (!renderer) {
          return;
        }
        probe.textContent =
          `${String(renderer.profile).padStart(2, "0")} · ` +
          `${renderer.name} · ${renderer.reviewTier.label}`;
      });
      tile.addEventListener("focus", () => {
        const renderer = rendererProfiles[0];
        probe.textContent =
          `${String(renderer.profile).padStart(2, "0")} · ` +
          `${renderer.name} · click to inspect`;
      });
      tile.addEventListener("click", (event) => {
        openInspector(
          scenario,
          event.detail > 0
            ? rendererIndexAtPoint(scenario, event)
            : inspectorRendererIndex,
        );
      });
      gridNode.append(tile);
      scenarioTiles.push(scenario);
    }
  }
}

function configureScenarioCanvas(scenario, previousLayout = null) {
  let previousCanvas = null;
  const canPreserve =
    previousLayout !== null &&
    scenario.hasVisibleFrame === true &&
    scenario.width > 0 &&
    scenario.height > 0;
  if (canPreserve) {
    previousCanvas = document.createElement("canvas");
    previousCanvas.width = scenario.width;
    previousCanvas.height = scenario.height;
    const previousContext = previousCanvas.getContext("2d", {
      alpha: false,
    });
    previousContext.imageSmoothingEnabled = false;
    previousContext.drawImage(scenario.canvas, 0, 0);
  }

  const contactRows = Math.ceil(rendererProfiles.length / contactColumns);
  scenario.width = contactColumns * contactTileWidth;
  scenario.height = contactRows * contactTileHeight;
  scenario.contactColumns = contactColumns;
  scenario.contactTileWidth = contactTileWidth;
  scenario.contactTileHeight = contactTileHeight;
  scenario.canvas.width = scenario.width;
  scenario.canvas.height = scenario.height;
  scenario.context.imageSmoothingEnabled = false;
  scenario.context.fillStyle = "#020507";
  scenario.context.fillRect(0, 0, scenario.width, scenario.height);
  if (previousCanvas) {
    for (
      let rendererIndex = 0;
      rendererIndex < rendererProfiles.length;
      rendererIndex += 1
    ) {
      const sourceX =
        (rendererIndex % previousLayout.contactColumns) *
        previousLayout.tileWidth;
      const sourceY =
        Math.floor(rendererIndex / previousLayout.contactColumns) *
        previousLayout.tileHeight;
      const destinationX = (rendererIndex % contactColumns) * contactTileWidth;
      const destinationY =
        Math.floor(rendererIndex / contactColumns) * contactTileHeight;
      scenario.context.drawImage(
        previousCanvas,
        sourceX,
        sourceY,
        previousLayout.tileWidth,
        previousLayout.tileHeight,
        destinationX,
        destinationY,
        contactTileWidth,
        contactTileHeight,
      );
    }
  }
  scenario.imageData = scenario.context.createImageData(
    scenario.width,
    scenario.height,
  );
  scenario.rgba = new Uint32Array(scenario.imageData.data.buffer);
  scenario.rgba.fill(0xff070502);
  scenario.hash = 0;
}

function setDensity(nextDensity) {
  const layout = DENSITY_LAYOUTS[nextDensity];
  if (!layout || nextDensity === density) {
    return;
  }
  const previousLayout = {
    contactColumns,
    tileWidth: contactTileWidth,
    tileHeight: contactTileHeight,
  };
  density = nextDensity;
  contactColumns = layout.contactColumns;
  contactTileWidth = layout.tileWidth;
  contactTileHeight = layout.tileHeight;
  gridNode.classList.remove(
    "density-matrix",
    "density-overview",
    "density-atlas",
  );
  gridNode.classList.add(`density-${density}`);
  for (const scenario of scenarioTiles) {
    configureScenarioCanvas(scenario, previousLayout);
  }
  scenarioCursor = 0;
  scenariosSeenInSweep.clear();
  sweepStartedAt = performance.now();
  firstFrameRendered = false;
  runtimeStateNode.classList.remove("ready");
  statusNode.textContent = "Reflowing live renderer matrix…";
  detailNode.textContent =
    "Previous frames remain visible while the new layout hydrates";
  window.__STACKCHAN_GRID_READY__ = false;
}

function rendererIndexAtPoint(scenario, event) {
  const bounds = scenario.canvas.getBoundingClientRect();
  if (!bounds.width || !bounds.height) {
    return 0;
  }
  const x = Math.max(
    0,
    Math.min(bounds.width - 0.001, event.clientX - bounds.left),
  );
  const y = Math.max(
    0,
    Math.min(bounds.height - 0.001, event.clientY - bounds.top),
  );
  const column = Math.floor((x / bounds.width) * scenario.contactColumns);
  const row = Math.floor(
    (y / bounds.height) *
      Math.ceil(rendererProfiles.length / scenario.contactColumns),
  );
  return Math.max(
    0,
    Math.min(
      rendererProfiles.length - 1,
      row * scenario.contactColumns + column,
    ),
  );
}

function createRgbaLookup() {
  const table = new Uint32Array(65536);
  for (let value = 0; value < 65536; value += 1) {
    const red5 = (value >> 11) & 0x1f;
    const green6 = (value >> 5) & 0x3f;
    const blue5 = value & 0x1f;
    const red = (red5 << 3) | (red5 >> 2);
    const green = (green6 << 2) | (green6 >> 4);
    const blue = (blue5 << 3) | (blue5 >> 2);
    table[value] = red | (green << 8) | (blue << 16) | 0xff000000;
  }
  return table;
}

function refreshFramebufferView() {
  if (framebufferBuffer !== wasm.HEAPU8.buffer) {
    framebufferBuffer = wasm.HEAPU8.buffer;
    framebufferWords = new Uint16Array(
      framebufferBuffer,
      framebufferPointer,
      RENDER_PIXELS,
    );
  }
}

function copyRendererIntoContactSheet(scenario, profileIndex, hash) {
  const destinationX =
    (profileIndex % scenario.contactColumns) * scenario.contactTileWidth;
  const destinationY =
    Math.floor(profileIndex / scenario.contactColumns) *
    scenario.contactTileHeight;
  for (let y = 0; y < scenario.contactTileHeight; y += 1) {
    const sourceY = Math.floor(
      (y * RENDER_HEIGHT) / scenario.contactTileHeight,
    );
    const sourceRow = sourceY * RENDER_WIDTH;
    const destinationRow = (destinationY + y) * scenario.width + destinationX;
    for (let x = 0; x < scenario.contactTileWidth; x += 1) {
      const sourceX = Math.floor(
        (x * RENDER_WIDTH) / scenario.contactTileWidth,
      );
      const pixel = framebufferWords[sourceRow + sourceX];
      scenario.rgba[destinationRow + x] = rgbaLookup[pixel];
      if ((x & 7) === 0 && (y & 3) === 0) {
        hash ^= pixel;
        hash = Math.imul(hash, 16777619);
      }
    }
  }
  return hash;
}

function copyFramebufferIntoInspector() {
  for (let pixel = 0; pixel < RENDER_PIXELS; pixel += 1) {
    inspectorRgba[pixel] = rgbaLookup[framebufferWords[pixel]];
  }
  inspectorFaceContext.putImageData(inspectorImageData, 0, 0);
}

function copyFramebufferIntoReviewTile(reviewTile) {
  for (let pixel = 0; pixel < RENDER_PIXELS; pixel += 1) {
    reviewTile.rgba[pixel] = rgbaLookup[framebufferWords[pixel]];
  }
  reviewTile.context.putImageData(reviewTile.imageData, 0, 0);
}

function applyActivityOverride(activityName) {
  const activity = REVIEW_ACTIVITIES[activityName];
  if (activity === null || activity === undefined) {
    return;
  }
  const key = wasm.HEAPU8;
  key[renderKeyPointer + RENDER_KEY_ACTIVITY_OFFSET] = activity;
  if (activity === REVIEW_ACTIVITIES.speaking) {
    key[renderKeyPointer + RENDER_KEY_FLAGS_OFFSET] |=
      RENDER_KEY_SPEAKING_FLAG;
    key[renderKeyPointer + RENDER_KEY_SPEECH_PHASE_OFFSET] =
      FACE_SPEECH_ACTIVE;
    return;
  }

  key[renderKeyPointer + RENDER_KEY_FLAGS_OFFSET] &=
    ~RENDER_KEY_SPEAKING_FLAG;
  key.fill(
    0,
    renderKeyPointer + RENDER_KEY_MOUTH_OFFSET,
    renderKeyPointer + RENDER_KEY_MOUTH_OFFSET + 5,
  );
  key[renderKeyPointer + RENDER_KEY_VISEME_OFFSET] = FACE_VISEME_SIL;
  key[renderKeyPointer + RENDER_KEY_PHONEME_OFFSET] = FACE_PHONEME_NONE;
  key[renderKeyPointer + RENDER_KEY_VISEME_WEIGHT_OFFSET] = 0;
  key[renderKeyPointer + RENDER_KEY_VISEME_BLEND_OFFSET] = 0;
  key[renderKeyPointer + RENDER_KEY_SPEECH_PHASE_OFFSET] = FACE_SPEECH_IDLE;
}

function prepareRenderKey(
  analyserProfile,
  stageName,
  sampleClock,
  activityName = "auto",
) {
  const sourceAnalyser = analysers.get(analyserProfile);
  if (!sourceAnalyser) {
    return false;
  }
  wasm.HEAPU8.set(
    wasm.HEAPU8.subarray(
      sourceAnalyser.keyPointer,
      sourceAnalyser.keyPointer + RENDER_KEY_BYTES,
    ),
    renderKeyPointer,
  );
  wasm._stackchan_wasm_apply_stage_cue(
    stageCuePointers.get(stageName),
    sampleClock >>> 0,
    renderKeyPointer,
  );
  applyActivityOverride(activityName);
  return true;
}

function updateInspectorLabels() {
  const renderer = rendererProfiles[inspectorRendererIndex];
  if (!renderer || !inspectorScenario) {
    return;
  }
  const analyserName = inspectorScenario.analyserDefinition.name;
  const stageName = STAGE_LABELS[inspectorScenario.stageName];
  const activityName = inspectorScenario.activityName ?? "auto";
  const activityLabel = REVIEW_ACTIVITY_LABELS[activityName];
  inspectorContextNode.textContent =
    `${analyserName} × ${STAGE_SHORT_LABELS[inspectorScenario.stageName]} × ` +
    activityLabel;
  inspectorTitleNode.textContent = renderer.name;
  inspectorScenarioNode.textContent =
    `${stageName}; ${activityLabel.toLowerCase()}. Same live PCM, ` +
    `facial-action IR, and sample clock as the complete matrix behind this ` +
    `inspector.`;
  inspectorRendererNode.textContent = `${String(renderer.profile).padStart(2, "0")} · ${renderer.slug}`;
  inspectorFamilyNode.textContent = renderer.familyName;
  inspectorMouthNode.textContent =
    MOUTH_KIND_NAMES[renderer.info.mouthKind] ?? "Custom";
  inspectorTierNode.textContent = renderer.reviewTier.label;
  inspectorStreamNode.textContent =
    `${analyserName} · ${STAGE_SHORT_LABELS[inspectorScenario.stageName]} · ` +
    activityLabel;
  inspectorPositionNode.textContent = `${inspectorRendererIndex + 1} / ${rendererProfiles.length}`;
}

function selectInspectorRenderer(rendererIndex) {
  if (!rendererProfiles.length) {
    return;
  }
  inspectorRendererIndex =
    (rendererIndex + rendererProfiles.length) % rendererProfiles.length;
  updateInspectorLabels();
}

function openInspector(scenario, rendererIndex) {
  inspectorScenario = scenario;
  priorityScenario = scenario;
  selectInspectorRenderer(rendererIndex);
  if (!inspectorNode.open) {
    inspectorNode.showModal();
  }
}

function renderScenario(scenario, sampleClock) {
  const started = performance.now();
  if (
    !prepareRenderKey(
      scenario.analyserProfile,
      scenario.stageName,
      sampleClock,
      scenario.activityName,
    )
  ) {
    return;
  }
  let hash = 2166136261;
  let slowest = null;
  for (
    let rendererIndex = 0;
    rendererIndex < rendererProfiles.length;
    rendererIndex += 1
  ) {
    const renderer = rendererProfiles[rendererIndex];
    const rendererStarted = performance.now();
    if (
      !wasm._stackchan_wasm_render(
        renderer.profile,
        renderKeyPointer,
        sampleClock >>> 0,
        framebufferPointer,
        RENDER_PIXELS,
      )
    ) {
      throw new Error(`Renderer ${renderer.slug} rejected its frame`);
    }
    refreshFramebufferView();
    hash = copyRendererIntoContactSheet(scenario, rendererIndex, hash);
    if (
      inspectorNode.open &&
      inspectorScenario === scenario &&
      inspectorRendererIndex === rendererIndex
    ) {
      copyFramebufferIntoInspector();
    }
    const renderMicros = (performance.now() - rendererStarted) * 1000;
    if (!slowest || renderMicros > slowest.renderMicros) {
      slowest = { name: renderer.name, renderMicros };
    }
  }
  scenario.context.putImageData(scenario.imageData, 0, 0);
  scenario.hash = hash >>> 0;
  scenario.hasVisibleFrame = true;
  scenario.renderMicros = (performance.now() - started) * 1000;
  scenario.renders += 1;
  if (slowest) {
    worstProfile = slowest.name;
  }
}

function renderReviewRoom(sampleClock) {
  const started = performance.now();
  const reviewScenario = currentReviewScenario();
  if (
    !prepareRenderKey(
      reviewScenario.analyserProfile,
      reviewScenario.stageName,
      sampleClock,
      reviewScenario.activityName,
    )
  ) {
    return;
  }
  for (const reviewTile of reviewTiles) {
    if (!renderProfileFrame(reviewTile.renderer.profile, sampleClock)) {
      throw new Error(
        `Renderer ${reviewTile.renderer.slug} rejected review frame`,
      );
    }
    copyFramebufferIntoReviewTile(reviewTile);
    if (
      inspectorNode.open &&
      inspectorRendererIndex === reviewTile.rendererIndex &&
      inspectorScenario?.analyserProfile === reviewScenario.analyserProfile &&
      inspectorScenario?.stageName === reviewScenario.stageName &&
      (inspectorScenario?.activityName ?? "auto") ===
        reviewScenario.activityName
    ) {
      copyFramebufferIntoInspector();
    }
  }
  reviewRenderMs = performance.now() - started;
}

function renderProfileFrame(profile, sampleClock) {
  if (
    !wasm._stackchan_wasm_render(
      profile,
      renderKeyPointer,
      sampleClock >>> 0,
      framebufferPointer,
      RENDER_PIXELS,
    )
  ) {
    return false;
  }
  refreshFramebufferView();
  return true;
}

function writeStageCue(pointer, name, startClock = 0) {
  const stage = STAGES[name] ?? STAGES.neutral;
  const view = new DataView(wasm.HEAPU8.buffer, pointer, STAGE_CUE_BYTES);
  for (let offset = 0; offset < STAGE_CUE_BYTES; offset += 1) {
    view.setUint8(offset, 0);
  }
  view.setUint32(0, startClock >>> 0, true);
  view.setUint32(4, 1200, true);
  view.setUint32(8, 0, true);
  view.setUint32(12, 0, true);
  view.setUint16(16, 1, true);
  view.setUint8(18, stage.expression);
  view.setUint8(19, stage.gesture);
  view.setUint8(20, stage.gaze);
  view.setUint8(21, 0);
  view.setUint8(22, 1);
  view.setUint8(23, 0);
  view.setUint8(24, 255);
  view.setUint8(25, 1 | (stage.loop ? 2 : 0));
  view.setInt8(26, stage.valence);
  view.setUint8(27, stage.arousal);
  view.setUint8(28, stage.dominance);
}

function createStageCues() {
  for (const pointer of stageCuePointers.values()) {
    wasm._free(pointer);
  }
  stageCuePointers = new Map();
  for (const stageName of STAGE_ORDER) {
    const pointer = wasm._malloc(STAGE_CUE_BYTES);
    writeStageCue(pointer, stageName);
    stageCuePointers.set(stageName, pointer);
  }
}

function renderMatrix(timestamp) {
  if (analysers.size !== ANALYSER_ORDER.length) {
    return;
  }
  latestMetrics = new Map();
  for (const [profile, analyserInstance] of analysers) {
    latestMetrics.set(profile, analyserInstance.snapshot());
  }
  const primaryMetrics =
    latestMetrics.get(2) ?? latestMetrics.values().next().value;
  const audioIsActive = Boolean(transport?.playing || microphoneSession);
  if (audioIsActive) {
    renderClock = primaryMetrics.playoutSamples;
  } else {
    const elapsed = lastRenderTimestamp
      ? Math.max(0, timestamp - lastRenderTimestamp)
      : RENDER_INTERVAL_MS;
    renderClock += Math.round((elapsed * SAMPLE_RATE) / 1000);
  }

  renderReviewRoom(renderClock);
  const matrixStarted = performance.now();
  let renderedThisFrame = 0;
  if (priorityScenario) {
    renderScenario(priorityScenario, renderClock);
    scenariosSeenInSweep.add(priorityScenario);
    renderedThisFrame += 1;
  }
  while (
    scenarioTiles.length &&
    (renderedThisFrame === 0 ||
      performance.now() - matrixStarted < MATRIX_FRAME_BUDGET_MS)
  ) {
    const scenario = scenarioTiles[scenarioCursor];
    if (scenario !== priorityScenario) {
      renderScenario(scenario, renderClock);
      scenariosSeenInSweep.add(scenario);
      renderedThisFrame += 1;
    }
    scenarioCursor += 1;
    if (scenarioCursor >= scenarioTiles.length) {
      scenarioCursor = 0;
      matrixSweepMs = performance.now() - sweepStartedAt;
      sweepStartedAt = performance.now();
      if (scenariosSeenInSweep.size >= scenarioTiles.length) {
        firstFrameRendered = true;
      }
      scenariosSeenInSweep.clear();
    }
    if (renderedThisFrame >= scenarioTiles.length) {
      break;
    }
  }
  statsFrames += 1;
  const statsElapsed = timestamp - statsWindowStarted;
  if (statsElapsed >= 1000) {
    measuredFps = (statsFrames * 1000) / statsElapsed;
    statsFrames = 0;
    statsWindowStarted = timestamp;
  }
  updateRuntimeTelemetry();
  updateClock(primaryMetrics.playoutSamples);

  if (firstFrameRendered && !runtimeStateNode.classList.contains("ready")) {
    runtimeStateNode.classList.add("ready");
    statusNode.textContent = `${(
      rendererProfiles.length * scenarioTiles.length
    ).toLocaleString()} C-rendered views are live`;
    detailNode.textContent =
      `${ANALYSER_ORDER.length} analysers · ${STAGE_ORDER.length} directions · ` +
      `${rendererProfiles.length} portable renderers`;
    window.__STACKCHAN_GRID_READY__ = true;
  }
}

function scheduler(timestamp) {
  if (
    !lastRenderTimestamp ||
    timestamp - lastRenderTimestamp >= RENDER_INTERVAL_MS
  ) {
    renderMatrix(timestamp);
    lastRenderTimestamp =
      timestamp - ((timestamp - lastRenderTimestamp) % RENDER_INTERVAL_MS);
  }
  schedulerHandle = requestAnimationFrame(scheduler);
}

function updateRuntimeTelemetry() {
  const hashes = new Set(
    scenarioTiles
      .filter((scenario) => scenario.hash !== 0)
      .map((scenario) => scenario.hash),
  );
  const combinations = rendererProfiles.length * scenarioTiles.length;
  const contactSheetPixels = scenarioTiles.reduce(
    (total, scenario) => total + scenario.width * scenario.height,
    0,
  );
  const primaryMetrics =
    latestMetrics.get(2) ?? latestMetrics.values().next().value;
  fpsNode.textContent = measuredFps ? measuredFps.toFixed(1) : "…";
  renderMsNode.textContent = matrixSweepMs ? matrixSweepMs.toFixed(0) : "…";
  visibleCountNode.textContent = combinations.toLocaleString();
  detailNode.textContent =
    `${ANALYSER_ORDER.length} analysers · ${STAGE_ORDER.length} directions · ` +
    `primary viseme ${primaryMetrics?.visemeName ?? "—"}`;
  metricsNode.textContent =
    `${rendererProfiles.length} profiles · ${scenarioTiles.length} scenario sheets · ` +
    `${matrixSweepMs ? (1000 / matrixSweepMs).toFixed(1) : "…"} Hz full-atlas refresh · ` +
    `slowest ${worstProfile}`;
  window.__STACKCHAN_GRID_DIAGNOSTICS__ = {
    abiVersion: wasm._stackchan_wasm_abi_version(),
    profileCount: rendererProfiles.length,
    analyserCount: ANALYSER_ORDER.length,
    stageCount: STAGE_ORDER.length,
    scenarioCount: scenarioTiles.length,
    renderedScenarios: scenarioTiles.filter((scenario) => scenario.hash !== 0)
      .length,
    matrixHydrated:
      firstFrameRendered &&
      scenarioTiles.every((scenario) => scenario.hash !== 0),
    distinctScenarioHashes: hashes.size,
    visibleViews: combinations,
    density,
    contactColumns,
    contactTileWidth,
    contactTileHeight,
    matrixRows: ANALYSER_ORDER.length,
    matrixColumns: STAGE_ORDER.length,
    contactSheetPixels,
    contactSheetImageBytes: contactSheetPixels * 4,
    estimatedCanvasAndImageBytes: contactSheetPixels * 8,
    reviewedStrongProfiles: rendererProfiles.filter(
      (renderer) => renderer.reviewTier.id === "reviewed",
    ).length,
    needsRefinementProfiles: rendererProfiles.filter(
      (renderer) => renderer.reviewTier.id === "refine",
    ).length,
    fps: measuredFps,
    matrixSweepMs,
    atlasRefreshHz: matrixSweepMs ? 1000 / matrixSweepMs : 0,
    worstProfile,
    audioSampleClock: primaryMetrics?.playoutSamples ?? 0,
    renderSampleClock: renderClock,
    renderKeyBytes: wasm._stackchan_wasm_render_key_size(),
    stageCueBytes: wasm._stackchan_wasm_stage_cue_size(),
    framebufferBytes: wasm._stackchan_wasm_render_frame_bytes(),
    source: sourceNode.value,
    loopEnabled: loopNode.checked,
    transportPlaying: Boolean(transport?.playing),
    transportCursor: transport?.cursor ?? 0,
    transportSamples: transport?.pcm.length ?? 0,
    microphoneActive: Boolean(microphoneSession),
    reviewPage: reviewPage + 1,
    reviewPageCount: Math.max(
      1,
      Math.ceil(reviewRendererIndexes().length / REVIEW_PAGE_SIZE),
    ),
    reviewPageSize: REVIEW_PAGE_SIZE,
    reviewFavoritesOnly,
    favoriteProfiles: rendererProfiles
      .filter((renderer) => favoriteSlugs.has(renderer.slug))
      .map((renderer) => renderer.profile),
    reviewVisibleProfiles: reviewTiles.map(
      (reviewTile) => reviewTile.renderer.profile,
    ),
    reviewAnalyser: Number(reviewAnalyserNode.value),
    reviewStage: reviewStageNode.value,
    reviewActivity: reviewActivityNode.value,
    reviewRenderMs,
    primaryViseme: primaryMetrics?.visemeName,
    inspectorOpen: inspectorNode.open,
    inspectedProfile: inspectorNode.open
      ? rendererProfiles[inspectorRendererIndex]?.profile
      : null,
    scenarioHashes: Object.fromEntries(
      scenarioTiles.map((scenario) => [
        `${scenario.analyserProfile}:${scenario.stageName}`,
        scenario.hash,
      ]),
    ),
  };
}

function resetAnalysers() {
  for (const analyserInstance of analysers.values()) {
    analyserInstance.destroy();
  }
  analysers = new Map(
    ANALYSER_ORDER.map((profile) => [profile, new WasmAnalyser(profile)]),
  );
  latestMetrics = new Map();
}

function feedUntil(target) {
  while (transport.cursor < target) {
    const end = Math.min(target, transport.cursor + WINDOW_SAMPLES);
    const chunk = transport.pcm.subarray(transport.cursor, end);
    for (const analyserInstance of analysers.values()) {
      analyserInstance.push(chunk);
    }
    transport.cursor = end;
  }
}

function updateClock(samples) {
  const milliseconds = Math.floor((samples * 1000) / SAMPLE_RATE);
  const minutes = Math.floor(milliseconds / 60_000);
  const seconds = Math.floor((milliseconds % 60_000) / 1000);
  const millis = milliseconds % 1000;
  clockNode.textContent = `${String(minutes).padStart(2, "0")}:${String(
    seconds,
  ).padStart(2, "0")}.${String(millis).padStart(3, "0")}`;
  sampleClockNode.textContent = samples.toLocaleString();
}

function drawWaveform(pcm, cursor) {
  const context = waveformContext;
  const width = waveformNode.width;
  const height = waveformNode.height;
  context.clearRect(0, 0, width, height);
  context.strokeStyle = "#263942";
  context.beginPath();
  context.moveTo(0, height / 2);
  context.lineTo(width, height / 2);
  context.stroke();
  context.strokeStyle = "#79e6b0";
  context.lineWidth = 2;
  context.beginPath();
  const samplesPerPixel = Math.max(1, Math.floor(pcm.length / width));
  for (let x = 0; x < width; x += 1) {
    let peak = 0;
    const start = x * samplesPerPixel;
    const end = Math.min(pcm.length, start + samplesPerPixel);
    for (let index = start; index < end; index += 1) {
      peak = Math.max(peak, Math.abs(pcm[index]));
    }
    const y = (peak / 32768) * (height * 0.42);
    context.moveTo(x, height / 2 - y);
    context.lineTo(x, height / 2 + y);
  }
  context.stroke();
  const playhead = (cursor / Math.max(1, pcm.length)) * width;
  context.strokeStyle = "#ffca62";
  context.lineWidth = 3;
  context.beginPath();
  context.moveTo(playhead, 0);
  context.lineTo(playhead, height);
  context.stroke();
}

function generateSyntheticPcm() {
  const seconds = 8;
  const output = new Int16Array(Math.floor(seconds * SAMPLE_RATE));
  const segments = [
    [0.35, 1.15, 700, 1150, 0.72],
    [1.25, 2.0, 390, 2050, 0.65],
    [2.08, 2.86, 300, 2450, 0.68],
    [2.98, 3.78, 460, 900, 0.74],
    [3.9, 4.68, 340, 780, 0.7],
    [4.8, 5.62, 620, 1750, 0.76],
    [5.74, 6.52, 430, 2350, 0.7],
    [6.65, 7.5, 760, 1060, 0.72],
  ];
  for (let index = 0; index < output.length; index += 1) {
    const time = index / SAMPLE_RATE;
    const segment = segments.find(
      ([start, end]) => time >= start && time < end,
    );
    if (!segment) {
      continue;
    }
    const [start, end, f1, f2, amplitude] = segment;
    const local = (time - start) / (end - start);
    const envelope = Math.min(1, local * 14, (1 - local) * 16);
    const pulse =
      Math.sin(2 * Math.PI * 118 * time) * 0.38 +
      Math.sin(2 * Math.PI * f1 * time) * 0.34 +
      Math.sin(2 * Math.PI * f2 * time) * 0.2;
    const noise =
      local < 0.025 ? (Math.sin(index * 12.9898) * 43758.5453) % 1 : 0;
    output[index] = Math.max(
      -32768,
      Math.min(
        32767,
        Math.round((pulse + noise * 0.25) * envelope * amplitude * 11000),
      ),
    );
  }
  return output;
}

function parsePcm16Wav(arrayBuffer) {
  const view = new DataView(arrayBuffer);
  const text = (offset, length) =>
    String.fromCharCode(...new Uint8Array(arrayBuffer, offset, length));
  if (text(0, 4) !== "RIFF" || text(8, 4) !== "WAVE") {
    throw new Error("The selected file is not a RIFF/WAVE file.");
  }
  let offset = 12;
  let format;
  let data;
  while (offset + 8 <= view.byteLength) {
    const id = text(offset, 4);
    const size = view.getUint32(offset + 4, true);
    const body = offset + 8;
    if (id === "fmt ") {
      format = {
        encoding: view.getUint16(body, true),
        channels: view.getUint16(body + 2, true),
        sampleRate: view.getUint32(body + 4, true),
        bits: view.getUint16(body + 14, true),
      };
    } else if (id === "data") {
      data = new Int16Array(arrayBuffer.slice(body, body + size));
    }
    offset = body + size + (size & 1);
  }
  if (
    !format ||
    !data ||
    format.encoding !== 1 ||
    format.channels !== 1 ||
    format.sampleRate !== SAMPLE_RATE ||
    format.bits !== 16
  ) {
    throw new Error("WAV must be mono PCM16 at exactly 16 kHz.");
  }
  return data;
}

async function ensureAudioContext() {
  audioContext ??= new AudioContext({ latencyHint: "interactive" });
  return audioContext;
}

async function loadSelectedSource() {
  const generation = ++inputSessionGeneration;
  stopMicrophone();
  transport?.pause();
  const source = sourceNode.value;
  let nextPcm;
  if (source === "synthetic") {
    nextPcm = generateSyntheticPcm();
  } else {
    try {
      const audioResponse = await fetch(`./audio/${source}.wav`);
      if (!audioResponse.ok) {
        throw new Error(`capture returned HTTP ${audioResponse.status}`);
      }
      nextPcm = parsePcm16Wav(await audioResponse.arrayBuffer());
    } catch (error) {
      if (generation !== inputSessionGeneration) {
        return;
      }
      sourceNode.value = "synthetic";
      nextPcm = generateSyntheticPcm();
    }
  }
  const context = await ensureAudioContext();
  if (generation !== inputSessionGeneration) {
    return;
  }
  activePcm = nextPcm;
  resetAnalysers();
  renderClock = 0;
  transport = new SharedPcmTransport(context, activePcm, loopNode.checked);
  drawWaveform(activePcm, 0);
  updateClock(0);
  playNode.textContent = loopNode.checked ? "Play loop" : "Play clip";
  statusNode.textContent = `Ready · press ${playNode.textContent}`;
}

async function startMicrophone() {
  const generation = ++inputSessionGeneration;
  transport?.pause();
  stopMicrophone();
  const context = await ensureAudioContext();
  await context.resume();
  const stream = await navigator.mediaDevices.getUserMedia({
    audio: {
      channelCount: 1,
      echoCancellation: false,
      autoGainControl: false,
      noiseSuppression: false,
    },
  });
  if (generation !== inputSessionGeneration) {
    stream.getTracks().forEach((track) => track.stop());
    return;
  }
  await context.audioWorklet.addModule("./pcm-worklet.js");
  if (generation !== inputSessionGeneration) {
    stream.getTracks().forEach((track) => track.stop());
    return;
  }
  const source = context.createMediaStreamSource(stream);
  const tap = new AudioWorkletNode(context, "stackchan-pcm-tap");
  const mute = context.createGain();
  mute.gain.value = 0;
  source.connect(tap).connect(mute).connect(context.destination);
  const downsampler = new StreamingDownsampler(context.sampleRate, SAMPLE_RATE);
  resetAnalysers();
  renderClock = 0;
  tap.port.onmessage = ({ data }) => {
    const pcm = downsampler.process(new Float32Array(data));
    if (pcm.length) {
      for (const analyserInstance of analysers.values()) {
        analyserInstance.push(pcm);
      }
    }
  };
  microphoneSession = { stream, source, tap, mute };
  microphoneNode.textContent = "Stop microphone";
  playNode.disabled = true;
  restartNode.disabled = true;
  statusNode.textContent = `Live mic · ${context.sampleRate.toLocaleString()} → 16,000 Hz`;
}

function stopMicrophone() {
  if (!microphoneSession) {
    return;
  }
  microphoneSession.stream.getTracks().forEach((track) => track.stop());
  microphoneSession.source.disconnect();
  microphoneSession.tap.disconnect();
  microphoneSession.mute.disconnect();
  microphoneSession = null;
  microphoneNode.textContent = "Use microphone";
  playNode.disabled = false;
  restartNode.disabled = false;
}

async function initialise() {
  wasm = await createStackchanFaceModule({
    locateFile: (path) => new URL(path, import.meta.url).href,
  });
  if (
    wasm._stackchan_wasm_abi_version() !== 2 ||
    wasm._stackchan_wasm_keyframe_size() !== 12 ||
    wasm._stackchan_wasm_render_key_size() !== RENDER_KEY_BYTES ||
    wasm._stackchan_wasm_metrics_size() !== METRICS_BYTES ||
    wasm._stackchan_wasm_stage_cue_size() !== STAGE_CUE_BYTES ||
    wasm._stackchan_wasm_render_frame_bytes() !== RENDER_PIXELS * 2
  ) {
    throw new Error("JavaScript and C/WASM ABI versions do not match.");
  }
  const model = new Uint8Array(
    await (await fetch("./head_audio_model_en_mixed.bin")).arrayBuffer(),
  );
  modelBytes = model.byteLength;
  modelPointer = wasm._malloc(modelBytes);
  wasm.HEAPU8.set(model, modelPointer);
  pcmPointer = wasm._malloc(WINDOW_SAMPLES * 2);
  renderKeyPointer = wasm._malloc(RENDER_KEY_BYTES);
  framebufferPointer = wasm._malloc(RENDER_PIXELS * 2);
  profileInfoPointer = wasm._malloc(16);
  rgbaLookup = createRgbaLookup();
  createStageCues();
  createRendererCatalog();
  seedActiveSpriteFavorites();
  createScenarioMatrix();
  updateReviewPage(0);
  await loadSelectedSource();
  playNode.disabled = false;
  restartNode.disabled = false;
  microphoneNode.disabled = false;
  schedulerHandle = requestAnimationFrame(scheduler);
}

sourceNode.addEventListener("change", () => {
  void loadSelectedSource();
});

loopNode.addEventListener("change", () => {
  if (transport) {
    transport.loop = loopNode.checked;
  }
  if (!transport?.playing) {
    playNode.textContent = loopNode.checked ? "Play loop" : "Play clip";
  }
  statusNode.textContent = loopNode.checked
    ? "Loop enabled · the selected clip will repeat"
    : "Loop disabled · the selected clip will play once";
});

playNode.addEventListener("click", () => {
  void transport.play();
});

restartNode.addEventListener("click", () => {
  transport.restart();
});

microphoneNode.addEventListener("click", () => {
  if (microphoneSession) {
    stopMicrophone();
    void loadSelectedSource();
  } else {
    const expectedGeneration = inputSessionGeneration + 1;
    void startMicrophone().catch((error) => {
      if (expectedGeneration !== inputSessionGeneration) {
        return;
      }
      stopMicrophone();
      statusNode.textContent = `Microphone error · ${error.message}`;
    });
  }
});

fileNode.addEventListener("change", async () => {
  const [file] = fileNode.files;
  if (!file) {
    return;
  }
  const generation = ++inputSessionGeneration;
  stopMicrophone();
  transport?.pause();
  try {
    const nextPcm = parsePcm16Wav(await file.arrayBuffer());
    const context = await ensureAudioContext();
    if (generation !== inputSessionGeneration) {
      return;
    }
    activePcm = nextPcm;
    sourceNode.value = "synthetic";
    resetAnalysers();
    renderClock = 0;
    transport = new SharedPcmTransport(context, activePcm, loopNode.checked);
    drawWaveform(activePcm, 0);
    playNode.textContent = loopNode.checked ? "Play loop" : "Play clip";
    statusNode.textContent = "Local PCM16 file ready";
  } catch (error) {
    statusNode.textContent = `WAV error · ${error.message}`;
  }
});

reviewPreviousNode.addEventListener("click", () => {
  updateReviewPage(reviewPage - 1);
});

reviewNextNode.addEventListener("click", () => {
  updateReviewPage(reviewPage + 1);
});

reviewFavoritesNode.addEventListener("click", () => {
  reviewFavoritesOnly = !reviewFavoritesOnly;
  updateReviewPage(0);
});

reviewAnalyserNode.addEventListener("change", () => {
  statusNode.textContent = `Review analyser · ${
    ANALYSERS.get(Number(reviewAnalyserNode.value)).name
  }`;
});

reviewStageNode.addEventListener("change", () => {
  statusNode.textContent = `Review direction · ${STAGE_LABELS[reviewStageNode.value]}`;
});

reviewActivityNode.addEventListener("change", () => {
  statusNode.textContent =
    `Review activity · ${REVIEW_ACTIVITY_LABELS[reviewActivityNode.value]}`;
});

densityControlNode.addEventListener("click", (event) => {
  const button = event.target.closest("button[data-density]");
  if (!button) {
    return;
  }
  setDensity(button.dataset.density);
  for (const candidate of densityControlNode.querySelectorAll(
    "button[data-density]",
  )) {
    const active = candidate === button;
    candidate.classList.toggle("active", active);
    candidate.setAttribute("aria-pressed", String(active));
  }
});

inspectorPreviousNode.addEventListener("click", () => {
  selectInspectorRenderer(inspectorRendererIndex - 1);
});

inspectorNextNode.addEventListener("click", () => {
  selectInspectorRenderer(inspectorRendererIndex + 1);
});

inspectorNode.addEventListener("keydown", (event) => {
  if (event.key === "ArrowLeft") {
    event.preventDefault();
    selectInspectorRenderer(inspectorRendererIndex - 1);
  } else if (event.key === "ArrowRight") {
    event.preventDefault();
    selectInspectorRenderer(inspectorRendererIndex + 1);
  }
});

inspectorNode.addEventListener("close", () => {
  inspectorScenario = null;
  priorityScenario = null;
});

window.addEventListener("beforeunload", () => {
  cancelAnimationFrame(schedulerHandle);
  for (const analyserInstance of analysers.values()) {
    analyserInstance.destroy();
  }
  for (const pointer of stageCuePointers.values()) {
    wasm._free(pointer);
  }
  stopMicrophone();
});

initialise().catch((error) => {
  console.error(error);
  statusNode.textContent = `Initialisation failed · ${error.message}`;
  detailNode.textContent = "See the browser console for details";
  document.body.dataset.error = "true";
});
