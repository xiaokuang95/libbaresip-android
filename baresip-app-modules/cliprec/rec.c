#include "cliprec.h"
#include "mov-file-buffer.h"

static void get_vid_name(char *buf) {
    time_t t = time(NULL);
    struct tm tm_val;

#if defined(_WIN32)
    localtime_s(&tm_val, &t);  // Windows
#else
    localtime_r(&t, &tm_val);  // Linux / Unix
#endif

    strftime(buf, 64, "VID_%Y%m%d-%H%M%S", &tm_val);
}

static void destroy(void *arg) {
    struct cliprec_ctx_t *ctx = arg;
    if (!ctx) return;

    ctx->open_flag = false;

    /* MP4 writer */
    if (ctx->mov) {
        fmp4_writer_destroy(ctx->mov);
        ctx->mov = NULL;
    }

    /* dynamic strings */
    mem_deref(ctx->file_dir);
    mem_deref(ctx->file_name);

    mem_deref(ctx->aud_src);
    mem_deref(ctx->aud_enc);

    mem_deref(ctx->vid_src);
    mem_deref(ctx->vid_src_orientation);
    mem_deref(ctx->vid_enc);

    mem_deref(ctx->vid_display);

    /* audio/video recorder objects */
    mem_deref(ctx->ar);
    mem_deref(ctx->vr);

    /* file */
    if (ctx->fp) {
        fclose(ctx->fp);
        ctx->fp = NULL;
    }

    mtx_destroy(ctx->mtx);
}

static inline int dup_if_present(char **dst, const char *src) {
    if (!src) return 0;       // null input is allowed
    return str_dup(dst, src); // propagate ENOMEM or success
}

static int rec_segmentation(struct cliprec_ctx_t *ctx) {
    ctx->open_flag = false;
    if (ctx->mov) {
        fmp4_writer_destroy(ctx->mov);
        ctx->mov = NULL;
    }
    if (ctx->fp) {
        fclose(ctx->fp);
        ctx->fp = NULL;
    }
    // 发送通知 把url传过去
    struct cliprec_callback_t *callback = (struct cliprec_callback_t *) mem_zalloc(sizeof(struct cliprec_callback_t),NULL);
//    callback->duration = ctx->flash_duration;
    str_dup(&callback->file_dir,ctx->file_dir);
    str_dup(&callback->file_name,ctx->file_name);
    bevent_app_emit(BEVENT_CUSTOM, callback, "cliprec");
    mem_deref(callback->file_dir);
    mem_deref(callback->file_name);
    mem_deref(callback);

    mem_deref(ctx->file_name);
    ctx->vid_track = ctx->aud_track = -1;
    ctx->vid_dts = ctx->aud_dts = 0;

    char file_name[64];
    get_vid_name(file_name);

    char file_path[4096];
    snprintf(file_path, sizeof(file_path), "%s/%s.mp4",
             ctx->file_dir, file_name);

    ctx->fp = fopen(file_path, "wb");
    if (!ctx->fp) {
        return ENOMEM;
    }
    ctx->mov = fmp4_writer_create(mov_file_buffer(), ctx->fp, 0);
    if (!ctx->mov) {
        return ENOMEM;
    }
    ctx->rec_date_time = tmr_jiffies();
    ctx->open_flag = true;
    return dup_if_present(&ctx->file_name, file_name);
}

int rec_alloc(struct cliprec_ctx_t **cc, struct cliprec_t *c) {
    int err = 0;
    struct cliprec_ctx_t *ctx = NULL;

    /* allocate context with destroy() as destructor */
    ctx = mem_zalloc(sizeof(*ctx), destroy);
    if (!ctx)
        return ENOMEM;

    /* -----------------------
       1. create file
       ----------------------- */
    char file_name[64];
    get_vid_name(file_name);

    char file_path[4096];
    snprintf(file_path, sizeof(file_path), "%s/%s.mp4",
             c->file_dir, file_name);

    ctx->fp = fopen(file_path, "wb");
    if (!ctx->fp) {
        err = ENOENT;
        goto fail;
    }

    /* -----------------------
       2. duplicate strings
       ----------------------- */
    if ((err = dup_if_present(&ctx->file_dir, c->file_dir))) goto fail;
    if ((err = dup_if_present(&ctx->file_name, file_name))) goto fail;

    if ((err = dup_if_present(&ctx->aud_src, c->aud_src))) goto fail;
    if ((err = dup_if_present(&ctx->aud_enc, c->aud_enc))) goto fail;

    if ((err = dup_if_present(&ctx->vid_src, c->vid_src))) goto fail;
    if ((err = dup_if_present(&ctx->vid_src_orientation, c->vid_src_orientation))) goto fail;
    if ((err = dup_if_present(&ctx->vid_enc, c->vid_enc))) goto fail;

    if ((err = dup_if_present(&ctx->vid_display, c->vid_display))) goto fail;

    /* -----------------------
       3. basic value copy
       ----------------------- */
    ctx->flash_duration = c->duration;

    ctx->aud_rate = c->aud_rate;
    ctx->aud_ch = c->aud_ch;

    ctx->vid_rate = c->vid_rate;
    ctx->vid_size = c->vid_size;

    ctx->aud_enable = c->aud_enable;
    ctx->vid_enable = c->vid_enable;
    ctx->preview_enable = c->preview_enable;

    ctx->vid_track = ctx->aud_track = -1;
    ctx->vid_dts = ctx->aud_dts = 0;

    /* -----------------------
       4. create MP4 writer
       ----------------------- */
    ctx->mov = fmp4_writer_create(mov_file_buffer(), ctx->fp, 0);
    if (!ctx->mov) {
        err = ENOMEM;
        goto fail;
    }

    /* -----------------------
       5. create audio/video recorders
       ----------------------- */
    if (ctx->aud_enable) {
        struct aud_rec_t *ar = NULL;
        err = aud_src_alloc(&ar, ctx);
        if (err) goto fail;
        ctx->ar = ar;
    }

    if (ctx->vid_enable) {
        struct vid_rec_t *vr = NULL;
        err = vid_src_alloc(&vr, ctx);
        if (err) goto fail;
        ctx->vr = vr;
    }

    ctx->segment = rec_segmentation;

    /* -----------------------
       6. lock
       ----------------------- */
    err  = mutex_alloc(&ctx->mtx);
    if (err) goto fail;

    /* -----------------------
       7. success
       ----------------------- */
    ctx->rec_date_time = tmr_jiffies();
    ctx->open_flag = true;
    *cc = ctx;

    return 0;

    fail:
    mem_deref(ctx);  // triggers destroy()
    return err;
}
