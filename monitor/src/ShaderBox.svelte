<script lang="ts">
  // Supported uniform types:
  // Name         |  JS type                                |  GLSL type     |  Notes
  // scalar       |  number                                 |  float         |
  // vector       |  { values: number[], length: number }   |  float[], int  |  length must be <= values.length
  // buffer       |  { buffer: Uint8Array, length: number } |  uint[], int   |  buffer Uint8Array must be 4 byte aligned. GLSL uint[] has 4 Uint8Array values per element. GLSL length is JS length / 4

  type UniformScalarValue = number
  interface UniformVectorValue {
    values: number[] // values must be a constant length that the shader knows at compile time (values.length == MAX_LENGTH)
    length: number // length is the actual count of values, the rest are ignored by the shader (length <= MAX_LENGTH)
  }
  interface UniformBufferValue {
    buffer: Uint8Array
    length: number
  }

  type UniformScalarLocation = WebGLUniformLocation
  interface UniformVectorLocation {
    values: WebGLUniformLocation
    length: WebGLUniformLocation
  }
  interface UniformBufferLocation {
    buffer: WebGLUniformLocation
    length: WebGLUniformLocation
  }

  export let width = 0
  export let height = 0
  export let vertShaderCode = null
  export let fragShaderCode = null
  export let uniforms: { [key: string]: UniformScalarValue | UniformVectorValue | UniformBufferValue } = null

  let dpr = window.devicePixelRatio

  let glInitDone = false
  let canvasElem: HTMLCanvasElement
  let gl: WebGLRenderingContext
  let program: WebGLProgram
  let uniformLocs: { [key: string]: UniformScalarLocation | UniformVectorLocation | UniformBufferLocation } | null = null

  const createShader = (type: number, code: string) => {
    const shader = gl.createShader(type)
    gl.shaderSource(shader, code)
    gl.compileShader(shader)

    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      console.warn(`Error compiling ${type === gl.VERTEX_SHADER ? 'vertex' : 'fragment'} shader:`)
      console.warn(gl.getShaderInfoLog(shader))
      return null
    }

    return shader
  }

  const initGL = () => {
    gl = canvasElem.getContext('webgl2')

    const vertShader = createShader(gl.VERTEX_SHADER, vertShaderCode)
    const fragShader = createShader(gl.FRAGMENT_SHADER, fragShaderCode)
    if (!vertShader || !fragShader) return

    program = gl.createProgram()
    gl.attachShader(program, vertShader)
    gl.attachShader(program, fragShader)
    gl.linkProgram(program)

    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      console.warn('Error linking shader program:')
      console.warn(gl.getProgramInfoLog(program))
      return
    }

    gl.useProgram(program)

    uniformLocs = {}
    for (const [name, val] of Object.entries(uniforms)) {
      if (typeof val === 'number') { // UniformScalarValue
        uniformLocs[name] = gl.getUniformLocation(program, name)
      } else if ('values' in val) { // UniformVectorValue
        uniformLocs[name] = {
          values: gl.getUniformLocation(program, name),
          length: gl.getUniformLocation(program, name + 'Length')
        }
      } else if ('buffer' in val) { // UniformBufferValue
        uniformLocs[name] = {
          buffer: gl.getUniformLocation(program, name),
          length: gl.getUniformLocation(program, name + 'Length')
        }
      } else {
        throw new Error('ShaderBox: invalid uniforms property.')
      }
    }

    const uScalingFactor = gl.getUniformLocation(program, 'uScalingFactor')
    gl.uniform2fv(uScalingFactor, [dpr, dpr])

    const vertArray = new Float32Array([-0.5, 0.5, 0.5, 0.5, 0.5, -0.5, -0.5, 0.5, 0.5, -0.5, -0.5, -0.5])
    const vertBuf = gl.createBuffer()
    const vertNumComponents = 2
    const vertCount = vertArray.length / vertNumComponents

    gl.bindBuffer(gl.ARRAY_BUFFER, vertBuf)
    gl.bufferData(gl.ARRAY_BUFFER, vertArray, gl.STATIC_DRAW)

    const aVertPos = gl.getAttribLocation(program, 'VertPos')

    gl.enableVertexAttribArray(aVertPos)
    gl.vertexAttribPointer(aVertPos, vertNumComponents, gl.FLOAT, false, 0, 0)

    gl.drawArrays(gl.TRIANGLES, 0, vertCount)
    glInitDone = true
  }

  $: if (glInitDone) {
    for (const [name, val] of Object.entries(uniforms)) {
      if (typeof val === 'number') { // UniformScalarValue
        const loc = uniformLocs[name] as UniformScalarLocation
        // Some uniforms may have been optimised away if unused so check before setting
        if (loc !== null) gl.uniform1f(loc, val)
      } else if ('values' in val) { // UniformVectorValue
        const locs = uniformLocs[name] as UniformVectorLocation
        if (locs.values !== null) gl.uniform1fv(locs.values, val.values)
        if (locs.length !== null) gl.uniform1i(locs.length, val.length)
      } else if ('buffer' in val) { // UniformBufferValue
        const locs = uniformLocs[name] as UniformBufferLocation
        if (locs.buffer !== null) gl.uniform1uiv(locs.buffer, new Uint32Array(val.buffer.buffer))
        if (locs.length !== null) gl.uniform1i(locs.length, val.length)
      }
    }

    gl.drawArrays(gl.TRIANGLES, 0, 6)
  }

  $: if (
    !glInitDone &&
    canvasElem &&
    vertShaderCode &&
    fragShaderCode &&
    uniforms &&
    width > 0 &&
    height > 0) {
      // DEBUG: this timeout hack sux but i've got other things to do
      setTimeout(initGL, 100)
    }
</script>

<canvas class={$$props.class} width={dpr*width} height={dpr*height} bind:this={canvasElem} style="width: {width}px; height: {height}px;">
</canvas>

<style>
  canvas {
    background-color: white;
  }
</style>
