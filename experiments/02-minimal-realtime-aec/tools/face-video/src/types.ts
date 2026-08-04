export type FaceState = {
  frame_index: number;
  playout_samples: number;
  level: number;
  mouth_open: number;
  mouth_width: number;
  mouth_round: number;
  mouth_press: number;
  mouth_teeth: number;
  eye_open: number;
  gaze_x: number;
  gaze_y: number;
  viseme: number;
  phoneme: number;
  confidence: number;
  activity: number;
  speaking: boolean;
};

export type FaceGeometry = {
  left_eye_x: number;
  left_eye_y: number;
  right_eye_x: number;
  right_eye_y: number;
  eye_width: number;
  eye_height: number;
  pupil_size: number;
  pupil_offset_x: number;
  pupil_offset_y: number;
  mouth_x: number;
  mouth_y: number;
  mouth_width: number;
  mouth_height: number;
};

export type PcmAnalysis = {
  mean_abs: number;
  peak: number;
  rms: number;
  zero_crossings: number;
};

export type FaceFrame = {
  clock_ms: number;
  queued_samples: number;
  underrun: boolean;
  state: FaceState;
  geometry: FaceGeometry;
  analysis: PcmAnalysis;
};

export type WaveformBin = {
  min: number;
  max: number;
};

export type GrokPacket = {
  index: number;
  protocol_event: string;
  received_seconds: number;
  bytes: number;
  sample_start: number;
  sample_end: number;
};

export type GrokStreamEvent = {
  index: number;
  kind: 'assistant_audio' | 'event';
  protocol_event: string;
  received_seconds: number;
  received_audio_samples: number;
  dispatch_playout_samples: number;
  bytes?: number;
  sample_start?: number;
  sample_end?: number;
  text_delta?: string;
  text?: string;
  cumulative?: boolean;
};

export type FaceStyleConfig = {
  name: string;
  hairColor: string;
  skinColor: string;
  skinShadow: string;
  eyeColor: string;
  lipColor: string;
  tongueColor: string;
  accentColor: string;
  backgroundNear: string;
  backgroundFar: string;
};

export type FaceAlgorithmMeta = {
  id: 'envelope' | 'viseme';
  label: string;
  profile: string;
  properties: Record<string, string | number | boolean>;
  propertySummary: string;
  tradeoff: string;
  stateBytes: number;
  modelBytes: number;
};

export type FacePcmVideoProps = {
  audioFile: string;
  sourceLabel: string;
  title: string;
  testCase: string;
  voice: string;
  transcript: string;
  prompt: string;
  model: string;
  sampleRate: number;
  fps: number;
  durationInFrames: number;
  pcmSha256: string;
  trace: FaceFrame[];
  waveform: WaveformBin[];
  waveformSamplesPerBin: number;
  packets: GrokPacket[];
  streamEvents: GrokStreamEvent[];
  algorithm: FaceAlgorithmMeta;
  style: FaceStyleConfig;
};

export type ShowcaseSlideSegment = {
  kind: 'slide';
  durationInFrames: number;
  kicker: string;
  title: string;
  body: string;
  bullets: string[];
  accentColor: string;
  footer?: string;
};

export type ShowcaseVideoSegment = {
  kind: 'video';
  durationInFrames: number;
  file: string;
};

export type ShowcaseSegment =
  | ShowcaseSlideSegment
  | ShowcaseVideoSegment;

export type ShowcaseProps = {
  title: string;
  fps: number;
  durationInFrames: number;
  segments: ShowcaseSegment[];
};
