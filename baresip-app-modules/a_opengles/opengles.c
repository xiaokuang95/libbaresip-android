#include <stdint.h>
#include "re.h"
#include "rem.h"
#include "baresip.h"
#include <pthread.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <malloc.h>
#include "vidisp.h"


#define GET_STR(x) #x

// 顶点着色器glsl
static const char *vertexShader = GET_STR(attribute vec4 aPosition; // 顶点坐标
                                                  attribute vec2 aTexCoord;                                   // 材质坐标
                                                  varying vec2 vTexCoord;                                     // 输出材质坐标
                                                  void main() {
                                                      vTexCoord = vec2(aTexCoord.x,
                                                                       1.0 - aTexCoord.y);
                                                      gl_Position = aPosition;
                                                  });

// 片元着色器,软解码出来的格式
static const char *fragYUV420P = GET_STR(precision mediump float; // 精度
                                                 varying vec2 vTexCoord;                                   // 顶点着色器传递的材质坐标
                                                 uniform sampler2D yTexture;                               // 输入的Y材质
                                                 uniform sampler2D uTexture;                               // 输入的U材质
                                                 uniform sampler2D vTexture;                               // 输入的V材质
                                                 void main() {
                                                     vec3 yuv;
                                                     vec3 rgb;
                                                     yuv.r = texture2D(yTexture, vTexCoord).r;
                                                     yuv.g = texture2D(uTexture, vTexCoord).r - 0.5;
                                                     yuv.b = texture2D(vTexture, vTexCoord).r - 0.5;
                                                     rgb = mat3(1.0, 1.0, 1.0, 0.0, -0.39465,
                                                                2.03211, 1.13983, -0.58060, 0.0) *
                                                           yuv;
                                                     // 输出像素颜色
                                                     gl_FragColor = vec4(rgb, 1.0);
                                                 });

// 片元着色器,软解码和部分x86硬解码
static const char *fragNV12 = GET_STR(precision mediump float; //精度
                                              varying vec2 vTexCoord;                                //顶点着色器传递的坐标
                                              uniform sampler2D yTexture;                            //输入的材质（不透明灰度，单像素）
                                              uniform sampler2D uvTexture; void main() {
            vec3 yuv;
            vec3 rgb;
            yuv.r = texture2D(yTexture, vTexCoord).r;
            yuv.g = texture2D(uvTexture, vTexCoord).r - 0.5;
            yuv.b = texture2D(uvTexture, vTexCoord).a - 0.5;
            rgb = mat3(1.0, 1.0, 1.0, 0.0, -0.39465, 2.03211, 1.13983, -0.58060, 0.0) * yuv;
            //输出像素颜色
            gl_FragColor = vec4(rgb, 1.0);
        });

//片元着色器,软解码和部分x86硬解码
static const char *fragNV21 = GET_STR(precision mediump float; //精度
                                              varying vec2 vTexCoord;                                //顶点着色器传递的坐标
                                              uniform sampler2D yTexture;                            //输入的材质（不透明灰度，单像素）
                                              uniform sampler2D uvTexture; void main() {
            vec3 yuv;
            vec3 rgb;
            yuv.r = texture2D(yTexture, vTexCoord).r;
            yuv.g = texture2D(uvTexture, vTexCoord).a - 0.5;
            yuv.b = texture2D(uvTexture, vTexCoord).r - 0.5;
            rgb = mat3(1.0, 1.0, 1.0, 0.0, -0.39465, 2.03211, 1.13983, -0.58060, 0.0) * yuv;
            //输出像素颜色
            gl_FragColor = vec4(rgb, 1.0);
        });

static ANativeWindow *window = NULL;

// static void ver_reset(struct vidisp_st *st) {
//     st->ver[0] = 1.0f;
//     st->ver[1] = -1.0f;
//     st->ver[2] = 0.0f; // 右下
//     st->ver[3] = -1.0f;
//     st->ver[4] = -1.0f;
//     st->ver[5] = 0.0f; // 左下
//     st->ver[6] = 1.0f;
//     st->ver[7] = 1.0f;
//     st->ver[8] = 0.0f; // 右上
//     st->ver[9] = -1.0f;
//     st->ver[10] = 1.0f;
//     st->ver[11] = 0.0f; // 左上
// }

static void update_ver(struct vidisp_st *st) {
    float video_aspect = (float) st->video_w / (float) st->video_h;
    float view_aspect = (float) st->view_w / (float) st->view_h;

    float x_scale = 1.0f;
    float y_scale = 1.0f;

    if (video_aspect > view_aspect) {
        y_scale = view_aspect / video_aspect;
    } else {
        x_scale = video_aspect / view_aspect;
    }

    st->ver[0] = x_scale;
    st->ver[1] = -y_scale;
    st->ver[2] = 0.0f; // 右下
    st->ver[3] = -x_scale;
    st->ver[4] = -y_scale;
    st->ver[5] = 0.0f; // 左下
    st->ver[6] = x_scale;
    st->ver[7] = y_scale;
    st->ver[8] = 0.0f; // 右上
    st->ver[9] = -x_scale;
    st->ver[10] = y_scale;
    st->ver[11] = 0.0f; // 左上
}

static int init_egl(struct vidisp_st *st) {
    // EGL
    // 1.display
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        debug("eglGetDisplay failed\n");
        return 1;
    }

    if (!eglInitialize(display, 0, 0)) {
        debug("eglInitialize failed\n");
        return 1;
    }
    // 2.surface
    // 2.1 surface窗口配置
    // 输出配置
    EGLConfig config;
    EGLint configNum;
    EGLint configSpec[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
    };
    if (!eglChooseConfig(display, configSpec, &config, 1, &configNum)) {
        debug("eglChooseConfig failed\n");
        return 1;
    }
    st->view_w = ANativeWindow_getWidth(window);
    st->view_h = ANativeWindow_getHeight(window);
    if (ANativeWindow_setBuffersGeometry(window, st->view_w, st->view_h, WINDOW_FORMAT_RGBA_8888)
        != 0) {
        debug("ANativeWindow_setBuffersGeometry failed\n");
        return 1;
    }
    // 创建surface
    EGLSurface winSurface = eglCreateWindowSurface(display, config, window, 0);
    if (winSurface == EGL_NO_SURFACE) {
        debug("eglCreateWindowSurface failed\n");
        return 1;
    }
    // 3.context
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttr);
    if (context == EGL_NO_CONTEXT) {
        debug("eglCreateContext failed\n");
        return 1;
    }
    if (!eglMakeCurrent(display, winSurface, winSurface, context)) {
        debug("eglMakeCurrent failed\n");
        return 1;
    }
    // 4. 开启 VSync（防止撕裂）
    eglSwapInterval(display, 1);
    st->display = display;
    st->surface = winSurface;
    st->context = context;
    st->window = window;
    debug("EGL Init success\n");

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // 黑色
    for (int i = 0; i < 3; i++) {
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(st->display, st->surface);
    }

    return 0;
}

static GLint InitShader(const char *code, GLint type) {
    // 创建shader
    GLint sh = glCreateShader(type);
    if (sh == 0) {
        debug("glCreateShader failed\n");
        return 0;
    }
    // 加载shader
    glShaderSource(sh, 1, &code, 0);
    // 编译shader
    glCompileShader(sh);
    // 获取编译状态
    GLint status;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
    if (!status) {
        debug("glCompileShader failed\n");
        return 0;
    }
    return sh;
}

static int init_shader(struct vidisp_st *st, enum vidfmt fmt) {
    // shader初始化
    // 顶点坐标
    GLint vsh = InitShader(vertexShader, GL_VERTEX_SHADER);
    // 片元
    GLint fsh;
    if (fmt == VID_FMT_YUV420P) {
        fsh = InitShader(fragYUV420P, GL_FRAGMENT_SHADER);
    } else if (fmt == VID_FMT_NV12) {
        fsh = InitShader(fragNV12, GL_FRAGMENT_SHADER);
    } else if (fmt == VID_FMT_NV21) {
        fsh = InitShader(fragNV21, GL_FRAGMENT_SHADER);
    } else {
        debug("unsupported format\n");
        return -1;
    }
    // 创建渲染程序
    GLint program = glCreateProgram();
    if (!program) {
        debug("glCreateProgram failed\n");
        return 1;
    }
    // 渲染程序中加入着色器
    glAttachShader(program, vsh);
    glAttachShader(program, fsh);
    // 链接程序
    glLinkProgram(program);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        debug("glLinkProgram failed\n");
        return 1;
    }
    debug("glLinkProgram success\n");
    // 使用程序
    glUseProgram(program);
    debug("shader init success\n");

    // 加入三维顶点数据
    GLuint apos = (GLuint) glGetAttribLocation(program, "aPosition");
    glEnableVertexAttribArray(apos);
    // 传递顶点坐标
    glVertexAttribPointer(apos, 3, GL_FLOAT, GL_FALSE, 12, st->ver);

    // 加入材质坐标
    static float tex[] = {
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            0.0f,
            1.0f,
    };
    GLuint atexs = (GLuint) glGetAttribLocation(program, "aTexCoord");
    glEnableVertexAttribArray(atexs);
    // 传递材质坐标
    glVertexAttribPointer(atexs, 2, GL_FLOAT, GL_FALSE, 8, tex);
    st->vsh = vsh;
    st->fsh = fsh;
    st->program = program;
    debug("shader init success\n");
    return 0;
}

static int init_texture(struct vidisp_st *st, enum vidfmt fmt) {
    // 材质纹理
    switch (fmt) {
        case VID_FMT_YUV420P: {
            // 设置纹理第一层
            glUniform1i(glGetUniformLocation(st->program, "yTexture"), 0);
            // 设置纹理第二层
            glUniform1i(glGetUniformLocation(st->program, "uTexture"), 1);
            // 设置纹理第三层
            glUniform1i(glGetUniformLocation(st->program, "vTexture"), 2);
            st->textures = calloc(3, sizeof(GLuint));
            if (!st->textures) {
                // 内存分配失败
                debug("malloc VID_FMT_YUV420P textures failed\n");
                return 1;
            }
            st->texture_size = 3;
            break;
        }
        case VID_FMT_NV12: {
            // 设置纹理第一层
            glUniform1i(glGetUniformLocation(st->program, "yTexture"), 0);
            // 设置纹理第二层
            glUniform1i(glGetUniformLocation(st->program, "uvTexture"), 1);
            st->textures = calloc(2, sizeof(GLuint));
            if (!st->textures) {
                // 内存分配失败
                debug("malloc VID_FMT_NV12 textures failed\n");
                return 1;
            }
            st->texture_size = 2;
            break;
        }
        case VID_FMT_NV21: {
            // 设置纹理第一层
            glUniform1i(glGetUniformLocation(st->program, "yTexture"), 0);
            // 设置纹理第二层
            glUniform1i(glGetUniformLocation(st->program, "uvTexture"), 1);
            st->textures = calloc(2, sizeof(GLuint));
            if (!st->textures) {
                // 内存分配失败
                debug("malloc VID_FMT_NV21 textures failed\n");
                return 1;
            }
            st->texture_size = 2;
            break;
        }
        default:
            debug("unsupported format\n");
            return 1;
    }
    return 0;
}

static void texture_draw(struct vidisp_st *st, int index, int width, int height, uint8_t *buf, int isa) {
    // 打印
    //    LOGD("texture_draw index:%d width:%d height:%d isa:%d", index, width, height, isa);
    GLint format = GL_LUMINANCE;
    if (isa)
        format = GL_LUMINANCE_ALPHA;
    // 检查是否需要重新创建纹理
    int need_recreate = 0;
    if (st->textures[index] == 0) {
        need_recreate = 1;
    } else if (st->texture_width[index] != width || st->texture_height[index] != height) {
        // 尺寸变化
        need_recreate = 1;
        glDeleteTextures(1, &st->textures[index]);
        st->textures[index] = 0;
    }
    if (need_recreate) {
        // 初始化
        glGenTextures(1, &st->textures[index]);
        st->texture_width[index] = width;
        st->texture_height[index] = height;
        // 设置纹理属性
        glBindTexture(GL_TEXTURE_2D, st->textures[index]);
        // 缩小的过滤器
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // ✅ 改动
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // ✅ 改动

        // ✅ 改动: 初始化为黑色纹理，避免前几帧花屏
        int black_size = width * height * (isa ? 2 : 1);
        uint8_t *black = calloc(black_size, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, black);
        free(black);
    }
    glActiveTexture(GL_TEXTURE0 + index);
    glBindTexture(GL_TEXTURE_2D, st->textures[index]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, GL_UNSIGNED_BYTE, buf);
}

static void draw(struct vidisp_st *st, const struct vidframe *frame) {

    // ✅ 改动: 每帧清屏，保证前几帧不花屏
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    bool ver = false;
    if (st->video_w != frame->size.w || st->video_h != frame->size.h) {
        st->video_w = frame->size.w;
        st->video_h = frame->size.h;
        update_ver(st);
        ver = true;
    }

    texture_draw(st, 0, frame->size.w, frame->size.h, frame->data[0], 0);
    if (frame->fmt == VID_FMT_YUV420P) {
        texture_draw(st, 1, frame->size.w / 2, frame->size.h / 2, frame->data[1], 0);
        texture_draw(st, 2, frame->size.w / 2, frame->size.h / 2, frame->data[2], 0);
    } else {
        texture_draw(st, 1, frame->size.w / 2, frame->size.h / 2, frame->data[1], 1);
    }

    if (ver) {
        GLuint apos = glGetAttribLocation(st->program, "aPosition");
        glEnableVertexAttribArray(apos);
        glVertexAttribPointer(apos, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, st->ver);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(st->display, st->surface);
    glFinish();
}

static void egl_close(struct vidisp_st *st) {
    if (st->display == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(st->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (st->surface != EGL_NO_SURFACE)
        eglDestroySurface(st->display, st->surface);
    if (st->context != EGL_NO_CONTEXT)
        eglDestroyContext(st->display, st->context);

    eglTerminate(st->display);

    st->display = EGL_NO_DISPLAY;
    st->surface = EGL_NO_SURFACE;
    st->context = EGL_NO_CONTEXT;
}

static void shader_close(struct vidisp_st *st) {
    //释放shader
    if (st->program)
        glDeleteProgram(st->program);
    if (st->fsh)
        glDeleteShader(st->fsh);
    if (st->vsh)
        glDeleteShader(st->vsh);

    st->program = 0;
    st->vsh = 0;
    st->fsh = 0;
}

static void texture_close(struct vidisp_st *st) {
    //释放材质
    for (int i = 0; i < st->texture_size; i++) {
        if (st->textures[i]) {
            glDeleteTextures(1, &st->textures[i]);
        }
        st->textures[i] = 0;
        st->texture_width[i] = 0;
        st->texture_height[i] = 0;
    }
    free(st->textures);
    st->textures = NULL;
}

static void destructor(void *arg) {
    struct vidisp_st *st = arg;
    egl_close(st);
    shader_close(st);
    texture_close(st);
}

int opengles_alloc(struct vidisp_st **stp, const struct vidisp *vd, struct vidisp_prm *prm,
                   const char *dev, vidisp_resize_h *resizeh, void *arg) {
    int err = 0;

    (void) prm;
    (void) dev;
    (void) resizeh;
    (void) arg;
    (void) vd;

    debug("At opengles_alloc() on thread %li\n", (long) pthread_self());

    struct vidisp_st *gst = NULL;
    gst = mem_zalloc(sizeof(*gst), destructor);
    if (!gst)
        return ENOMEM;

    if (err)
        gst = mem_deref(gst);
    else
        *stp = gst;

    return err;
}

int opengles_display(
        struct vidisp_st *st, const char *title, const struct vidframe *frame, uint64_t timestamp) {
    (void) title;
    (void) timestamp;
    int err = 0;

//    debug("At opengles_display() on thread %li\n", (long) pthread_self());

    if (window == NULL) {
        return 0;
    }

    if (!st->context) {
        err = init_egl(st);
        if (err) {
            warning("Renderer context init failed with error %d\n", err);
            return err;
        }
        err = init_shader(st, frame->fmt);
        if (err) {
            warning("Renderer shader init failed with error %d\n", err);
            return err;
        }
        err = init_texture(st, frame->fmt);
        if (err) {
            warning("Renderer texture init failed with error %d\n", err);
            return err;
        }
    }
    draw(st, frame);
    return err;
}

void set_display_window(JNIEnv* env, jobject surface) {
    if (surface != 0) {
        window = ANativeWindow_fromSurface(env, surface);
    } else {
        if (window) {
            ANativeWindow_release(window);
            window = NULL;
        }
    }
}

