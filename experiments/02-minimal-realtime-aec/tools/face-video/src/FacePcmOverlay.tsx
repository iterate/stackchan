import {Audio} from '@remotion/media';
import React from 'react';
import {
  AbsoluteFill,
  interpolate,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from 'remotion';
import type {
  FaceFrame,
  FacePcmVideoProps,
  FaceStyleConfig,
  GrokPacket,
  GrokStreamEvent,
  WaveformBin,
} from './types';

const DISPLAY_WIDTH = 640;
const DISPLAY_HEIGHT = 480;
const DISPLAY_X = 38;
const DISPLAY_Y = 78;
const WAVEFORM_HEIGHT = 82;
const WAVEFORM_Y = DISPLAY_HEIGHT - WAVEFORM_HEIGHT;
const WAVEFORM_BINS = 160;
const CURSOR_FRACTION = 0.42;

const panel: React.CSSProperties = {
  border: '1px solid #214158',
  background: 'rgba(7, 21, 33, 0.94)',
  boxShadow: '0 18px 50px rgba(0, 0, 0, 0.35)',
};

const mono: React.CSSProperties = {
  fontFamily:
    '"SFMono-Regular", "Cascadia Code", "Roboto Mono", Menlo, monospace',
};

const bounded = (value: number, lower: number, upper: number): number =>
  Math.min(upper, Math.max(lower, value));

const packetAtSample = (
  packets: GrokPacket[],
  sample: number,
): GrokPacket | undefined =>
  packets.find(
    (packet) => sample >= packet.sample_start && sample < packet.sample_end,
  );

const transcriptAtSample = (
  events: GrokStreamEvent[],
  sample: number,
  fallback: string,
): string => {
  const timed = events.filter(
    (event) =>
      event.kind === 'event' &&
      event.dispatch_playout_samples <= sample &&
      typeof event.text === 'string' &&
      event.protocol_event.startsWith('response.output_audio_transcript.'),
  );
  if (timed.length === 0) {
    const hasTranscriptTiming = events.some(
      (event) =>
        event.kind === 'event' &&
        event.protocol_event.startsWith(
          'response.output_audio_transcript.',
        ),
    );
    return hasTranscriptTiming ? '' : fallback;
  }
  return timed[timed.length - 1].text ?? '';
};

const latestProtocolEvent = (
  events: GrokStreamEvent[],
  sample: number,
): GrokStreamEvent | undefined => {
  const dispatched = events.filter(
    (event) =>
      event.kind === 'event' &&
      event.dispatch_playout_samples <= sample,
  );
  return dispatched[dispatched.length - 1];
};

const VISEMES = [
  'aa',
  'E',
  'I',
  'O',
  'U',
  'PP',
  'SS',
  'TH',
  'DD',
  'FF',
  'kk',
  'nn',
  'RR',
  'CH',
  'sil',
] as const;

const ACTIVITIES = ['IDLE', 'LISTENING', 'THINKING', 'SPEAKING'] as const;

const visemeName = (value: number): string =>
  value >= 0 && value < VISEMES.length ? VISEMES[value] : 'envelope';

const activityName = (value: number): string =>
  value >= 0 && value < ACTIVITIES.length
    ? ACTIVITIES[value]
    : 'UNKNOWN';

const FaceAvatar: React.FC<{
  pose: FaceFrame;
  style: FaceStyleConfig;
}> = ({pose, style}) => {
  const state = pose.state;
  const open = state.mouth_open / 255;
  const width = state.mouth_width / 255;
  const round = state.mouth_round / 255;
  const press = state.mouth_press / 255;
  const teeth = state.mouth_teeth / 255;
  const eyeOpen = state.eye_open / 255;
  const mouthWidth = (46 + width * 84) * (1 - round * 0.34);
  const innerHeight = Math.max(
    1.5,
    2 + open * 55 - press * (7 + open * 12),
  );
  const outerHeight = Math.max(6, innerHeight + 8 + press * 2);
  const mouthY = 132 + open * 3;
  const eyeHeight = Math.max(2.5, 19 * eyeOpen - open * 2.2);
  const browLift = open * 4 + (state.speaking ? 1.5 : 0);
  const headScale = 1 + open * 0.018;
  const headBob = -open * 1.8;
  const pupilX = (state.gaze_x / 8) * 5;
  const pupilY = (state.gaze_y / 8) * 3;
  const mouthClip = `mouth-${style.name.replace(/[^a-z0-9]/gi, '')}`;

  return (
    <svg
      width="100%"
      height={WAVEFORM_Y}
      viewBox="0 0 320 199"
      role="img"
      aria-label="StackChan PCM-driven face"
    >
      <defs>
        <filter id="head-shadow" x="-30%" y="-30%" width="160%" height="180%">
          <feDropShadow
            dx="0"
            dy="5"
            stdDeviation="6"
            floodColor="#000"
            floodOpacity="0.3"
          />
        </filter>
        <clipPath id={mouthClip}>
          <ellipse
            cx={160}
            cy={mouthY}
            rx={mouthWidth / 2 - 4}
            ry={innerHeight / 2}
          />
        </clipPath>
      </defs>
      <g
        transform={`translate(160 ${96 + headBob}) scale(${headScale}) translate(-160 -96)`}
      >
        <ellipse
          cx={160}
          cy={98}
          rx={102}
          ry={81}
          fill={style.skinShadow}
          opacity={0.62}
          filter="url(#head-shadow)"
        />
        <ellipse
          cx={160}
          cy={94}
          rx={98}
          ry={78}
          fill={style.skinColor}
        />
        <ellipse
          cx={72}
          cy={101}
          rx={12}
          ry={20}
          fill={style.skinColor}
          stroke={style.skinShadow}
          strokeWidth={3}
        />
        <ellipse
          cx={248}
          cy={101}
          rx={12}
          ry={20}
          fill={style.skinColor}
          stroke={style.skinShadow}
          strokeWidth={3}
        />

        <path
          d="M69 75 C78 18, 113 4, 159 8 C210 4, 243 29, 251 78
             C224 55, 207 41, 188 44 C178 28, 154 25, 135 41
             C113 32, 88 49, 69 75 Z"
          fill={style.hairColor}
        />
        <path
          d="M93 48 C112 20, 143 18, 160 31 C177 14, 213 27, 229 55"
          fill="none"
          stroke="#ffffff26"
          strokeWidth={4}
          strokeLinecap="round"
        />

        <ellipse
          cx={112}
          cy={116}
          rx={24}
          ry={13}
          fill={style.lipColor}
          opacity={0.05 + open * 0.14}
        />
        <ellipse
          cx={208}
          cy={116}
          rx={24}
          ry={13}
          fill={style.lipColor}
          opacity={0.05 + open * 0.14}
        />

        <path
          d={`M94 ${67 - browLift} Q112 ${59 - browLift} 130 ${
            68 - browLift
          }`}
          fill="none"
          stroke={style.hairColor}
          strokeWidth={5}
          strokeLinecap="round"
        />
        <path
          d={`M190 ${68 - browLift} Q208 ${59 - browLift} 226 ${
            67 - browLift
          }`}
          fill="none"
          stroke={style.hairColor}
          strokeWidth={5}
          strokeLinecap="round"
        />

        {[115, 205].map((x) => (
          <g key={x}>
            <ellipse
              cx={x}
              cy={86 - open * 1.2}
              rx={23}
              ry={eyeHeight / 2}
              fill="#f7fbff"
              stroke={style.skinShadow}
              strokeWidth={1.4}
            />
            {eyeHeight > 4 ? (
              <>
                <circle
                  cx={x + pupilX}
                  cy={86 - open * 1.2 + pupilY}
                  r={7.5}
                  fill={style.eyeColor}
                />
                <circle
                  cx={x + pupilX + 2}
                  cy={83 - open * 1.2 + pupilY}
                  r={2}
                  fill="#ffffffcc"
                />
              </>
            ) : null}
          </g>
        ))}

        <path
          d={`M157 90 Q151 ${106 + open * 2} 161 108`}
          fill="none"
          stroke={style.skinShadow}
          strokeWidth={2.2}
          strokeLinecap="round"
          opacity={0.55}
        />

        <ellipse
          cx={160}
          cy={mouthY}
          rx={mouthWidth / 2}
          ry={outerHeight / 2}
          fill={style.lipColor}
        />
        <ellipse
          cx={160}
          cy={mouthY}
          rx={mouthWidth / 2 - 4}
          ry={innerHeight / 2}
          fill="#24131d"
        />
        <g clipPath={`url(#${mouthClip})`}>
          <ellipse
            cx={160}
            cy={mouthY + innerHeight * 0.36}
            rx={mouthWidth * 0.36}
            ry={Math.max(2, innerHeight * 0.32)}
            fill={style.tongueColor}
            opacity={bounded(open * 1.5, 0, 0.92)}
          />
          <rect
            x={160 - mouthWidth / 2}
            y={mouthY - innerHeight / 2}
            width={mouthWidth}
            height={Math.max(2, innerHeight * 0.32)}
            rx={2}
            fill="#f8f5e9"
            opacity={bounded(teeth * 1.35 * open, 0, 1)}
          />
        </g>
        {press > 0.24 ? (
          <path
            d={`M${160 - mouthWidth / 2 + 4} ${mouthY}
                Q160 ${mouthY + press * 2}
                ${160 + mouthWidth / 2 - 4} ${mouthY}`}
            fill="none"
            stroke="#6f2439"
            strokeWidth={1.5 + press * 2}
            strokeLinecap="round"
          />
        ) : null}
      </g>
    </svg>
  );
};

const Waveform: React.FC<{
  waveform: WaveformBin[];
  waveformSamplesPerBin: number;
  packets: GrokPacket[];
  events: GrokStreamEvent[];
  currentSample: number;
  accent: string;
}> = ({
  waveform,
  waveformSamplesPerBin,
  packets,
  events,
  currentSample,
  accent,
}) => {
  const currentBin = Math.floor(currentSample / waveformSamplesPerBin);
  const startBin = currentBin - Math.floor(WAVEFORM_BINS * CURSOR_FRACTION);
  const visible = Array.from({length: WAVEFORM_BINS}, (_, offset) => {
    const index = startBin + offset;
    return {index, bin: waveform[index] ?? {min: 0, max: 0}};
  });
  const binWidth = DISPLAY_WIDTH / WAVEFORM_BINS;
  const cursorX = DISPLAY_WIDTH * CURSOR_FRACTION;
  const markers = events.filter(
    (event) =>
      event.kind === 'event' &&
      (event.protocol_event.includes('transcript') ||
        event.protocol_event === 'response.created' ||
        event.protocol_event === 'response.done'),
  );

  return (
    <svg
      width={DISPLAY_WIDTH}
      height={WAVEFORM_HEIGHT}
      viewBox={`0 0 ${DISPLAY_WIDTH} ${WAVEFORM_HEIGHT}`}
      style={{
        position: 'absolute',
        left: 0,
        top: WAVEFORM_Y,
        background: 'rgba(3, 13, 21, 0.9)',
        borderTop: `1px solid ${accent}55`,
      }}
    >
      {packets.map((packet) => {
        const x =
          (packet.sample_start / waveformSamplesPerBin - startBin) * binWidth;
        const width =
          ((packet.sample_end - packet.sample_start) /
            waveformSamplesPerBin) *
          binWidth;
        if (x + width < 0 || x > DISPLAY_WIDTH) {
          return null;
        }
        return (
          <rect
            key={packet.index}
            x={x}
            y={0}
            width={Math.max(1, width)}
            height={9}
            fill={packet.index % 2 === 0 ? `${accent}aa` : '#58a6ffaa'}
          />
        );
      })}
      <line
        x1={0}
        x2={DISPLAY_WIDTH}
        y1={WAVEFORM_HEIGHT / 2}
        y2={WAVEFORM_HEIGHT / 2}
        stroke="#29485b"
        strokeWidth={1}
      />
      {visible.map(({index, bin}, offset) => {
        const x = offset * binWidth + binWidth / 2;
        const y1 =
          WAVEFORM_HEIGHT / 2 -
          (bounded(bin.max, -32768, 32767) / 32768) *
            (WAVEFORM_HEIGHT * 0.37);
        const y2 =
          WAVEFORM_HEIGHT / 2 -
          (bounded(bin.min, -32768, 32767) / 32768) *
            (WAVEFORM_HEIGHT * 0.37);
        return (
          <line
            key={index}
            x1={x}
            x2={x}
            y1={y1}
            y2={y2}
            stroke="#ff8d81"
            strokeWidth={Math.max(1.3, binWidth * 0.58)}
            opacity={index * waveformSamplesPerBin < 0 ? 0.15 : 0.9}
          />
        );
      })}
      {markers.map((event) => {
        const x =
          (event.dispatch_playout_samples / waveformSamplesPerBin -
            startBin) *
          binWidth;
        if (x < 0 || x > DISPLAY_WIDTH) {
          return null;
        }
        return (
          <line
            key={`event-${event.index}`}
            x1={x}
            x2={x}
            y1={9}
            y2={WAVEFORM_HEIGHT}
            stroke={
              event.protocol_event.includes('transcript')
                ? '#ffd166'
                : '#c4b5fd'
            }
            strokeWidth={1}
            opacity={0.7}
          />
        );
      })}
      <rect
        x={cursorX}
        y={9}
        width={(160 / waveformSamplesPerBin) * binWidth}
        height={WAVEFORM_HEIGHT - 9}
        fill="rgba(255, 255, 255, 0.08)"
        stroke="#f4f8ff"
        strokeWidth={1}
      />
    </svg>
  );
};

const FaceDisplay: React.FC<{
  pose: FaceFrame;
  style: FaceStyleConfig;
  waveform: WaveformBin[];
  waveformSamplesPerBin: number;
  packets: GrokPacket[];
  events: GrokStreamEvent[];
  currentSample: number;
  sampleRate: number;
}> = (props) => {
  const {pose, style, currentSample, sampleRate} = props;
  const state = pose.state;
  const currentFrameStart = Math.floor(currentSample / 160) * 160;
  return (
    <div
      style={{
        position: 'absolute',
        left: DISPLAY_X,
        top: DISPLAY_Y,
        width: DISPLAY_WIDTH,
        height: DISPLAY_HEIGHT,
        overflow: 'hidden',
        borderRadius: 30,
        background: `radial-gradient(circle at 50% 42%, ${style.backgroundNear}, ${style.backgroundFar} 78%)`,
        border: `2px solid ${style.accentColor}77`,
        boxShadow: 'inset 0 0 60px rgba(0,0,0,.24)',
      }}
    >
      <div
        style={{
          ...mono,
          position: 'absolute',
          zIndex: 3,
          top: 13,
          left: 18,
          color: state.speaking ? style.accentColor : '#8aa3b4',
          fontWeight: 850,
          fontSize: 14,
          letterSpacing: 2.4,
        }}
      >
        {activityName(state.activity)}
      </div>
      <div
        style={{
          ...mono,
          position: 'absolute',
          zIndex: 3,
          top: 13,
          right: 18,
          color: '#8aa3b4',
          fontSize: 12,
        }}
      >
        {visemeName(state.viseme)} · {state.mouth_open}/255
      </div>
      <FaceAvatar pose={pose} style={style} />
      <Waveform
        waveform={props.waveform}
        waveformSamplesPerBin={props.waveformSamplesPerBin}
        packets={props.packets}
        events={props.events}
        currentSample={currentSample}
        accent={style.accentColor}
      />
      <div
        style={{
          ...mono,
          position: 'absolute',
          left: 13,
          bottom: 3,
          color: '#9eb7c7',
          fontSize: 10,
        }}
      >
        PCM16LE · {sampleRate / 1000} kHz · samples{' '}
        {currentFrameStart.toLocaleString()}–
        {(currentFrameStart + 159).toLocaleString()}
      </div>
    </div>
  );
};

const Metric: React.FC<{
  label: string;
  value: string;
  accent?: string;
}> = ({label, value, accent = '#f4f8ff'}) => (
  <div
    style={{
      padding: '11px 13px',
      borderBottom: '1px solid #183245',
      minWidth: 0,
    }}
  >
    <div
      style={{
        ...mono,
        color: '#7693a7',
        fontSize: 10,
        letterSpacing: 1.25,
        marginBottom: 4,
      }}
    >
      {label}
    </div>
    <div
      style={{
        ...mono,
        color: accent,
        fontSize: 18,
        fontWeight: 760,
        whiteSpace: 'nowrap',
        overflow: 'hidden',
        textOverflow: 'ellipsis',
      }}
    >
      {value}
    </div>
  </div>
);

export const FacePcmOverlay: React.FC<FacePcmVideoProps> = (props) => {
  const frame = useCurrentFrame();
  const {fps} = useVideoConfig();
  if (props.trace.length === 0) {
    return <AbsoluteFill style={{background: '#050d14'}} />;
  }

  const currentSample = Math.min(
    Math.floor((frame / fps) * props.sampleRate),
    props.trace[props.trace.length - 1].state.playout_samples,
  );
  const traceIndex = bounded(
    Math.floor((currentSample * 100) / props.sampleRate),
    0,
    props.trace.length - 1,
  );
  const pose = props.trace[traceIndex];
  const packet = packetAtSample(props.packets, currentSample);
  const protocolEvent = latestProtocolEvent(props.streamEvents, currentSample);
  const transcript = transcriptAtSample(
    props.streamEvents,
    currentSample,
    props.transcript,
  );
  const fade = interpolate(frame, [0, 7], [0, 1], {
    extrapolateLeft: 'clamp',
    extrapolateRight: 'clamp',
  });
  const algorithmColor =
    props.algorithm.id === 'viseme' ? '#ffd166' : '#60d394';

  return (
    <AbsoluteFill
      style={{
        background:
          'radial-gradient(circle at 30% 42%, #102a3a 0%, #07131d 45%, #040a10 100%)',
        color: '#f4f8ff',
        opacity: fade,
      }}
    >
      <Audio src={staticFile(props.audioFile)} />

      <div
        style={{
          ...mono,
          position: 'absolute',
          left: 40,
          top: 24,
          fontSize: 14,
          letterSpacing: 2.1,
          color: '#60d394',
          fontWeight: 850,
        }}
      >
        {props.sourceLabel} · ORDERED 16 kHz PCM + EVENTS
      </div>
      <div
        style={{
          position: 'absolute',
          right: 42,
          top: 18,
          padding: '8px 12px',
          borderRadius: 999,
          border: `1px solid ${algorithmColor}77`,
          background: '#0a1c28',
          ...mono,
          color: algorithmColor,
          fontSize: 12,
          fontWeight: 750,
        }}
      >
        {props.algorithm.label} · {props.algorithm.profile}
      </div>

      <FaceDisplay
        pose={pose}
        style={props.style}
        waveform={props.waveform}
        waveformSamplesPerBin={props.waveformSamplesPerBin}
        packets={props.packets}
        events={props.streamEvents}
        currentSample={currentSample}
        sampleRate={props.sampleRate}
      />

      <div
        style={{
          ...panel,
          position: 'absolute',
          left: 706,
          top: DISPLAY_Y,
          width: 536,
          height: DISPLAY_HEIGHT,
          borderRadius: 24,
          overflow: 'hidden',
        }}
      >
        <div
          style={{
            padding: '15px 17px 13px',
            borderBottom: '1px solid #214158',
          }}
        >
          <div style={{fontSize: 26, fontWeight: 850, letterSpacing: -0.5}}>
            {props.title}
          </div>
          <div
            style={{
              ...mono,
              color: '#8ba5b6',
              fontSize: 12,
              marginTop: 5,
            }}
          >
            case/{props.testCase} · voice/{props.voice} · style/{props.style.name}
          </div>
        </div>

        <div
          style={{
            display: 'grid',
            gridTemplateColumns: '1fr 1fr 1fr',
          }}
        >
          <Metric
            label="PLAYOUT"
            value={`${pose.clock_ms} ms`}
            accent="#60d394"
          />
          <Metric
            label="VISEME"
            value={visemeName(pose.state.viseme)}
            accent={algorithmColor}
          />
          <Metric
            label="CONFIDENCE"
            value={
              props.algorithm.id === 'viseme'
                ? `${Math.round((pose.state.confidence / 255) * 100)}%`
                : 'n/a'
            }
          />
          <Metric
            label="MOUTH"
            value={`${pose.state.mouth_open}/255`}
            accent="#ff8d81"
          />
          <Metric
            label="PROTOTYPE"
            value={
              pose.state.phoneme === 255
                ? '—'
                : `#${pose.state.phoneme}`
            }
          />
          <Metric
            label="WORKING RAM"
            value={`${(props.algorithm.stateBytes / 1024).toFixed(1)} KiB`}
          />
          <Metric
            label="GROK CHUNK"
            value={packet ? `#${packet.index}` : 'release'}
            accent="#58a6ff"
          />
          <Metric
            label="PCM MEAN"
            value={pose.analysis.mean_abs.toLocaleString()}
          />
          <Metric
            label="EVENT"
            value={
              protocolEvent
                ? protocolEvent.protocol_event
                    .replace('response.output_audio_', '')
                    .replace('response.', '')
                : '—'
            }
            accent="#c4b5fd"
          />
        </div>

        <div style={{padding: '13px 16px'}}>
          <div
            style={{
              ...mono,
              color: algorithmColor,
              fontSize: 11,
              letterSpacing: 1.4,
              marginBottom: 5,
            }}
          >
            IMPLEMENTATION PROPERTIES
          </div>
          <div
            style={{
              ...mono,
              color: '#d6e2ea',
              fontSize: 12,
              lineHeight: 1.45,
            }}
          >
            {props.algorithm.propertySummary}
          </div>
          <div
            style={{
              color: '#8ca5b5',
              fontSize: 13,
              lineHeight: 1.35,
              marginTop: 8,
            }}
          >
            {props.algorithm.tradeoff}
          </div>
        </div>
      </div>

      <div
        style={{
          position: 'absolute',
          left: 40,
          right: 40,
          bottom: 25,
          height: 105,
          borderTop: '1px solid #244258',
          paddingTop: 15,
        }}
      >
        <div
          style={{
            ...mono,
            color: '#69889c',
            fontSize: 10,
            letterSpacing: 1.5,
            marginBottom: 7,
          }}
        >
          SAMPLE-CLOCKED ASSISTANT TRANSCRIPT · YELLOW LINES ARE PROVIDER EVENTS
        </div>
        <div
          style={{
            fontSize: 24,
            lineHeight: 1.25,
            fontWeight: 620,
            color: transcript ? '#e8f0f5' : '#6f8797',
          }}
        >
          {transcript ? `“${transcript}”` : 'Waiting for transcript event…'}
        </div>
      </div>

      <div
        style={{
          ...mono,
          position: 'absolute',
          right: 42,
          bottom: 7,
          color: '#49687b',
          fontSize: 8,
        }}
      >
        PCM {props.pcmSha256.slice(0, 16)}… · {props.model}
      </div>
    </AbsoluteFill>
  );
};
