import React from 'react';
import {
  AbsoluteFill,
  interpolate,
  OffthreadVideo,
  Sequence,
  staticFile,
  useCurrentFrame,
} from 'remotion';
import type {
  ShowcaseProps,
  ShowcaseSlideSegment,
} from './types';

const mono: React.CSSProperties = {
  fontFamily:
    '"SFMono-Regular", "Cascadia Code", "Roboto Mono", Menlo, monospace',
};

const NarrativeSlide: React.FC<{
  slide: ShowcaseSlideSegment;
}> = ({slide}) => {
  const frame = useCurrentFrame();
  const opacity = interpolate(
    frame,
    [0, 8, slide.durationInFrames - 8, slide.durationInFrames],
    [0, 1, 1, 0],
    {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'},
  );
  const lift = interpolate(frame, [0, 14], [18, 0], {
    extrapolateLeft: 'clamp',
    extrapolateRight: 'clamp',
  });

  return (
    <AbsoluteFill
      style={{
        opacity,
        background:
          'radial-gradient(circle at 25% 40%, #14364a 0%, #071521 47%, #03090e 100%)',
        color: '#f4f8ff',
        padding: '74px 94px',
      }}
    >
      <div
        style={{
          position: 'absolute',
          left: 0,
          top: 0,
          bottom: 0,
          width: 11,
          background: slide.accentColor,
          boxShadow: `0 0 35px ${slide.accentColor}88`,
        }}
      />
      <div
        style={{
          ...mono,
          color: slide.accentColor,
          fontSize: 16,
          fontWeight: 850,
          letterSpacing: 2.7,
          marginBottom: 23,
        }}
      >
        {slide.kicker}
      </div>
      <div
        style={{
          transform: `translateY(${lift}px)`,
          fontSize: 61,
          lineHeight: 1.02,
          letterSpacing: -2.4,
          fontWeight: 900,
          maxWidth: 1040,
        }}
      >
        {slide.title}
      </div>
      <div
        style={{
          marginTop: 26,
          maxWidth: 990,
          fontSize: 25,
          lineHeight: 1.38,
          color: '#bbceda',
          fontWeight: 520,
        }}
      >
        {slide.body}
      </div>
      {slide.bullets.length > 0 ? (
        <div
          style={{
            marginTop: 31,
            display: 'grid',
            gridTemplateColumns:
              slide.bullets.length > 2 ? '1fr 1fr' : '1fr',
            gap: '13px 34px',
            maxWidth: 1080,
          }}
        >
          {slide.bullets.map((bullet) => (
            <div
              key={bullet}
              style={{
                display: 'flex',
                alignItems: 'flex-start',
                gap: 13,
                fontSize: 19,
                lineHeight: 1.35,
                color: '#e1ebf1',
              }}
            >
              <span style={{color: slide.accentColor, fontWeight: 900}}>→</span>
              <span>{bullet}</span>
            </div>
          ))}
        </div>
      ) : null}
      <div
        style={{
          ...mono,
          position: 'absolute',
          left: 95,
          bottom: 40,
          color: '#557489',
          fontSize: 12,
          letterSpacing: 1.4,
        }}
      >
        {slide.footer ?? 'STACKCHAN · PCM FACE ALGORITHM LAB'}
      </div>
    </AbsoluteFill>
  );
};

export const FaceShowcase: React.FC<ShowcaseProps> = ({segments}) => {
  let offset = 0;
  return (
    <AbsoluteFill style={{background: '#03090e'}}>
      {segments.map((segment, index) => {
        const from = offset;
        offset += segment.durationInFrames;
        return (
          <Sequence
            key={`${segment.kind}-${index}`}
            from={from}
            durationInFrames={segment.durationInFrames}
            premountFor={15}
          >
            {segment.kind === 'slide' ? (
              <NarrativeSlide slide={segment} />
            ) : (
              <OffthreadVideo
                src={staticFile(segment.file)}
                style={{width: '100%', height: '100%'}}
              />
            )}
          </Sequence>
        );
      })}
    </AbsoluteFill>
  );
};
