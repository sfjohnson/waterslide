#version 300 es
precision highp float;

#define MAX_AUDIO_CHANNELS 64
#define CLIPPING_MARKER_HEIGHT 0.01
#define PEAK_MARKER_HEIGHT 0.003
#define CHANNEL_WIDTH 0.7
#define CHANNEL_GAP 0.3

in vec2 Pos;
out vec4 FragColour;
uniform float uLevels[MAX_AUDIO_CHANNELS];
uniform float uPeaks[MAX_AUDIO_CHANNELS];
uniform float uClipping[MAX_AUDIO_CHANNELS];
uniform int uLevelsLength, uPeaksLength, uClippingLength;

void main () {
  float totalChannelWidth = (CHANNEL_WIDTH + CHANNEL_GAP) / float(uLevelsLength);
  int channel = int(Pos.x / totalChannelWidth);
  float xWithinChannel = mod(Pos.x, totalChannelWidth);
  if (channel >= uLevelsLength || xWithinChannel > CHANNEL_WIDTH / float(uLevelsLength)) {
    FragColour = vec4(1.0, 1.0, 1.0, 1.0);
    return;
  }

  float level = CLIPPING_MARKER_HEIGHT + uLevels[channel] * (1.0 - CLIPPING_MARKER_HEIGHT);
  float peak = CLIPPING_MARKER_HEIGHT + uPeaks[channel] * (1.0 - CLIPPING_MARKER_HEIGHT);

  if (Pos.y <= CLIPPING_MARKER_HEIGHT) {
    if (uClipping[channel] > 0.0) {
      FragColour = vec4(1.0, 0.0, 0.0, 1.0);
    } else {
      FragColour = vec4(0.8, 0.8, 0.8, 1.0);
    }
    return;
  }

  float peakEasing = 1.3 * max(1.0 - abs(Pos.y - peak)/PEAK_MARKER_HEIGHT, 0.0);
  vec3 resultColour;

  if (Pos.y > level) {
    resultColour = vec3(0.59, 0.84, 0.52);
  } else {
    resultColour = vec3(0.0, 0.0, 0.0);
  }

  resultColour = peakEasing * vec3(1.0, 1.0, 0.0) + (1.0-peakEasing) * resultColour;
  FragColour = vec4(resultColour, 1.0);
}
