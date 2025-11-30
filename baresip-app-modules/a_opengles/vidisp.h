#ifndef BARESIP_VIDISP_H
#define BARESIP_VIDISP_H

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>

struct vidisp_st
{
    const struct opengles *vd;
    //    struct vidframe *vf;

    //    GLuint texture_id;
    //    GLfloat vertices[4 * 3];

    ANativeWindow *window;

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    //    EGLint width;
    //    EGLint height;

    GLint vsh;
    GLint fsh;
    GLuint program;
    GLuint *textures;
    int texture_width[4];
    int texture_height[4];
    int texture_size;
    float ver[12];
    unsigned video_w;
    unsigned video_h;
    unsigned view_w;
    unsigned view_h;
};

enum opengles_state_t {
    NONE = 0,
    START,
    STOP
};

struct opengles_t {
    int state;
    jobject surface;
    JNIEnv *env;
};

int opengles_alloc(struct vidisp_st **stp, const struct vidisp *vd, struct vidisp_prm *prm,
                          const char *dev, vidisp_resize_h *resizeh, void *arg);

int opengles_display(
        struct vidisp_st *st, const char *title, const struct vidframe *frame, uint64_t timestamp);

void set_display_window(JNIEnv* env, jobject surface);

// int opengles_module_init(void);
// int opengles_module_close(void);

#endif //BARESIP_VIDISP_H
