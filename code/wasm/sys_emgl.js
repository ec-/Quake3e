// TODO: can call all EMGL functions or re.* renderer2 export functions from scripts
//   Maybe this is where the paralel frame buffering comes in, or treat the javascript
//   like update calls and extrapolate.


function __webgl_enable_WEBGL_draw_instanced_base_vertex_base_instance(ctx) {
  return !!(ctx.dibvbi = ctx.getExtension("WEBGL_draw_instanced_base_vertex_base_instance"));
}

function __webgl_enable_WEBGL_multi_draw_instanced_base_vertex_base_instance(ctx) {
  return !!(ctx.mdibvbi = ctx.getExtension("WEBGL_multi_draw_instanced_base_vertex_base_instance"));
}

function __webgl_enable_WEBGL_multi_draw(ctx) {
  return !!(ctx.multiDrawWebgl = ctx.getExtension("WEBGL_multi_draw"));
}

var GL = {
  counter: 1,
  buffers: [],
  mappedBuffers: {},
  programs: [],
  framebuffers: [],
  renderbuffers: [],
  textures: [],
  shaders: [],
  vaos: [],
  contexts: [],
  offscreenCanvases: {},
  queries: [],
  samplers: [],
  transformFeedbacks: [],
  syncs: [],
  byteSizeByTypeRoot: 5120,
  byteSizeByType: [1, 1, 2, 2, 4, 4, 4, 2, 3, 4, 8],
  stringCache: {},
  stringiCache: {},
  unpackAlignment: 4,
  recordError: function recordError(errorCode) {
    if (!GL.lastError) {
      GL.lastError = errorCode;
    }
  },
  getNewId: function (table) {
    var ret = GL.counter++;
    for (var i = table.length; i < ret; i++) {
      table[i] = null;
    }
    return ret;
  },
  MAX_TEMP_BUFFER_SIZE: 2097152,
  numTempVertexBuffersPerSize: 64,
  log2ceilLookup: function (i) {
    return 32 - Math.clz32(i === 0 ? 0 : i - 1);
  },
  generateTempBuffers: function (quads, context) {
    var largestIndex = GL.log2ceilLookup(GL.MAX_TEMP_BUFFER_SIZE);
    context.tempVertexBufferCounters1 = [];
    context.tempVertexBufferCounters2 = [];
    context.tempVertexBufferCounters1.length = context.tempVertexBufferCounters2.length = largestIndex + 1;
    context.tempVertexBuffers1 = [];
    context.tempVertexBuffers2 = [];
    context.tempVertexBuffers1.length = context.tempVertexBuffers2.length = largestIndex + 1;
    context.tempIndexBuffers = [];
    context.tempIndexBuffers.length = largestIndex + 1;
    for (var i = 0; i <= largestIndex; ++i) {
      context.tempIndexBuffers[i] = null;
      context.tempVertexBufferCounters1[i] = context.tempVertexBufferCounters2[i] = 0;
      var ringbufferLength = GL.numTempVertexBuffersPerSize;
      context.tempVertexBuffers1[i] = [];
      context.tempVertexBuffers2[i] = [];
      var ringbuffer1 = context.tempVertexBuffers1[i];
      var ringbuffer2 = context.tempVertexBuffers2[i];
      ringbuffer1.length = ringbuffer2.length = ringbufferLength;
      for (var j = 0; j < ringbufferLength; ++j) {
        ringbuffer1[j] = ringbuffer2[j] = null;
      }
    }
    if (quads) {
      context.tempQuadIndexBuffer = GLctx.createBuffer();
      context.GLctx.bindBuffer(34963, context.tempQuadIndexBuffer);
      var numIndexes = GL.MAX_TEMP_BUFFER_SIZE >> 1;
      var quadIndexes = new Uint16Array(numIndexes);
      var i = 0, v = 0;
      while (1) {
        quadIndexes[i++] = v;
        if (i >= numIndexes) break;
        quadIndexes[i++] = v + 1;
        if (i >= numIndexes) break;
        quadIndexes[i++] = v + 2;
        if (i >= numIndexes) break;
        quadIndexes[i++] = v;
        if (i >= numIndexes) break;
        quadIndexes[i++] = v + 2;
        if (i >= numIndexes) break;
        quadIndexes[i++] = v + 3;
        if (i >= numIndexes) break;
        v += 4;
      }
      context.GLctx.bufferData(34963, quadIndexes, 35044);
      context.GLctx.bindBuffer(34963, null);
    }
  },
  getTempVertexBuffer: function getTempVertexBuffer(sizeBytes) {
    var idx = GL.log2ceilLookup(sizeBytes);
    var ringbuffer = GL.currentContext.tempVertexBuffers1[idx];
    var nextFreeBufferIndex = GL.currentContext.tempVertexBufferCounters1[idx];
    GL.currentContext.tempVertexBufferCounters1[idx] = GL.currentContext.tempVertexBufferCounters1[idx] + 1 & GL.numTempVertexBuffersPerSize - 1;
    var vbo = ringbuffer[nextFreeBufferIndex];
    if (vbo) {
      return vbo;
    }
    var prevVBO = GLctx.getParameter(34964);
    ringbuffer[nextFreeBufferIndex] = GLctx.createBuffer();
    GLctx.bindBuffer(34962, ringbuffer[nextFreeBufferIndex]);
    GLctx.bufferData(34962, 1 << idx, 35048);
    GLctx.bindBuffer(34962, prevVBO);
    return ringbuffer[nextFreeBufferIndex];
  },
  getTempIndexBuffer: function getTempIndexBuffer(sizeBytes) {
    var idx = GL.log2ceilLookup(sizeBytes);
    var ibo = GL.currentContext.tempIndexBuffers[idx];
    if (ibo) {
      return ibo;
    }
    var prevIBO = GLctx.getParameter(34965);
    GL.currentContext.tempIndexBuffers[idx] = GLctx.createBuffer();
    GLctx.bindBuffer(34963, GL.currentContext.tempIndexBuffers[idx]);
    GLctx.bufferData(34963, 1 << idx, 35048);
    GLctx.bindBuffer(34963, prevIBO);
    return GL.currentContext.tempIndexBuffers[idx];
  },
  newRenderingFrameStarted: function newRenderingFrameStarted() {
    if (!GL.currentContext) {
      return;
    }
    var vb = GL.currentContext.tempVertexBuffers1;
    GL.currentContext.tempVertexBuffers1 = GL.currentContext.tempVertexBuffers2;
    GL.currentContext.tempVertexBuffers2 = vb;
    vb = GL.currentContext.tempVertexBufferCounters1;
    GL.currentContext.tempVertexBufferCounters1 = GL.currentContext.tempVertexBufferCounters2;
    GL.currentContext.tempVertexBufferCounters2 = vb;
    var largestIndex = GL.log2ceilLookup(GL.MAX_TEMP_BUFFER_SIZE);
    for (var i = 0; i <= largestIndex; ++i) {
      GL.currentContext.tempVertexBufferCounters1[i] = 0;
    }
  },
  getSource: function (shader, count, string, length) {
    var source = "";
    for (var i = 0; i < count; ++i) {
      var len = length ? SAFE_HEAP_LOAD(length + i * 4 | 0, 4, 0) | 0 : -1;
      source += addressToString(SAFE_HEAP_LOAD(string + i * 4 | 0, 4, 0) | 0, len < 0 ? undefined : len);
    }
    return source;
  },
  calcBufLength: function calcBufLength(size, type, stride, count) {
    if (stride > 0) {
      return count * stride;
    }
    var typeSize = GL.byteSizeByType[type - GL.byteSizeByTypeRoot];
    return size * typeSize * count;
  },
  usedTempBuffers: [],
  preDrawHandleClientVertexAttribBindings: function preDrawHandleClientVertexAttribBindings(count) {
    GL.resetBufferBinding = false;
    for (var i = 0; i < GL.currentContext.maxVertexAttribs; ++i) {
      var cb = GL.currentContext.clientBuffers[i];
      if (!cb.clientside || !cb.enabled) continue;
      GL.resetBufferBinding = true;
      var size = GL.calcBufLength(cb.size, cb.type, cb.stride, count);
      var buf = GL.getTempVertexBuffer(size);
      GLctx.bindBuffer(34962, buf);
      GLctx.bufferSubData(34962, 0, HEAPU8.subarray(cb.ptr, cb.ptr + size));
      cb.vertexAttribPointerAdaptor.call(GLctx, i, cb.size, cb.type, cb.normalized, cb.stride, 0);
    }
  },
  postDrawHandleClientVertexAttribBindings: function postDrawHandleClientVertexAttribBindings() {
    if (GL.resetBufferBinding) {
      GLctx.bindBuffer(34962, GL.buffers[GLctx.currentArrayBufferBinding]);
    }
  },
  createContext: function (canvas, webGLContextAttributes) {
    if (!canvas.getContextSafariWebGL2Fixed) {
      canvas.getContextSafariWebGL2Fixed = canvas.getContext;
      canvas.getContext = function (ver, attrs) {
        var gl = canvas.getContextSafariWebGL2Fixed(ver, attrs);
        return ver == "webgl" == gl instanceof WebGLRenderingContext ? gl : null;
      };
    }
    var ctx = canvas.getContext("webgl2", webGLContextAttributes);
    if (!ctx) return 0;
    var handle = GL.registerContext(ctx, webGLContextAttributes);
    return handle;
  },
  registerContext: function (ctx, webGLContextAttributes) {
    var handle = GL.getNewId(GL.contexts);
    var context = {
      handle: handle,
      attributes: webGLContextAttributes,
      version: webGLContextAttributes.majorVersion,
      GLctx: ctx
    };
    if (ctx.canvas) ctx.canvas.GLctxObject = context;
    GL.contexts[handle] = context;
    if (typeof webGLContextAttributes.enableExtensionsByDefault === "undefined" || webGLContextAttributes.enableExtensionsByDefault) {
      GL.initExtensions(context);
    }
    context.maxVertexAttribs = context.GLctx.getParameter(34921);
    context.clientBuffers = [];
    for (var i = 0; i < context.maxVertexAttribs; i++) {
      context.clientBuffers[i] = {
        enabled: false,
        clientside: false,
        size: 0,
        type: 0,
        normalized: 0,
        stride: 0,
        ptr: 0,
        vertexAttribPointerAdaptor: null
      };
    }
    GL.generateTempBuffers(false, context);
    return handle;
  },
  makeContextCurrent: function (contextHandle) {
    GL.currentContext = GL.contexts[contextHandle];
    window.ctx = GLctx = GL.currentContext && GL.currentContext.GLctx;
    return !(contextHandle && !GLctx);
  },
  getContext: function (contextHandle) {
    return GL.contexts[contextHandle];
  },
  deleteContext: function (contextHandle) {
    if (GL.currentContext === GL.contexts[contextHandle]) GL.currentContext = null;
    if (typeof JSEvents === "object") JSEvents.removeAllHandlersOnTarget(GL.contexts[contextHandle].GLctx.canvas);
    if (GL.contexts[contextHandle] && GL.contexts[contextHandle].GLctx.canvas) GL.contexts[contextHandle].GLctx.canvas.GLctxObject = undefined;
    GL.contexts[contextHandle] = null;
  },
  initExtensions: function (context) {
    if (!context) context = GL.currentContext;
    if (context.initExtensionsDone) return;
    context.initExtensionsDone = true;
    var GLctx = context.GLctx;
    __webgl_enable_WEBGL_draw_instanced_base_vertex_base_instance(GLctx);
    __webgl_enable_WEBGL_multi_draw_instanced_base_vertex_base_instance(GLctx);
    if (context.version >= 2) {
      GLctx.disjointTimerQueryExt = GLctx.getExtension("EXT_disjoint_timer_query_webgl2");
    }
    if (context.version < 2 || !GLctx.disjointTimerQueryExt) {
      GLctx.disjointTimerQueryExt = GLctx.getExtension("EXT_disjoint_timer_query");
    }
    __webgl_enable_WEBGL_multi_draw(GLctx);
    var exts = GLctx.getSupportedExtensions() || [];
    exts.forEach(function (ext) {
      if (!ext.includes("lose_context") && !ext.includes("debug")) {
        GLctx.getExtension(ext);
      }
    });
  }
};

function _getTempRet0() {
  return getTempRet0();
}

function _glActiveTexture(x0) {
  GLctx["activeTexture"](x0);
}

function _glAlphaFunc() {
  debugger;
}

function _glArrayElement() {
  debugger;
}

function _glAttachShader(program, shader) {
  GLctx.attachShader(GL.programs[program], GL.shaders[shader]);
}

function _glBegin() {
  throw "Legacy GL function (glBegin) called. If you want legacy GL emulation, you need to compile with -s LEGACY_GL_EMULATION=1 to enable legacy GL emulation.";
}

function _glBindAttribLocation(program, index, name) {
  GLctx.bindAttribLocation(GL.programs[program], index, addressToString(name));
}

function _glBindBuffer(target, buffer) {
  if (target == 34962) {
    GLctx.currentArrayBufferBinding = buffer;
  } else if (target == 34963) {
    GLctx.currentElementArrayBufferBinding = buffer;
  }
  if (target == 35051) {
    GLctx.currentPixelPackBufferBinding = buffer;
  } else if (target == 35052) {
    GLctx.currentPixelUnpackBufferBinding = buffer;
  }
  GLctx.bindBuffer(target, GL.buffers[buffer]);
}

function _glBindFramebuffer(target, framebuffer) {
  GLctx.bindFramebuffer(target, GL.framebuffers[framebuffer]);
}

function _glBindRenderbuffer(target, renderbuffer) {
  GLctx.bindRenderbuffer(target, GL.renderbuffers[renderbuffer]);
}

function _glBindTexture(target, texture) {
  GLctx.bindTexture(target, GL.textures[texture]);
  EMGL.previousTex = texture
  if (typeof EMGL.texFiles[texture] != 'undefined') {
    EMGL.previousImage = EMGL.texFiles[texture][1]
  } else {
    EMGL.previousImage = null
  }
}

function _glBindVertexArray(vao) {
  GLctx["bindVertexArray"](GL.vaos[vao]);
  var ibo = GLctx.getParameter(34965);
  GLctx.currentElementArrayBufferBinding = ibo ? ibo.name | 0 : 0;
}

function _glBlendFunc(x0, x1) {
  GLctx["blendFunc"](x0, x1);
}

function _glBlitFramebuffer(x0, x1, x2, x3, x4, x5, x6, x7, x8, x9) {
  GLctx["blitFramebuffer"](x0, x1, x2, x3, x4, x5, x6, x7, x8, x9);
}

function _glBufferData(target, size, data, usage) {
  if (true) {
    if (data) {
      GLctx.bufferData(target, HEAPU8, usage, data, size);
    } else {
      GLctx.bufferData(target, size, usage);
    }
  } else {
    GLctx.bufferData(target, data ? HEAPU8.subarray(data, data + size) : size, usage);
  }
}

function _glBufferSubData(target, offset, size, data) {
  if (true) {
    GLctx.bufferSubData(target, offset, HEAPU8, data, size);
    return;
  }
  GLctx.bufferSubData(target, offset, HEAPU8.subarray(data, data + size));
}

function _glCheckFramebufferStatus(x0) {
  return GLctx["checkFramebufferStatus"](x0);
}

function _glClear(x0) {
  GLctx["clear"](x0);
}

function _glClearColor(x0, x1, x2, x3) {
  GLctx["clearColor"](x0, x1, x2, x3);
}

function _glClearDepth(x0) {
  GLctx["clearDepth"](x0);
}

function _glClearStencil(x0) {
  GLctx["clearStencil"](x0);
}

function _glClipPlane() {
  debugger;
}

function _glColor3f() {
  debugger;
}

function _glColor4f() {
  debugger;
}

function _glColor4ubv() {
  debugger;
}

function _glColorMask(red, green, blue, alpha) {
  GLctx.colorMask(!!red, !!green, !!blue, !!alpha);
}

function _glColorPointer() {
  debugger;
}

function _glCompileShader(shader) {
  GLctx.compileShader(GL.shaders[shader]);
}

function _glCompressedTexImage2D(target, level, internalFormat, width, height, border, imageSize, data) {
  if (true) {
    if (GLctx.currentPixelUnpackBufferBinding) {
      GLctx["compressedTexImage2D"](target, level, internalFormat, width, height, border, imageSize, data);
    } else {
      GLctx["compressedTexImage2D"](target, level, internalFormat, width, height, border, HEAPU8, data, imageSize);
    }
    return;
  }
  GLctx["compressedTexImage2D"](target, level, internalFormat, width, height, border, data ? HEAPU8.subarray(data, data + imageSize) : null);
}

function _glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data) {
  if (true) {
    if (GLctx.currentPixelUnpackBufferBinding) {
      GLctx["compressedTexSubImage2D"](target, level, xoffset, yoffset, width, height, format, imageSize, data);
    } else {
      GLctx["compressedTexSubImage2D"](target, level, xoffset, yoffset, width, height, format, HEAPU8, data, imageSize);
    }
    return;
  }
  GLctx["compressedTexSubImage2D"](target, level, xoffset, yoffset, width, height, format, data ? HEAPU8.subarray(data, data + imageSize) : null);
}

function _glCopyTexSubImage2D(x0, x1, x2, x3, x4, x5, x6, x7) {
  GLctx["copyTexSubImage2D"](x0, x1, x2, x3, x4, x5, x6, x7);
}

function _glCreateProgram() {
  var id = GL.getNewId(GL.programs);
  var program = GLctx.createProgram();
  program.name = id;
  program.maxUniformLength = program.maxAttributeLength = program.maxUniformBlockNameLength = 0;
  program.uniformIdCounter = 1;
  GL.programs[id] = program;
  return id;
}

function _glCreateShader(shaderType) {
  var id = GL.getNewId(GL.shaders);
  GL.shaders[id] = GLctx.createShader(shaderType);
  return id;
}

function _glCullFace(x0) {
  GLctx["cullFace"](x0);
}

function _glDeleteBuffers(n, buffers) {
  for (var i = 0; i < n; i++) {
    var id = SAFE_HEAP_LOAD(buffers + i * 4 | 0, 4, 0) | 0;
    var buffer = GL.buffers[id];
    if (!buffer) continue;
    GLctx.deleteBuffer(buffer);
    buffer.name = 0;
    GL.buffers[id] = null;
    if (id == GLctx.currentArrayBufferBinding) GLctx.currentArrayBufferBinding = 0;
    if (id == GLctx.currentElementArrayBufferBinding) GLctx.currentElementArrayBufferBinding = 0;
    if (id == GLctx.currentPixelPackBufferBinding) GLctx.currentPixelPackBufferBinding = 0;
    if (id == GLctx.currentPixelUnpackBufferBinding) GLctx.currentPixelUnpackBufferBinding = 0;
  }
}

function _glDeleteFramebuffers(n, framebuffers) {
  for (var i = 0; i < n; ++i) {
    var id = SAFE_HEAP_LOAD(framebuffers + i * 4 | 0, 4, 0) | 0;
    var framebuffer = GL.framebuffers[id];
    if (!framebuffer) continue;
    GLctx.deleteFramebuffer(framebuffer);
    framebuffer.name = 0;
    GL.framebuffers[id] = null;
  }
}

function _glDeleteProgram(id) {
  if (!id) return;
  var program = GL.programs[id];
  if (!program) {
    GL.recordError(1281);
    return;
  }
  GLctx.deleteProgram(program);
  program.name = 0;
  GL.programs[id] = null;
}

function _glDeleteRenderbuffers(n, renderbuffers) {
  for (var i = 0; i < n; i++) {
    var id = SAFE_HEAP_LOAD(renderbuffers + i * 4 | 0, 4, 0) | 0;
    var renderbuffer = GL.renderbuffers[id];
    if (!renderbuffer) continue;
    GLctx.deleteRenderbuffer(renderbuffer);
    renderbuffer.name = 0;
    GL.renderbuffers[id] = null;
  }
}

function _glDeleteShader(id) {
  if (!id) return;
  var shader = GL.shaders[id];
  if (!shader) {
    GL.recordError(1281);
    return;
  }
  GLctx.deleteShader(shader);
  GL.shaders[id] = null;
}

function _glDeleteTextures(n, textures) {
  for (var i = 0; i < n; i++) {
    var id = SAFE_HEAP_LOAD(textures + i * 4 | 0, 4, 0) | 0;
    var texture = GL.textures[id];
    if (!texture) continue;
    GLctx.deleteTexture(texture);
    texture.name = 0;
    GL.textures[id] = null;
  }
}

function _glDeleteVertexArrays(n, vaos) {
  for (var i = 0; i < n; i++) {
    var id = SAFE_HEAP_LOAD(vaos + i * 4 | 0, 4, 0) | 0;
    GLctx["deleteVertexArray"](GL.vaos[id]);
    GL.vaos[id] = null;
  }
}

function _glDepthFunc(x0) {
  GLctx["depthFunc"](x0);
}

function _glDepthMask(flag) {
  GLctx.depthMask(!!flag);
}

function _glDepthRange(x0, x1) {
  GLctx["depthRange"](x0, x1);
}

function _glDetachShader(program, shader) {
  GLctx.detachShader(GL.programs[program], GL.shaders[shader]);
}

function _glDisable(x0) {
  GLctx["disable"](x0);
}

function _glDisableClientState() {
  debugger;
}

function _glDisableVertexAttribArray(index) {
  var cb = GL.currentContext.clientBuffers[index];
  cb.enabled = false;
  GLctx.disableVertexAttribArray(index);
}

function _glDrawArrays(mode, first, count) {
  GL.preDrawHandleClientVertexAttribBindings(first + count);
  GLctx.drawArrays(mode, first, count);
  GL.postDrawHandleClientVertexAttribBindings();
}

function _glDrawBuffer(buf) {
  GLctx["drawBuffers"]([buf]);
}

var tempFixedLengthArray = [];

function _glDrawBuffers(n, bufs) {
  var bufArray = tempFixedLengthArray[n];
  for (var i = 0; i < n; i++) {
    bufArray[i] = SAFE_HEAP_LOAD(bufs + i * 4 | 0, 4, 0) | 0;
  }
  GLctx["drawBuffers"](bufArray);
}

for (var i = 0; i < 32; ++i) tempFixedLengthArray.push(new Array(i));

function _glDrawElements(mode, count, type, indices) {
  var buf;
  if (!GLctx.currentElementArrayBufferBinding) {
    var size = GL.calcBufLength(1, type, 0, count);
    buf = GL.getTempIndexBuffer(size);
    GLctx.bindBuffer(34963, buf);
    GLctx.bufferSubData(34963, 0, HEAPU8.subarray(indices, indices + size));
    indices = 0;
  }
  GL.preDrawHandleClientVertexAttribBindings(count);
  GLctx.drawElements(mode, count, type, indices);
  GL.postDrawHandleClientVertexAttribBindings(count);
  if (!GLctx.currentElementArrayBufferBinding) {
    GLctx.bindBuffer(34963, null);
  }
}

function _glEnable(x0) {
  GLctx["enable"](x0);
}

function _glEnableClientState() {
  debugger;
}

function _glEnableVertexAttribArray(index) {
  var cb = GL.currentContext.clientBuffers[index];
  cb.enabled = true;
  GLctx.enableVertexAttribArray(index);
}

function _glEnd() {
  debugger;
}

function _glFinish() {
  GLctx["finish"]();
}

function _glFlush() {
  GLctx["flush"]();
}

function _glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer) {
  GLctx.framebufferRenderbuffer(target, attachment, renderbuffertarget, GL.renderbuffers[renderbuffer]);
}

function _glFramebufferTexture2D(target, attachment, textarget, texture, level) {
  GLctx.framebufferTexture2D(target, attachment, textarget, GL.textures[texture], level);
}

function _glFrustum() {
  debugger;
}

function __glGenObject(n, buffers, createFunction, objectTable) {
  for (var i = 0; i < n; i++) {
    var buffer = GLctx[createFunction]();
    var id = buffer && GL.getNewId(objectTable);
    if (buffer) {
      buffer.name = id;
      objectTable[id] = buffer;
    } else {
      GL.recordError(1282);
    }
    SAFE_HEAP_STORE(buffers + i * 4 | 0, id | 0, 4);
  }
}

function _glGenBuffers(n, buffers) {
  __glGenObject(n, buffers, "createBuffer", GL.buffers);
}

function _glGenFramebuffers(n, ids) {
  __glGenObject(n, ids, "createFramebuffer", GL.framebuffers);
}

function _glGenRenderbuffers(n, renderbuffers) {
  __glGenObject(n, renderbuffers, "createRenderbuffer", GL.renderbuffers);
}

function BmpEncoder(imgData){
	this.buffer = imgData.data;
	this.width = imgData.width;
	this.height = imgData.height;
	this.extraBytes = this.width%4;
	this.rgbSize = this.height*(4*this.width+this.extraBytes);
	this.headerInfoSize = 108;

	this.data = [];
	/******************header***********************/
	this.flag = "BM";
	this.reserved = 0;
	this.offset = 14 + this.headerInfoSize;
	this.fileSize = this.rgbSize+this.offset;
	this.planes = 1;
	this.bitPP = 32;
	this.compress = 3;
	this.hr = 0;
	this.vr = 0;
	this.colors = 0;
	this.importantColors = 0;
}

function writeLE(pos, buffer, int) {
  buffer.set([
    (int & 0xFF) >> 0, (int & 0xFF00) >> 8, 
    (int & 0xFF0000) >> 16, (int & 0xFF000000) >> 24, 
    ], pos)
}


BmpEncoder.prototype.encode = function() {
	var tempBuffer = new Uint8Array(this.offset+this.rgbSize);
	this.pos = 0;
	tempBuffer.set(this.flag.split('').map(c => c.charCodeAt(0)));
  this.pos+=2;
	writeLE(this.pos, tempBuffer, this.fileSize);this.pos+=4;
	writeLE(this.pos, tempBuffer, this.reserved);this.pos+=4;
	writeLE(this.pos, tempBuffer, this.offset);this.pos+=4;

	writeLE(this.pos, tempBuffer, this.headerInfoSize);this.pos+=4;
	writeLE(this.pos, tempBuffer, this.width);this.pos+=4;
	writeLE(this.pos, tempBuffer, this.height);this.pos+=4;
	tempBuffer.set([(this.planes & 0xFF), (this.planes & 0xFF00) >> 8],this.pos);this.pos+=2;
	tempBuffer.set([ (this.bitPP & 0xFF), (this.bitPP & 0xFF00) >> 8],this.pos);this.pos+=2;
	writeLE(this.pos, tempBuffer, this.compress);this.pos+=4;
	writeLE(this.pos, tempBuffer, this.rgbSize);this.pos+=4;
	writeLE(this.pos, tempBuffer, this.hr);this.pos+=4;
	writeLE(this.pos, tempBuffer, this.vr);this.pos+=4;
	writeLE(this.pos, tempBuffer, this.colors);this.pos+=4;
	writeLE(this.pos, tempBuffer, this.importantColors);this.pos+=4;

  writeLE(this.pos, tempBuffer, 0x00FF0000, this.pos); this.pos += 4;
  writeLE(this.pos, tempBuffer, 0x0000FF00, this.pos); this.pos += 4;
  writeLE(this.pos, tempBuffer, 0x000000FF, this.pos); this.pos += 4;
  writeLE(this.pos, tempBuffer, 0xFF000000, this.pos); this.pos += 4;

  tempBuffer.set(0x20, this.pos); this.pos++;
  tempBuffer.set(0x6E, this.pos); this.pos++;
  tempBuffer.set(0x69, this.pos); this.pos++;
  tempBuffer.set(0x57, this.pos); this.pos++;

  for (let i = 0; i < 48; i++) {
    tempBuffer.set(0, this.pos); this.pos++;
  }

	var i=0;
	var rowBytes = 4*this.width+this.extraBytes;

	for (var y = 0; y <this.height; y++){
		for (var x = 0; x < this.width; x++){
			var p = this.pos+y*rowBytes+x*4;
			i++;//a
			tempBuffer[p]= this.buffer[i++];//b
			tempBuffer[p+1] = this.buffer[i++];//g
			tempBuffer[p+2]  = this.buffer[i++];//r
			tempBuffer[p+3]  = this.buffer[i++];//a
		}
		if(this.extraBytes>0){
			var setOffset = this.pos+y*rowBytes+this.width*4;
			tempBuffer.set(0,setOffset,setOffset+this.extraBytes);
		}
	}

	return tempBuffer;
};

function convertBMP(imgData, quality) {
  if (typeof quality === 'undefined') quality = 100;
 	var encoder = new BmpEncoder(imgData);
	var data = encoder.encode();
  return {
    data: data,
    width: imgData.width,
    height: imgData.height
  };
};


function loadImage(filename, pic, ext) {
  let filenameStr = addressToString(filename)
  let buf = Z_Malloc(8) // pointer to pointer
  let length
  EMGL.previousImage = null
  EMGL.previousName = ''
  HEAPU32[buf >> 2] = 0
  // TODO: merge with virtual filesystem...
  //   But doing it this way, it's possible for images to load with the page
  //   If I switch back to FS.virtual mode, this part will always reload async
  // TODO: save time loading on map-review page aka LiveView
  /*
  let preloadedImage = document.querySelector(`IMG[title="${filenameStr}"]`)
  if (preloadedImage) {
    EMGL.previousName = filenameStr
    EMGL.previousImage = preloadedImage
    HEAPU32[pic >> 2] = 1
    return
  }
  */

  let palette = R_FindPalette(filename) 
    || R_FindPalette(stringToAddress(filenameStr.replace(/\..+?$/gi, '.tga')))

  if ((length = FS_ReadFile(filename, buf)) > 0 && HEAPU32[buf >> 2] > 0
    || palette) {
    let thisImage = document.createElement('IMG')
    EMGL.previousName = filenameStr
    EMGL.previousImage = thisImage
    thisImage.onload = function (evt) {
      debugger
      HEAP32[(evt.target.address - 4 * 4) >> 2] = evt.target.width
      HEAP32[(evt.target.address - 3 * 4) >> 2] = evt.target.height
      R_FinishImage3(evt.target.address - 7 * 4, 0x1908 /* GL_RGBA */, 0)
    }
    let imageView
    if(HEAPU32[buf >> 2]) {
      imageView = Array.from(HEAPU8.slice(HEAPU32[buf >> 2], 
          HEAPU32[buf >> 2] + length))
    } else
    if(palette) {
      imageView = Array.from(Uint8Array.from(convertBMP({
        data: HEAPU8.slice(palette, palette + 16*16*4),
        height: 16,
        width: 16,
      }).data))
      ext = 'bmp'
      // TODO: init XHR alt-name requests
    }
    let utfEncoded = imageView.map(function (c) { 
        return String.fromCharCode(c) }).join('')
    thisImage.src = 'data:image/' + ext + ';base64,' + btoa(utfEncoded)
    thisImage.name = filenameStr
    if(palette) {
      HEAPU32[pic >> 2] = palette // TO BE COPIED OUT
    } else {
      HEAPU32[pic >> 2] = 1
    }
    if(HEAPU32[buf >> 2]) {
      FS_FreeFile(HEAPU32[buf >> 2])
      Z_Free(buf)
    }
    document.body.appendChild(thisImage)
    // continue to palette
  } // else 

  if(HEAPU32[pic >> 2] != 0) {
    return
  }

  // TODO: Promise.any(altImages) based on palette.shader list
  EMGL.previousName = ''
  EMGL.previousImage = null
  HEAPU32[pic >> 2] = null
}


function R_LoadPNG(filename, pic) {
  return loadImage(filename, pic, 'png')
}

function R_LoadJPG(filename, pic) {
  return loadImage(filename, pic, 'jpg')
}


function _glGenTextures(n, textures) {
  __glGenObject(n, textures, "createTexture", GL.textures);
  // engine is giving us address of where it will store texture name, also
  let texture = HEAP32[textures >> 2]
  EMGL.texFiles[texture] = [textures, null]
  let newName = addressToString(HEAPU32[(EMGL.texFiles[texture][0] - 7*4) >> 2])
  if(newName.length == 0) {
    // using CreateImage2() not 3
    return;
  }
  if(EMGL.previousImage 
    && EMGL.previousName.replace(/\..*?$/, '') == newName.replace(/\..*?$/, '')) {
    EMGL.previousImage.address = EMGL.texFiles[texture][0]
    EMGL.texFiles[texture][1] = EMGL.previousImage
    // copy width, height to C struct
    if(EMGL.previousImage.width > 0 && EMGL.previousImage.height > 0) {
      HEAP32[(textures - 4 * 4) >> 2] = EMGL.previousImage.width
      HEAP32[(textures - 3 * 4) >> 2] = EMGL.previousImage.height
    }
  }
  EMGL.previousImage = null
  EMGL.previousName = ''
}

function _glGenVertexArrays(n, arrays) {
  __glGenObject(n, arrays, "createVertexArray", GL.vaos);
}

function _glGenerateMipmap(x0) {
  GLctx["generateMipmap"](x0);
}

function __glGetActiveAttribOrUniform(funcName, program, index, bufSize, length, size, type, name) {
  program = GL.programs[program];
  var info = GLctx[funcName](program, index);
  if (info) {
    var numBytesWrittenExclNull = name && stringToAddress(info.name, name, bufSize);
    if (length) SAFE_HEAP_STORE(length | 0, numBytesWrittenExclNull | 0, 4);
    if (size) SAFE_HEAP_STORE(size | 0, info.size | 0, 4);
    if (type) SAFE_HEAP_STORE(type | 0, info.type | 0, 4);
  }
}

function _glGetActiveUniform(program, index, bufSize, length, size, type, name) {
  __glGetActiveAttribOrUniform("getActiveUniform", program, index, bufSize, length, size, type, name);
}

function readI53FromI64(ptr) {
  return SAFE_HEAP_LOAD((ptr >> 2) * 4, 4, 1) + SAFE_HEAP_LOAD((ptr + 4 >> 2) * 4, 4, 0) * 4294967296;
}

function readI53FromU64(ptr) {
  return SAFE_HEAP_LOAD((ptr >> 2) * 4, 4, 1) + SAFE_HEAP_LOAD((ptr + 4 >> 2) * 4, 4, 1) * 4294967296;
}

function writeI53ToI64(ptr, num) {
  SAFE_HEAP_STORE((ptr >> 2) * 4, num, 4);
  SAFE_HEAP_STORE((ptr + 4 >> 2) * 4, (num - SAFE_HEAP_LOAD((ptr >> 2) * 4, 4, 1)) / 4294967296, 4);
  var deserialized = num >= 0 ? readI53FromU64(ptr) : readI53FromI64(ptr);
  if (deserialized != num) warnOnce("writeI53ToI64() out of range: serialized JS Number " + num + " to Wasm heap as bytes lo=0x" + SAFE_HEAP_LOAD((ptr >> 2) * 4, 4, 1).toString(16) + ", hi=0x" + SAFE_HEAP_LOAD((ptr + 4 >> 2) * 4, 4, 1).toString(16) + ", which deserializes back to " + deserialized + " instead!");
}

function emscriptenWebGLGet(name_, p, type) {
  if (!p) {
    GL.recordError(1281);
    return;
  }
  var ret = undefined;
  switch (name_) {
    case 36346:
      ret = 1;
      break;

    case 36344:
      if (type != 0 && type != 1) {
        GL.recordError(1280);
      }
      return;

    case 34814:
    case 36345:
      ret = 0;
      break;

    case 34466:
      var formats = GLctx.getParameter(34467);
      ret = formats ? formats.length : 0;
      break;

    case 33309:
      if (GL.currentContext.version < 2) {
        GL.recordError(1282);
        return;
      }
      var exts = GLctx.getSupportedExtensions() || [];
      ret = 2 * exts.length;
      break;

    case 33307:
    case 33308:
      if (GL.currentContext.version < 2) {
        GL.recordError(1280);
        return;
      }
      ret = name_ == 33307 ? 3 : 0;
      break;
  }
  if (ret === undefined) {
    var result = GLctx.getParameter(name_);
    switch (typeof result) {
      case "number":
        ret = result;
        break;

      case "boolean":
        ret = result ? 1 : 0;
        break;

      case "string":
        GL.recordError(1280);
        return;

      case "object":
        if (result === null) {
          switch (name_) {
            case 34964:
            case 35725:
            case 34965:
            case 36006:
            case 36007:
            case 32873:
            case 34229:
            case 36662:
            case 36663:
            case 35053:
            case 35055:
            case 36010:
            case 35097:
            case 35869:
            case 32874:
            case 36389:
            case 35983:
            case 35368:
            case 34068:
              {
                ret = 0;
                break;
              }

            default:
              {
                GL.recordError(1280);
                return;
              }
          }
        } else if (result instanceof Float32Array || result instanceof Uint32Array || result instanceof Int32Array || result instanceof Array) {
          for (var i = 0; i < result.length; ++i) {
            switch (type) {
              case 0:
                SAFE_HEAP_STORE(p + i * 4 | 0, result[i] | 0, 4);
                break;

              case 2:
                SAFE_HEAP_STORE_D(p + i * 4 | 0, Math.fround(result[i]), 4);
                break;

              case 4:
                SAFE_HEAP_STORE(p + i | 0, (result[i] ? 1 : 0) | 0, 1);
                break;
            }
          }
          return;
        } else {
          try {
            ret = result.name | 0;
          } catch (e) {
            GL.recordError(1280);
            err("GL_INVALID_ENUM in glGet" + type + "v: Unknown object returned from WebGL getParameter(" + name_ + ")! (error: " + e + ")");
            return;
          }
        }
        break;

      default:
        GL.recordError(1280);
        err("GL_INVALID_ENUM in glGet" + type + "v: Native code calling glGet" + type + "v(" + name_ + ") and it returns " + result + " of type " + typeof result + "!");
        return;
    }
  }
  switch (type) {
    case 1:
      writeI53ToI64(p, ret);
      break;

    case 0:
      SAFE_HEAP_STORE(p | 0, ret | 0, 4);
      break;

    case 2:
      SAFE_HEAP_STORE_D(p | 0, Math.fround(ret), 4);
      break;

    case 4:
      SAFE_HEAP_STORE(p | 0, (ret ? 1 : 0) | 0, 1);
      break;
  }
}

function _glGetBooleanv(name_, p) {
  emscriptenWebGLGet(name_, p, 4);
}

function _glGetError() {
  var error = GLctx.getError() || GL.lastError;
  GL.lastError = 0;
  return error;
}

function _glGetIntegerv(name_, p) {
  emscriptenWebGLGet(name_, p, 0);
}

function _glGetProgramInfoLog(program, maxLength, length, infoLog) {
  var log = GLctx.getProgramInfoLog(GL.programs[program]);
  if (log === null) log = "(unknown error)";
  var numBytesWrittenExclNull = maxLength > 0 && infoLog ? stringToAddress(log, infoLog, maxLength) : 0;
  if (length) SAFE_HEAP_STORE(length | 0, numBytesWrittenExclNull | 0, 4);
}

function _glGetProgramiv(program, pname, p) {
  if (!p) {
    GL.recordError(1281);
    return;
  }
  if (program >= GL.counter) {
    GL.recordError(1281);
    return;
  }
  program = GL.programs[program];
  if (pname == 35716) {
    var log = GLctx.getProgramInfoLog(program);
    if (log === null) log = "(unknown error)";
    SAFE_HEAP_STORE(p | 0, log.length + 1 | 0, 4);
  } else if (pname == 35719) {
    if (!program.maxUniformLength) {
      for (var i = 0; i < GLctx.getProgramParameter(program, 35718); ++i) {
        program.maxUniformLength = Math.max(program.maxUniformLength, GLctx.getActiveUniform(program, i).name.length + 1);
      }
    }
    SAFE_HEAP_STORE(p | 0, program.maxUniformLength | 0, 4);
  } else if (pname == 35722) {
    if (!program.maxAttributeLength) {
      for (var i = 0; i < GLctx.getProgramParameter(program, 35721); ++i) {
        program.maxAttributeLength = Math.max(program.maxAttributeLength, GLctx.getActiveAttrib(program, i).name.length + 1);
      }
    }
    SAFE_HEAP_STORE(p | 0, program.maxAttributeLength | 0, 4);
  } else if (pname == 35381) {
    if (!program.maxUniformBlockNameLength) {
      for (var i = 0; i < GLctx.getProgramParameter(program, 35382); ++i) {
        program.maxUniformBlockNameLength = Math.max(program.maxUniformBlockNameLength, GLctx.getActiveUniformBlockName(program, i).length + 1);
      }
    }
    SAFE_HEAP_STORE(p | 0, program.maxUniformBlockNameLength | 0, 4);
  } else {
    SAFE_HEAP_STORE(p | 0, GLctx.getProgramParameter(program, pname) | 0, 4);
  }
}

function _glGetShaderInfoLog(shader, maxLength, length, infoLog) {
  var log = GLctx.getShaderInfoLog(GL.shaders[shader]);
  if (log === null) log = "(unknown error)";
  var numBytesWrittenExclNull = maxLength > 0 && infoLog ? stringToAddress(log, infoLog, maxLength) : 0;
  if (length) SAFE_HEAP_STORE(length | 0, numBytesWrittenExclNull | 0, 4);
}

function _glGetShaderSource(shader, bufSize, length, source) {
  var result = GLctx.getShaderSource(GL.shaders[shader]);
  if (!result) return;
  var numBytesWrittenExclNull = bufSize > 0 && source ? stringToAddress(result, source, bufSize) : 0;
  if (length) SAFE_HEAP_STORE(length | 0, numBytesWrittenExclNull | 0, 4);
}

function _glGetShaderiv(shader, pname, p) {
  if (!p) {
    GL.recordError(1281);
    return;
  }
  if (pname == 35716) {
    var log = GLctx.getShaderInfoLog(GL.shaders[shader]);
    if (log === null) log = "(unknown error)";
    var logLength = log ? log.length + 1 : 0;
    SAFE_HEAP_STORE(p | 0, logLength | 0, 4);
  } else if (pname == 35720) {
    var source = GLctx.getShaderSource(GL.shaders[shader]);
    var sourceLength = source ? source.length + 1 : 0;
    SAFE_HEAP_STORE(p | 0, sourceLength | 0, 4);
  } else {
    SAFE_HEAP_STORE(p | 0, GLctx.getShaderParameter(GL.shaders[shader], pname) | 0, 4);
  }
}

function _glGetString(name_) {
  var ret = GL.stringCache[name_];
  if (!ret) {
    switch (name_) {
      case 7939:
        var exts = GLctx.getSupportedExtensions() || [];
        exts = exts.concat(exts.map(function (e) {
          return "GL_" + e;
        }));
        ret = stringToAddress(exts.join(" "));
        break;

      case 7936:
      case 7937:
      case 37445:
      case 37446:
        var s = GLctx.getParameter(name_);
        if (!s) {
          GL.recordError(1280);
        }
        ret = s && stringToAddress(s);
        break;

      case 7938:
        var glVersion = GLctx.getParameter(7938);
        if (true) glVersion = "OpenGL ES 3.0 (" + glVersion + ")"; else {
          glVersion = "OpenGL ES 2.0 (" + glVersion + ")";
        }
        ret = stringToAddress(glVersion);
        break;

      case 35724:
        var glslVersion = GLctx.getParameter(35724);
        var ver_re = /^WebGL GLSL ES ([0-9]\.[0-9][0-9]?)(?:$| .*)/;
        var ver_num = glslVersion.match(ver_re);
        if (ver_num !== null) {
          if (ver_num[1].length == 3) ver_num[1] = ver_num[1] + "0";
          glslVersion = "OpenGL ES GLSL ES " + ver_num[1] + " (" + glslVersion + ")";
        }
        ret = stringToAddress(glslVersion);
        break;

      default:
        GL.recordError(1280);
    }
    GL.stringCache[name_] = ret;
  }
  return ret;
}

function _glGetStringi(name, index) {
  if (GL.currentContext.version < 2) {
    GL.recordError(1282);
    return 0;
  }
  var stringiCache = GL.stringiCache[name];
  if (stringiCache) {
    if (index < 0 || index >= stringiCache.length) {
      GL.recordError(1281);
      return 0;
    }
    return stringiCache[index];
  }
  switch (name) {
    case 7939:
      var exts = GLctx.getSupportedExtensions() || [];
      exts = exts.concat(exts.map(function (e) {
        return "GL_" + e;
      }));
      exts = exts.map(function (e) {
        return stringToAddress(e);
      });
      stringiCache = GL.stringiCache[name] = exts;
      if (index < 0 || index >= stringiCache.length) {
        GL.recordError(1281);
        return 0;
      }
      return stringiCache[index];

    default:
      GL.recordError(1280);
      return 0;
  }
}

function jstoi_q(str) {
  return parseInt(str);
}

function webglGetLeftBracePos(name) {
  return name.slice(-1) == "]" && name.lastIndexOf("[");
}

function webglPrepareUniformLocationsBeforeFirstUse(program) {
  var uniformLocsById = program.uniformLocsById, uniformSizeAndIdsByName = program.uniformSizeAndIdsByName, i, j;
  if (!uniformLocsById) {
    program.uniformLocsById = uniformLocsById = {};
    program.uniformArrayNamesById = {};
    for (i = 0; i < GLctx.getProgramParameter(program, 35718); ++i) {
      var u = GLctx.getActiveUniform(program, i);
      var nm = u.name;
      var sz = u.size;
      var lb = webglGetLeftBracePos(nm);
      var arrayName = lb > 0 ? nm.slice(0, lb) : nm;
      var id = program.uniformIdCounter;
      program.uniformIdCounter += sz;
      uniformSizeAndIdsByName[arrayName] = [sz, id];
      for (j = 0; j < sz; ++j) {
        uniformLocsById[id] = j;
        program.uniformArrayNamesById[id++] = arrayName;
      }
    }
  }
}

function _glGetUniformLocation(program, name) {
  name = addressToString(name);
  if (program = GL.programs[program]) {
    webglPrepareUniformLocationsBeforeFirstUse(program);
    var uniformLocsById = program.uniformLocsById;
    var arrayIndex = 0;
    var uniformBaseName = name;
    var leftBrace = webglGetLeftBracePos(name);
    if (leftBrace > 0) {
      arrayIndex = jstoi_q(name.slice(leftBrace + 1)) >>> 0;
      uniformBaseName = name.slice(0, leftBrace);
    }
    var sizeAndId = program.uniformSizeAndIdsByName[uniformBaseName];
    if (sizeAndId && arrayIndex < sizeAndId[0]) {
      arrayIndex += sizeAndId[1];
      if (uniformLocsById[arrayIndex] = uniformLocsById[arrayIndex] || GLctx.getUniformLocation(program, name)) {
        return arrayIndex;
      }
    }
  } else {
    GL.recordError(1281);
  }
  return -1;
}

function _glLineWidth(x0) {
  GLctx["lineWidth"](x0);
}

function _glLinkProgram(program) {
  program = GL.programs[program];
  GLctx.linkProgram(program);
  program.uniformLocsById = 0;
  program.uniformSizeAndIdsByName = {};
}

function _glLoadIdentity() {
  throw "Legacy GL function (glLoadIdentity) called. If you want legacy GL emulation, you need to compile with -s LEGACY_GL_EMULATION=1 to enable legacy GL emulation.";
}

function _glLoadMatrixf() {
  debugger;
}

function _glMapBufferRange() {
  debugger;
}

function _glMatrixMode() {
  throw "Legacy GL function (glMatrixMode) called. If you want legacy GL emulation, you need to compile with -s LEGACY_GL_EMULATION=1 to enable legacy GL emulation.";
}

function _glOrtho() {
  debugger;
}

function _glPolygonMode() { }

function _glPolygonOffset(x0, x1) {
  GLctx["polygonOffset"](x0, x1);
}

function _glPopMatrix() {
  debugger;
}

function _glPushMatrix() {
  debugger;
}

function _glReadBuffer(x0) {
  GLctx["readBuffer"](x0);
}

function computeUnpackAlignedImageSize(width, height, sizePerPixel, alignment) {
  function roundedToNextMultipleOf(x, y) {
    return x + y - 1 & -y;
  }
  var plainRowSize = width * sizePerPixel;
  var alignedRowSize = roundedToNextMultipleOf(plainRowSize, alignment);
  return height * alignedRowSize;
}

function __colorChannelsInGlTextureFormat(format) {
  var colorChannels = {
    5: 3,
    6: 4,
    8: 2,
    29502: 3,
    29504: 4,
    26917: 2,
    26918: 2,
    29846: 3,
    29847: 4
  };
  return colorChannels[format - 6402] || 1;
}

function heapObjectForWebGLType(type) {
  type -= 5120;
  if (type == 0) return new Int8Array(ENV.memory.buffer);
  if (type == 1) return new Uint8Array(ENV.memory.buffer);
  if (type == 2) return new Int16Array(ENV.memory.buffer);
  if (type == 4) return new Int32Array(ENV.memory.buffer);
  if (type == 6) return new Float32Array(ENV.memory.buffer);
  if (type == 5 || type == 28922 || type == 28520 || type == 30779 || type == 30782) return HEAPU32;
  return HEAPU16;
}

function heapAccessShiftForWebGLHeap(heap) {
  return 31 - Math.clz32(heap.BYTES_PER_ELEMENT);
}

function emscriptenWebGLGetTexPixelData(type, format, width, height, pixels, internalFormat) {
  var heap = heapObjectForWebGLType(type);
  var shift = heapAccessShiftForWebGLHeap(heap);
  var byteSize = 1 << shift;
  var sizePerPixel = __colorChannelsInGlTextureFormat(format) * byteSize;
  var bytes = computeUnpackAlignedImageSize(width, height, sizePerPixel, GL.unpackAlignment);
  return heap.subarray(pixels >> shift, pixels + bytes >> shift);
}

function _glReadPixels(x, y, width, height, format, type, pixels) {
  if (true) {
    if (GLctx.currentPixelPackBufferBinding) {
      GLctx.readPixels(x, y, width, height, format, type, pixels);
    } else {
      var heap = heapObjectForWebGLType(type);
      GLctx.readPixels(x, y, width, height, format, type, heap, pixels >> heapAccessShiftForWebGLHeap(heap));
    }
    return;
  }
  var pixelData = emscriptenWebGLGetTexPixelData(type, format, width, height, pixels, format);
  if (!pixelData) {
    GL.recordError(1280);
    return;
  }
  GLctx.readPixels(x, y, width, height, format, type, pixelData);
}

function _glRenderbufferStorage(x0, x1, x2, x3) {
  GLctx["renderbufferStorage"](x0, x1, x2, x3);
}

function _glRenderbufferStorageMultisample(x0, x1, x2, x3, x4) {
  GLctx["renderbufferStorageMultisample"](x0, x1, x2, x3, x4);
}

function _glScissor(x0, x1, x2, x3) {
  GLctx["scissor"](x0, x1, x2, x3);
}

function _glShadeModel() {
  debugger;
}

function _glShaderSource(shader, count, string, length) {
  var source = GL.getSource(shader, count, string, length);
  GLctx.shaderSource(GL.shaders[shader], source);
}

function _glStencilFunc(x0, x1, x2) {
  GLctx["stencilFunc"](x0, x1, x2);
}

function _glStencilMask(x0) {
  GLctx["stencilMask"](x0);
}

function _glStencilOp(x0, x1, x2) {
  GLctx["stencilOp"](x0, x1, x2);
}

function _glTexCoord2f() {
  debugger;
}

function _glTexCoord2fv() {
  debugger;
}

function _glTexCoordPointer() {
  debugger;
}

function _glTexEnvf() {
  debugger;
}

function _glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels) {
  /*
  //if(width <= 2 || height <= 2) return
  if(EMGL.previousImage && EMGL.previousImage.complete) {
    width = EMGL.previousImage.width
    height = EMGL.previousImage.height
    GLctx.texImage2D(target, level, internalFormat, width, height, border, format, type, pixels ? EMGL.previousImage : null);
    return
  } else if (typeof EMGL.texFiles[EMGL.previousTex] != 'undefined'
    && EMGL.texFiles[EMGL.previousTex][1]) {
    console.log('whoops')
    return
  }
  */

  if (true) {
    if (GLctx.currentPixelUnpackBufferBinding) {
      GLctx.texImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
    } else if (pixels) {
      var heap = heapObjectForWebGLType(type);
      GLctx.texImage2D(target, level, internalFormat, width, height, border, format, type, heap, pixels >> heapAccessShiftForWebGLHeap(heap));
    } else {
      GLctx.texImage2D(target, level, internalFormat, width, height, border, format, type, null);
    }
    return;
  }
  GLctx.texImage2D(target, level, internalFormat, width, height, border, format, type, pixels ? emscriptenWebGLGetTexPixelData(type, format, width, height, pixels, internalFormat) : null);
}

function _glTexParameterf(x0, x1, x2) {
  GLctx["texParameterf"](x0, x1, x2);
}

function _glTexParameteri(x0, x1, x2) {
  GLctx["texParameteri"](x0, x1, x2);
}

function _glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels) {
  if (EMGL.previousImage) {
    if (width <= 2 || height <= 2) return
    //width = EMGL.previousImage.width
    //height = EMGL.previousImage.height
    GLctx.texSubImage2D(target, level, xoffset, yoffset, width, height, format, type, EMGL.previousImage);
    return
  } else if (typeof EMGL.texFiles[EMGL.previousTex] != 'undefined'
    && EMGL.texFiles[EMGL.previousTex][1]) {
    //debugger
    return
  }

  if (true) {
    if (GLctx.currentPixelUnpackBufferBinding) {
      GLctx.texSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    } else if (pixels) {
      var heap = heapObjectForWebGLType(type);
      GLctx.texSubImage2D(target, level, xoffset, yoffset, width, height, format, type, heap, pixels >> heapAccessShiftForWebGLHeap(heap));
    } else {
      GLctx.texSubImage2D(target, level, xoffset, yoffset, width, height, format, type, null);
    }
    return;
  }

  var pixelData = null;
  if (pixels) pixelData = emscriptenWebGLGetTexPixelData(type, format, width, height, pixels, 0);
  GLctx.texSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixelData);
}

function _glTranslatef() {
  debugger;
}

function webglGetUniformLocation(location) {
  var p = GLctx.currentProgram;
  if (p) {
    var webglLoc = p.uniformLocsById[location];
    if (typeof webglLoc === "number") {
      p.uniformLocsById[location] = webglLoc = GLctx.getUniformLocation(p, p.uniformArrayNamesById[location] + (webglLoc > 0 ? "[" + webglLoc + "]" : ""));
    }
    return webglLoc;
  } else {
    GL.recordError(1282);
  }
}

function _glUniform1f(location, v0) {
  GLctx.uniform1f(webglGetUniformLocation(location), v0);
}

function _glUniform1fv(location, count, value) {
  GLctx.uniform1fv(webglGetUniformLocation(location), HEAPF32, value >> 2, count);
}

function _glUniform1i(location, v0) {
  GLctx.uniform1i(webglGetUniformLocation(location), v0);
}

function _glUniform2f(location, v0, v1) {
  GLctx.uniform2f(webglGetUniformLocation(location), v0, v1);
}

function _glUniform3f(location, v0, v1, v2) {
  GLctx.uniform3f(webglGetUniformLocation(location), v0, v1, v2);
}

function _glUniform4f(location, v0, v1, v2, v3) {
  GLctx.uniform4f(webglGetUniformLocation(location), v0, v1, v2, v3);
}

function _glUniformMatrix4fv(location, count, transpose, value) {
  GLctx.uniformMatrix4fv(webglGetUniformLocation(location), !!transpose, HEAPF32, value >> 2, count * 16);
}

function _glUnmapBuffer() {
  debugger;
}

function _glUseProgram(program) {
  program = GL.programs[program];
  GLctx.useProgram(program);
  GLctx.currentProgram = program;
}

function _glValidateProgram(program) {
  GLctx.validateProgram(GL.programs[program]);
}

function _glVertex2f() {
  debugger;
}

function _glVertex3f() {
  debugger;
}

function _glVertex3fv() {
  debugger;
}

function _glVertexAttribPointer(index, size, type, normalized, stride, ptr) {
  var cb = GL.currentContext.clientBuffers[index];
  if (!GLctx.currentArrayBufferBinding) {
    cb.size = size;
    cb.type = type;
    cb.normalized = normalized;
    cb.stride = stride;
    cb.ptr = ptr;
    cb.clientside = true;
    cb.vertexAttribPointerAdaptor = function (index, size, type, normalized, stride, ptr) {
      this.vertexAttribPointer(index, size, type, normalized, stride, ptr);
    };
    return;
  }
  cb.clientside = false;
  GLctx.vertexAttribPointer(index, size, type, !!normalized, stride, ptr);
}

function _glVertexPointer() {
  throw "Legacy GL function (glVertexPointer) called. If you want legacy GL emulation, you need to compile with -s LEGACY_GL_EMULATION=1 to enable legacy GL emulation.";
}

function _glViewport(x0, x1, x2, x3) {
  GLctx["viewport"](x0, x1, x2, x3);
}

function _setTempRet0(val) {
  setTempRet0(val);
}

function GL_GetDrawableSize(width, height) {
  // THIS IS THE NEW VID_RESTART FAST HACK
  INPUT.updateWidth = width
  INPUT.updateHeight = height
  HEAP32[width>>2] = GL.canvas.width
  HEAP32[height>>2] = GL.canvas.height
}

var GLctx;

var EMGL = {
  previousName: '',
  previousImage: null,
  previousTex: 0,
  texFiles: {},
  GL_GetDrawableSize: GL_GetDrawableSize,
  GL_GetProcAddress: function () { },
  R_LoadPNG: R_LoadPNG,
  R_LoadJPG: R_LoadJPG,
  "getTempRet0": _getTempRet0,
  "glActiveTexture": _glActiveTexture,
  "glAlphaFunc": _glAlphaFunc,
  "glArrayElement": _glArrayElement,
  "glAttachShader": _glAttachShader,
  "glBegin": _glBegin,
  "glBindAttribLocation": _glBindAttribLocation,
  "glBindBuffer": _glBindBuffer,
  "glBindFramebuffer": _glBindFramebuffer,
  "glBindRenderbuffer": _glBindRenderbuffer,
  "glBindTexture": _glBindTexture,
  "glBindVertexArray": _glBindVertexArray,
  "glBlendFunc": _glBlendFunc,
  "glBlitFramebuffer": _glBlitFramebuffer,
  "glBufferData": _glBufferData,
  "glBufferSubData": _glBufferSubData,
  "glCheckFramebufferStatus": _glCheckFramebufferStatus,
  "glClear": _glClear,
  "glClearColor": _glClearColor,
  "glClearDepth": _glClearDepth,
  "glClearStencil": _glClearStencil,
  "glClipPlane": _glClipPlane,
  "glColor3f": _glColor3f,
  "glColor4f": _glColor4f,
  "glColor4ubv": _glColor4ubv,
  "glColorMask": _glColorMask,
  "glColorPointer": _glColorPointer,
  "glCompileShader": _glCompileShader,
  "glCompressedTexImage2D": _glCompressedTexImage2D,
  "glCompressedTexSubImage2D": _glCompressedTexSubImage2D,
  "glCopyTexSubImage2D": _glCopyTexSubImage2D,
  "glCreateProgram": _glCreateProgram,
  "glCreateShader": _glCreateShader,
  "glCullFace": _glCullFace,
  "glDeleteBuffers": _glDeleteBuffers,
  "glDeleteFramebuffers": _glDeleteFramebuffers,
  "glDeleteProgram": _glDeleteProgram,
  "glDeleteRenderbuffers": _glDeleteRenderbuffers,
  "glDeleteShader": _glDeleteShader,
  "glDeleteTextures": _glDeleteTextures,
  "glDeleteVertexArrays": _glDeleteVertexArrays,
  "glDepthFunc": _glDepthFunc,
  "glDepthMask": _glDepthMask,
  "glDepthRange": _glDepthRange,
  "glDetachShader": _glDetachShader,
  "glDisable": _glDisable,
  "glDisableClientState": _glDisableClientState,
  "glDisableVertexAttribArray": _glDisableVertexAttribArray,
  "glDrawArrays": _glDrawArrays,
  "glDrawBuffer": _glDrawBuffer,
  "glDrawBuffers": _glDrawBuffers,
  "glDrawElements": _glDrawElements,
  "glEnable": _glEnable,
  "glEnableClientState": _glEnableClientState,
  "glEnableVertexAttribArray": _glEnableVertexAttribArray,
  "glEnd": _glEnd,
  "glFinish": _glFinish,
  "glFlush": _glFlush,
  "glFramebufferRenderbuffer": _glFramebufferRenderbuffer,
  "glFramebufferTexture2D": _glFramebufferTexture2D,
  "glFrustum": _glFrustum,
  "glGenBuffers": _glGenBuffers,
  "glGenFramebuffers": _glGenFramebuffers,
  "glGenRenderbuffers": _glGenRenderbuffers,
  "glGenTextures": _glGenTextures,
  "glGenVertexArrays": _glGenVertexArrays,
  "glGenerateMipmap": _glGenerateMipmap,
  "glGetActiveUniform": _glGetActiveUniform,
  "glGetBooleanv": _glGetBooleanv,
  "glGetError": _glGetError,
  "glGetIntegerv": _glGetIntegerv,
  "glGetProgramInfoLog": _glGetProgramInfoLog,
  "glGetProgramiv": _glGetProgramiv,
  "glGetShaderInfoLog": _glGetShaderInfoLog,
  "glGetShaderSource": _glGetShaderSource,
  "glGetShaderiv": _glGetShaderiv,
  "glGetString": _glGetString,
  "glGetStringi": _glGetStringi,
  "glGetUniformLocation": _glGetUniformLocation,
  "glLineWidth": _glLineWidth,
  "glLinkProgram": _glLinkProgram,
  "glLoadIdentity": _glLoadIdentity,
  "glLoadMatrixf": _glLoadMatrixf,
  "glMapBufferRange": _glMapBufferRange,
  "glMatrixMode": _glMatrixMode,
  "glOrtho": _glOrtho,
  "glPolygonMode": _glPolygonMode,
  "glPolygonOffset": _glPolygonOffset,
  "glPopMatrix": _glPopMatrix,
  "glPushMatrix": _glPushMatrix,
  "glReadBuffer": _glReadBuffer,
  "glReadPixels": _glReadPixels,
  "glRenderbufferStorage": _glRenderbufferStorage,
  "glRenderbufferStorageMultisample": _glRenderbufferStorageMultisample,
  "glScissor": _glScissor,
  "glShadeModel": _glShadeModel,
  "glShaderSource": _glShaderSource,
  "glStencilFunc": _glStencilFunc,
  "glStencilMask": _glStencilMask,
  "glStencilOp": _glStencilOp,
  "glTexCoord2f": _glTexCoord2f,
  "glTexCoord2fv": _glTexCoord2fv,
  "glTexCoordPointer": _glTexCoordPointer,
  "glTexEnvf": _glTexEnvf,
  "glTexImage2D": _glTexImage2D,
  "glTexParameterf": _glTexParameterf,
  "glTexParameteri": _glTexParameteri,
  "glTexSubImage2D": _glTexSubImage2D,
  "glTranslatef": _glTranslatef,
  "glUniform1f": _glUniform1f,
  "glUniform1fv": _glUniform1fv,
  "glUniform1i": _glUniform1i,
  "glUniform2f": _glUniform2f,
  "glUniform3f": _glUniform3f,
  "glUniform4f": _glUniform4f,
  "glUniformMatrix4fv": _glUniformMatrix4fv,
  "glUnmapBuffer": _glUnmapBuffer,
  "glUseProgram": _glUseProgram,
  "glValidateProgram": _glValidateProgram,
  "glVertex2f": _glVertex2f,
  "glVertex3f": _glVertex3f,
  "glVertex3fv": _glVertex3fv,
  "glVertexAttribPointer": _glVertexAttribPointer,
  "glVertexPointer": _glVertexPointer,
  "glViewport": _glViewport,
  glGenQueries: function () {},
  glDeleteQueries: function () {},
  glBeginQuery: function () {},
  glEndQuery: function () {},
  glGetQueryObjectiv: function () {},
  glGetQueryObjectuiv: function () {},
  glTextureParameterfEXT: function () {},
  glBindMultiTextureEXT: function () {},
  glTextureParameteriEXT: function () {},
  glTextureImage2DEXT: function () {},
  glTextureSubImage2DEXT: function () {},
  glCopyTextureSubImage2DEXT: function () {},
  glCompressedTextureImage2DEXT: function () {},
  glCompressedTextureSubImage2DEXT: function () {},
  glGenerateTextureMipmapEXT: function () {},
  glProgramUniform1iEXT: function () {},
  glProgramUniform1fEXT: function () {},
  glProgramUniform2fEXT: function () {},
  glProgramUniform3fEXT: function () {},
  glProgramUniform4fEXT: function () {},
  glProgramUniform1fvEXT: function () {},
  glProgramUniformMatrix4fvEXT: function () {},
  glNamedRenderbufferStorageEXT: function () {},
  glNamedRenderbufferStorageMultisampleEXT: function () {},
  glCheckNamedFramebufferStatusEXT: function () {},
  glNamedFramebufferRenderbufferEXT: function () {},
  glNamedFramebufferTexture2DEXT: function () {},

}

