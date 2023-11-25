// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#version 300 es

in vec2 VertPos;
out vec2 Pos;
uniform vec2 uScalingFactor;

void main () {
  Pos = 0.5 * (1.0 + uScalingFactor * VertPos);
  Pos.y = 1.0 - Pos.y;
  gl_Position = vec4(VertPos * uScalingFactor, 0.0, 1.0);
}
