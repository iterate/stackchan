import React from 'react';
import {Composition, type CalculateMetadataFunction} from 'remotion';
import {FacePcmOverlay} from './FacePcmOverlay';
import {FaceShowcase} from './FaceShowcase';
import type {
  FacePcmVideoProps,
  ShowcaseProps,
} from './types';

const defaultStyle = {
  name: 'silver',
  hairColor: '#8797a5',
  skinColor: '#efc8a8',
  skinShadow: '#c98f76',
  eyeColor: '#315b6f',
  lipColor: '#a9435d',
  tongueColor: '#d9687c',
  accentColor: '#60d394',
  backgroundNear: '#17384b',
  backgroundFar: '#071521',
};

const defaultProps: FacePcmVideoProps = {
  audioFile: 'response.wav',
  sourceLabel: 'REAL xAI GROK REALTIME',
  title: 'Real Grok PCM',
  testCase: 'default',
  voice: 'eve',
  transcript: '',
  prompt: '',
  model: 'grok-voice-latest',
  sampleRate: 16_000,
  fps: 30,
  durationInFrames: 90,
  pcmSha256: '',
  trace: [],
  waveform: [],
  waveformSamplesPerBin: 40,
  packets: [],
  streamEvents: [],
  algorithm: {
    id: 'envelope',
    label: 'Amplitude envelope',
    profile: 'default',
    properties: {},
    propertySummary: 'default',
    tradeoff: '',
    stateBytes: 60,
    modelBytes: 0,
  },
  style: defaultStyle,
};

const defaultShowcase: ShowcaseProps = {
  title: 'StackChan PCM face comparison',
  fps: 30,
  durationInFrames: 90,
  segments: [
    {
      kind: 'slide',
      durationInFrames: 90,
      kicker: 'STACKCHAN FACE LAB',
      title: 'PCM → expression',
      body: 'A deterministic comparison of lightweight rendering algorithms.',
      bullets: [],
      accentColor: '#60d394',
    },
  ],
};

const calculateFaceMetadata: CalculateMetadataFunction<FacePcmVideoProps> = ({
  props,
}) => ({
  durationInFrames: props.durationInFrames,
  fps: props.fps,
  defaultOutName: `algorithm-${props.algorithm.id}_voice-${props.voice}.mp4`,
});

const calculateShowcaseMetadata: CalculateMetadataFunction<ShowcaseProps> = ({
  props,
}) => ({
  durationInFrames: props.durationInFrames,
  fps: props.fps,
  defaultOutName: 'stackchan-pcm-face-algorithm-showcase.mp4',
});

export const RemotionRoot: React.FC = () => (
  <>
    <Composition
      id="FacePcmOverlay"
      component={FacePcmOverlay}
      width={1280}
      height={720}
      fps={30}
      durationInFrames={90}
      defaultProps={defaultProps}
      calculateMetadata={calculateFaceMetadata}
    />
    <Composition
      id="FaceShowcase"
      component={FaceShowcase}
      width={1280}
      height={720}
      fps={30}
      durationInFrames={90}
      defaultProps={defaultShowcase}
      calculateMetadata={calculateShowcaseMetadata}
    />
  </>
);
