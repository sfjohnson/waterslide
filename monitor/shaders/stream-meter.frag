#version 300 es
precision highp float;

#define CLIPPING_MARKER_HEIGHT 0.01
#define STREAM_METER_BINS 512
#define WINDOW_SIZE 0.04
#define WINDOW_SCALE 0.3
#define COLOUR_COUNT 4

in vec2 Pos;
out vec4 FragColour;
uniform uint uBins[STREAM_METER_BINS / 4];
uniform int uBinsLength;
uniform float uUnderrun;
uniform float uOverrun;

vec3 colours[COLOUR_COUNT];

// https://www.andrewnoske.com/wiki/Code_-_heatmaps_and_color_gradients
// returns vec3(r, g, b)
vec3 toHeatmapColour (float x) {
  int idx1;       // |-- Our desired color will be between these two indexes in "colour".
  int idx2;       // |
  float fractBetween = 0.0;   // Fraction between "idx1" and "idx2" where our value is.
  
  if (x <= 0.0) {
    idx1 = idx2 = 0;
  } else if (x >= 1.0) {
    idx1 = idx2 = COLOUR_COUNT - 1;
  } else {
    x = x * float(COLOUR_COUNT - 1);  // Will multiply value by 3.
    idx1  = int(x);                     // Our desired color will be after this index.
    idx2  = idx1 + 1;                   // ... and before this index (inclusive).
    fractBetween = x - float(idx1);     // Distance between the two indexes (0-1).
  }

  return vec3(
    fractBetween * (colours[idx2].r - colours[idx1].r) + colours[idx1].r,
    fractBetween * (colours[idx2].g - colours[idx1].g) + colours[idx1].g,
    fractBetween * (colours[idx2].b - colours[idx1].b) + colours[idx1].b
  );
}

void main () {
  colours[0] = vec3(0.737, 0.310, 1.0);
  colours[1] = vec3(0.310, 0.757, 1.0);
  colours[2] = vec3(0.851, 1.0, 0.310);
  colours[3] = vec3(1.0, 0.369, 0.310);

  if (Pos.y <= CLIPPING_MARKER_HEIGHT) {
    if (uOverrun > 0.0) {
      FragColour = vec4(1.0, 0.0, 0.0, 1.0);
    } else {
      FragColour = vec4(0.8, 0.8, 0.8, 1.0);
    }
    return;
  } else if (Pos.y >= 1.0 - CLIPPING_MARKER_HEIGHT) {
    if (uUnderrun > 0.0) {
      FragColour = vec4(1.0, 0.0, 0.0, 1.0);
    } else {
      FragColour = vec4(0.8, 0.8, 0.8, 1.0);
    }
    return;
  }

  float fragPos = 1.0 - Pos.y + 2.0*CLIPPING_MARKER_HEIGHT * (Pos.y - 0.5);
  float total = 0.0;
  int totalCount = 0;
  for (int i = 0; i < STREAM_METER_BINS; i++) {
    // unpack uint8 from uBins
    float binsVal = float((uBins[i/4] >> 8*(i%4)) & 0xffu);
    float binPos = float(i) / float(STREAM_METER_BINS - 1);
    float weight = WINDOW_SIZE - abs(fragPos - binPos);
    if (weight > 0.0 && binsVal > 0.0) {
      total += WINDOW_SCALE * weight * binsVal;
      totalCount++;
    }
  }

  if (totalCount == 0) {
    FragColour = vec4(0.0, 0.0, 0.0, 1.0);
  }

  total /= float(totalCount);
  FragColour = vec4(toHeatmapColour(total), 1.0);
}
