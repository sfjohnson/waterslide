#version 300 es
precision highp float;

#define COLOUR_COUNT 4
#define BINS_WINDOW 10 // each bin is averaged with this many bins before it
#define BINS_OFFSET 0.03

in vec2 Pos;
out vec4 FragColour;
uniform sampler2D uBins;
uniform int uBinsWidth;

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

  int binPos = int(float(uBinsWidth) * Pos.x);
  vec4 binVal = texelFetch(uBins, ivec2(binPos, 0), 0);
  if (binVal.g == 0.0 || binPos < BINS_WINDOW) {
    FragColour = vec4(0.9, 0.9, 0.9, 1.0);
    return;
  }

  float relTime = binVal.r;
  for (int i = 0; i < BINS_WINDOW - 1; i++) {
    relTime += texelFetch(uBins, ivec2(binPos - i - 1, 0), 0).r;
  }

  FragColour = vec4(toHeatmapColour(relTime / float(BINS_WINDOW) - BINS_OFFSET), 1.0);
}
