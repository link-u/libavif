// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "gles_r8_writer.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/log.h>

#include <climits>
#include <cstring>
#include <mutex>

#define GLES_R8_LOG_TAG "avif_jni_gles"
#define GLES_R8_LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, GLES_R8_LOG_TAG, __VA_ARGS__))

namespace avif_jni {
namespace {

// Full-screen triangle generated from gl_VertexID; no vertex buffers or VAOs
// are needed (the default VAO 0 is valid in OpenGL ES 3.0).
const char kVertexShaderSource[] =
    "#version 300 es\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 pos = vec2(gl_VertexID == 1 ? 3.0 : -1.0,\n"
    "                  gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "  v_uv = pos * 0.5 + 0.5;\n"
    "  gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

// Copies the red channel of the source R8 texture into the R8 render target.
// u_range = (scale, offset) applied to the normalized Y sample:
//   full range:    (1, 0)
//   limited range: (255/219, -16/219)  ->  (Y - 16) / 219, clamped to [0, 1]
// which is the float equivalent of LimitedToFull8() in libavif_jni.cc.
const char kFragmentShaderSource[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec2 u_range;\n"
    "in vec2 v_uv;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "void main() {\n"
    "  float y = texture(u_texture, v_uv).r;\n"
    "  y = clamp(y * u_range.x + u_range.y, 0.0, 1.0);\n"
    "  o_color = vec4(y, 0.0, 0.0, 1.0);\n"
    "}\n";

bool HasExtension(const char* extensions, const char* name) {
  if (extensions == nullptr || name == nullptr) {
    return false;
  }
  const size_t name_length = strlen(name);
  const char* cursor = extensions;
  while ((cursor = strstr(cursor, name)) != nullptr) {
    const bool starts_at_token = (cursor == extensions) || (cursor[-1] == ' ');
    const char terminator = cursor[name_length];
    if (starts_at_token && (terminator == ' ' || terminator == '\0')) {
      return true;
    }
    cursor += name_length;
  }
  return false;
}

struct GlesR8Context {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLConfig config = nullptr;
  EGLContext context = EGL_NO_CONTEXT;
  // 1x1 pbuffer used only when EGL_KHR_surfaceless_context is missing.
  EGLSurface surface = EGL_NO_SURFACE;
  PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC get_native_client_buffer = nullptr;
  PFNEGLCREATEIMAGEKHRPROC create_image = nullptr;
  PFNEGLDESTROYIMAGEKHRPROC destroy_image = nullptr;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d = nullptr;
  GLuint program = 0;
  GLint texture_location = -1;
  GLint range_location = -1;
  bool available = false;
};

// Makes the shared offscreen context current for the lifetime of the object
// and restores whatever the calling thread had bound before.
class ScopedMakeCurrent {
 public:
  explicit ScopedMakeCurrent(const GlesR8Context& ctx) : ctx_(ctx) {
    previous_display_ = eglGetCurrentDisplay();
    previous_context_ = eglGetCurrentContext();
    previous_draw_surface_ = eglGetCurrentSurface(EGL_DRAW);
    previous_read_surface_ = eglGetCurrentSurface(EGL_READ);
    ok_ = eglMakeCurrent(ctx_.display, ctx_.surface, ctx_.surface,
                         ctx_.context) == EGL_TRUE;
    if (!ok_) {
      GLES_R8_LOGE("eglMakeCurrent failed: 0x%x.", eglGetError());
    }
  }

  ~ScopedMakeCurrent() {
    if (!ok_) {
      return;
    }
    if (previous_display_ != EGL_NO_DISPLAY &&
        previous_context_ != EGL_NO_CONTEXT) {
      eglMakeCurrent(previous_display_, previous_draw_surface_,
                     previous_read_surface_, previous_context_);
    } else {
      eglMakeCurrent(ctx_.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     EGL_NO_CONTEXT);
    }
  }

  ScopedMakeCurrent(const ScopedMakeCurrent&) = delete;
  ScopedMakeCurrent& operator=(const ScopedMakeCurrent&) = delete;

  bool ok() const { return ok_; }

 private:
  const GlesR8Context& ctx_;
  EGLDisplay previous_display_ = EGL_NO_DISPLAY;
  EGLContext previous_context_ = EGL_NO_CONTEXT;
  EGLSurface previous_draw_surface_ = EGL_NO_SURFACE;
  EGLSurface previous_read_surface_ = EGL_NO_SURFACE;
  bool ok_ = false;
};

GLuint CompileShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  if (shader == 0) {
    GLES_R8_LOGE("glCreateShader failed: 0x%x.", glGetError());
    return 0;
  }
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint status = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    char info_log[512] = {};
    glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
    GLES_R8_LOGE("Shader compile failed: %s", info_log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint LinkProgram(const char* vertex_source, const char* fragment_source) {
  const GLuint vertex_shader = CompileShader(GL_VERTEX_SHADER, vertex_source);
  if (vertex_shader == 0) {
    return 0;
  }
  const GLuint fragment_shader =
      CompileShader(GL_FRAGMENT_SHADER, fragment_source);
  if (fragment_shader == 0) {
    glDeleteShader(vertex_shader);
    return 0;
  }
  GLuint program = glCreateProgram();
  if (program != 0) {
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
      char info_log[512] = {};
      glGetProgramInfoLog(program, sizeof(info_log), nullptr, info_log);
      GLES_R8_LOGE("Program link failed: %s", info_log);
      glDeleteProgram(program);
      program = 0;
    }
  }
  // Shaders are reference counted by the program; safe to delete now.
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  return program;
}

void DestroyContext(GlesR8Context* ctx) {
  if (ctx->display != EGL_NO_DISPLAY) {
    if (ctx->program != 0) {
      ScopedMakeCurrent current(*ctx);
      if (current.ok()) {
        glDeleteProgram(ctx->program);
      }
      ctx->program = 0;
    }
    if (ctx->context != EGL_NO_CONTEXT) {
      eglDestroyContext(ctx->display, ctx->context);
      ctx->context = EGL_NO_CONTEXT;
    }
    if (ctx->surface != EGL_NO_SURFACE) {
      eglDestroySurface(ctx->display, ctx->surface);
      ctx->surface = EGL_NO_SURFACE;
    }
    // Intentionally no eglTerminate(): the default display is shared with the
    // rest of the process (HWUI, the app's own GL code, ...).
    ctx->display = EGL_NO_DISPLAY;
  }
  ctx->available = false;
}

// Creates the shared offscreen GLES 3.0 context. Returns false (with the
// struct partially filled; call DestroyContext) on any failure.
bool InitContext(GlesR8Context* ctx) {
  ctx->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (ctx->display == EGL_NO_DISPLAY) {
    GLES_R8_LOGE("eglGetDisplay failed: 0x%x.", eglGetError());
    return false;
  }
  EGLint major = 0;
  EGLint minor = 0;
  if (eglInitialize(ctx->display, &major, &minor) != EGL_TRUE) {
    GLES_R8_LOGE("eglInitialize failed: 0x%x.", eglGetError());
    ctx->display = EGL_NO_DISPLAY;
    return false;
  }

  const char* egl_extensions = eglQueryString(ctx->display, EGL_EXTENSIONS);
  if (!HasExtension(egl_extensions, "EGL_KHR_image_base") ||
      !HasExtension(egl_extensions, "EGL_ANDROID_image_native_buffer") ||
      !HasExtension(egl_extensions, "EGL_ANDROID_get_native_client_buffer")) {
    GLES_R8_LOGE("Required EGL extensions are missing: %s",
                 egl_extensions != nullptr ? egl_extensions : "(null)");
    return false;
  }

  ctx->get_native_client_buffer =
      reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
          eglGetProcAddress("eglGetNativeClientBufferANDROID"));
  ctx->create_image = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
      eglGetProcAddress("eglCreateImageKHR"));
  ctx->destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
      eglGetProcAddress("eglDestroyImageKHR"));
  if (ctx->get_native_client_buffer == nullptr || ctx->create_image == nullptr ||
      ctx->destroy_image == nullptr) {
    GLES_R8_LOGE("eglGetProcAddress failed for EGLImage entry points.");
    return false;
  }

  if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
    GLES_R8_LOGE("eglBindAPI failed: 0x%x.", eglGetError());
    return false;
  }

  const EGLint config_attribs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
      EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
      EGL_RED_SIZE,        8,
      EGL_GREEN_SIZE,      8,
      EGL_BLUE_SIZE,       8,
      EGL_ALPHA_SIZE,      8,
      EGL_NONE};
  EGLint num_configs = 0;
  if (eglChooseConfig(ctx->display, config_attribs, &ctx->config, 1,
                      &num_configs) != EGL_TRUE ||
      num_configs < 1) {
    GLES_R8_LOGE("eglChooseConfig found no GLES3 pbuffer config: 0x%x.",
                 eglGetError());
    return false;
  }

  const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  ctx->context = eglCreateContext(ctx->display, ctx->config, EGL_NO_CONTEXT,
                                  context_attribs);
  if (ctx->context == EGL_NO_CONTEXT) {
    GLES_R8_LOGE("eglCreateContext (GLES 3.0) failed: 0x%x.", eglGetError());
    return false;
  }

  if (!HasExtension(egl_extensions, "EGL_KHR_surfaceless_context")) {
    const EGLint pbuffer_attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    ctx->surface =
        eglCreatePbufferSurface(ctx->display, ctx->config, pbuffer_attribs);
    if (ctx->surface == EGL_NO_SURFACE) {
      GLES_R8_LOGE("eglCreatePbufferSurface failed: 0x%x.", eglGetError());
      return false;
    }
  }

  ScopedMakeCurrent current(*ctx);
  if (!current.ok()) {
    return false;
  }

  const char* gl_extensions =
      reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  if (!HasExtension(gl_extensions, "GL_OES_EGL_image")) {
    GLES_R8_LOGE("GL_OES_EGL_image is not supported.");
    return false;
  }
  ctx->image_target_texture_2d =
      reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
          eglGetProcAddress("glEGLImageTargetTexture2DOES"));
  if (ctx->image_target_texture_2d == nullptr) {
    GLES_R8_LOGE("eglGetProcAddress(glEGLImageTargetTexture2DOES) failed.");
    return false;
  }

  ctx->program = LinkProgram(kVertexShaderSource, kFragmentShaderSource);
  if (ctx->program == 0) {
    return false;
  }
  ctx->texture_location = glGetUniformLocation(ctx->program, "u_texture");
  ctx->range_location = glGetUniformLocation(ctx->program, "u_range");
  if (ctx->texture_location < 0 || ctx->range_location < 0) {
    GLES_R8_LOGE("Uniform lookup failed (texture=%d, range=%d).",
                 ctx->texture_location, ctx->range_location);
    return false;
  }

  ctx->available = true;
  return true;
}

std::mutex& ContextMutex() {
  static std::mutex mutex;
  return mutex;
}

// Must be called with ContextMutex() held. The context lives for the process
// lifetime; EGL objects are cheap to keep and expensive to recreate per image.
GlesR8Context* GetContextLocked() {
  static GlesR8Context ctx;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    if (!InitContext(&ctx)) {
      DestroyContext(&ctx);
      GLES_R8_LOGE("GLES R8 writer unavailable; R8 output will be skipped.");
    }
  }
  return ctx.available ? &ctx : nullptr;
}

void DrainGlErrors() {
  while (glGetError() != GL_NO_ERROR) {
  }
}

}  // namespace

bool GlesR8WriterIsAvailable() {
  std::lock_guard<std::mutex> lock(ContextMutex());
  return GetContextLocked() != nullptr;
}

bool WriteYPlaneToR8HardwareBuffer(AHardwareBuffer* hw_buffer,
                                   const uint8_t* src_y, size_t src_row_bytes,
                                   uint32_t width, uint32_t height,
                                   bool limited_range) {
  if (hw_buffer == nullptr || src_y == nullptr || width == 0 || height == 0) {
    return false;
  }
  // GL_UNPACK_ROW_LENGTH is a GLint pixel count; for R8 pixels == bytes.
  if (src_row_bytes < width || src_row_bytes > static_cast<size_t>(INT_MAX) ||
      width > static_cast<uint32_t>(INT_MAX) ||
      height > static_cast<uint32_t>(INT_MAX)) {
    GLES_R8_LOGE("Unsupported plane geometry (%ux%u, rowBytes=%zu).", width,
                 height, src_row_bytes);
    return false;
  }

  std::lock_guard<std::mutex> lock(ContextMutex());
  GlesR8Context* ctx = GetContextLocked();
  if (ctx == nullptr) {
    return false;
  }
  ScopedMakeCurrent current(*ctx);
  if (!current.ok()) {
    return false;
  }
  DrainGlErrors();

  EGLClientBuffer client_buffer = ctx->get_native_client_buffer(hw_buffer);
  if (client_buffer == nullptr) {
    GLES_R8_LOGE("eglGetNativeClientBufferANDROID failed: 0x%x.",
                 eglGetError());
    return false;
  }
  const EGLint image_attribs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
  EGLImageKHR image =
      ctx->create_image(ctx->display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                        client_buffer, image_attribs);
  if (image == EGL_NO_IMAGE_KHR) {
    GLES_R8_LOGE("eglCreateImageKHR failed for R8 AHardwareBuffer: 0x%x.",
                 eglGetError());
    return false;
  }

  GLuint textures[2] = {0, 0};
  glGenTextures(2, textures);
  const GLuint dst_texture = textures[0];
  const GLuint src_texture = textures[1];
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);

  bool ok = false;
  do {
    // Destination: the AHardwareBuffer itself, imported as a 2D texture and
    // attached as the color target. R8 is color-renderable in GLES 3.0.
    glBindTexture(GL_TEXTURE_2D, dst_texture);
    ctx->image_target_texture_2d(GL_TEXTURE_2D,
                                 static_cast<GLeglImageOES>(image));
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
      GLES_R8_LOGE("glEGLImageTargetTexture2DOES failed: 0x%x.", error);
      break;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           dst_texture, 0);
    const GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
      GLES_R8_LOGE("R8 framebuffer incomplete: 0x%x.", fb_status);
      break;
    }

    // Source: the decoded Y plane uploaded as a plain R8 texture. Rows may be
    // padded (yuvRowBytes >= width), hence GL_UNPACK_ROW_LENGTH.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(src_row_bytes));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height), 0, GL_RED, GL_UNSIGNED_BYTE,
                 src_y);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    error = glGetError();
    if (error != GL_NO_ERROR) {
      GLES_R8_LOGE("glTexImage2D(GL_R8 %ux%u) failed: 0x%x.", width, height,
                   error);
      break;
    }

    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DITHER);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(ctx->program);
    glUniform1i(ctx->texture_location, 0);
    if (limited_range) {
      glUniform2f(ctx->range_location, 255.0f / 219.0f, -16.0f / 219.0f);
    } else {
      glUniform2f(ctx->range_location, 1.0f, 0.0f);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // Bitmap.wrapHardwareBuffer has no fence parameter, so wait for the GPU
    // here instead of handing over a buffer that is still being written.
    glFinish();
    error = glGetError();
    if (error != GL_NO_ERROR) {
      GLES_R8_LOGE("R8 draw failed: 0x%x.", error);
      break;
    }
    ok = true;
  } while (false);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(2, textures);
  ctx->destroy_image(ctx->display, image);
  return ok;
}

}  // namespace avif_jni
