#!/usr/bin/env node
/**
 * Generate a deterministic HeadAudio feature-vector oracle.
 *
 * Usage:
 *   node tools/generate_face_viseme_golden.mjs \
 *     ~/src/github.com/met4citizen/HeadAudio
 *
 * The output is deliberately JSON on stdout so the small checked-in C test
 * can be refreshed from first-party upstream source without depending on
 * Node.js or the upstream checkout during normal test runs.
 */

import {pathToFileURL} from 'node:url';
import path from 'node:path';

const upstream = path.resolve(process.argv[2] ?? '');
if (!process.argv[2]) {
  throw new Error('pass the HeadAudio source checkout as the first argument');
}

const {Processor} = await import(
  pathToFileURL(path.join(upstream, 'modules/processor.mjs')).href
);

const pcm16 = Int16Array.from({length: 512}, (_, index) => {
  const carrier =
    9200 * Math.sin((2 * Math.PI * 173 * index) / 16000) +
    5100 * Math.sin((2 * Math.PI * 731 * index) / 16000);
  const envelope = 0.35 + 0.65 * index / 511;
  return Math.round(carrier * envelope);
});
const input = Float32Array.from(pcm16, (sample) => sample / 32768);
const events = [];
const worklet = {
  port: {
    postMessage: (event) => events.push(event),
  },
};
const processor = new Processor(
  {
    sampleRate: 16000,
    processorOptions: {featureEventsEnabled: true},
    parameterData: {vadMode: 0},
  },
  worklet,
);
processor.process(input);

const feature = events.find((event) => event.event === 'feature');
if (!feature) {
  throw new Error('upstream Processor did not emit a feature event');
}

process.stdout.write(
  `${JSON.stringify(
    {
      generator: 'met4citizen/HeadAudio Processor',
      sampleRate: 16000,
      samples: Array.from(pcm16),
      features: Array.from(feature.vector),
    },
    null,
    2,
  )}\n`,
);
