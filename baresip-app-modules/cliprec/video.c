#include "cliprec.h"
#include <librtp/rtp-payload.h>

#define RTP_PRESZ 16
#define RTP_TRAILSZ 16

static void destructor(void *arg) {
    struct vid_rec_t *r = arg;
    if (r) {
        mem_deref(r->vidsrc);
        mem_deref(r->enc);
        mem_deref(r->vidisp);
        mem_deref(r->mb_frame);
    }
}

static void vidsrc_frame_handler(struct vidframe *frame, uint64_t timestamp,
                                 void *arg) {
    int err;
    struct vid_rec_t *r = arg;
    if (!r->vcd) {
        warning("cliprec: encoder not found\n");
        return;
    }
    if (r->vvd) {
        err = r->vvd->disph(r->vidisp, "preview", frame, timestamp);
        if (err) {
            warning("cliprec: display error: %d\n", err);
            return;
        }
    }
    if (!r->enc)
        warning("cliprec r->enc is null\n");
    if (!frame)
        warning("cliprec frame is null\n");
    err = r->vcd->ench(r->enc, false, frame, timestamp);
    if (err) {
        warning("cliprec: encoder error: %d\n", err);
        return;
    }
}

static int packet_handler(bool marker, uint64_t ts,
                          const uint8_t *hdr, size_t hdr_len,
                          const uint8_t *pld, size_t pld_len,
                          const struct video *arg) {
    (void) ts;
//    if (marker) {
//        warning("packet_handler ts:%llu marker:%d\n", ts, marker);
//    }
    struct vid_rec_t *r = (struct vid_rec_t *) arg;
    struct mbuf *mb = mbuf_alloc(hdr_len + pld_len);
    if (hdr)
        (void) mbuf_write_mem(mb, hdr, hdr_len);
    (void) mbuf_write_mem(mb, pld, pld_len);
    mbuf_set_pos(mb, 0);
    if (tmr_jiffies() - r->ctx->rec_date_time >= (1000 * r->ctx->flash_duration)) {
        mtx_lock(r->ctx->mtx);
		if (r->ctx->segment) {
        	r->ctx->segment(r->ctx);
		}
        mtx_unlock(r->ctx->mtx);
    }
    mtx_lock(r->ctx->mtx);
	if (r->payload_decoder) {
    	int err = r->payload_decoder->input(mb, marker, r);
 		if (err) {
        	warning("r->payload_decoder->input failed :%d\n", err);
    	}
	}
    mtx_unlock(r->ctx->mtx);
    mem_deref(mb);
    return 0;
}

static void vidsrc_error_handler(int err, void *arg) {
    struct vid_rec_t *vr = arg;
    warning("cliprec: video-source error: %d\n", err);
    mem_deref(vr);
}

static int init_vid_source(struct vid_rec_t *r) {
    int err;

    char *cliprec_vid_src = r->ctx->vid_src;
    char *cliprec_vid_orientation = r->ctx->vid_src_orientation;
    int cliprec_vid_fmt = r->ctx->vid_src_fmt;

    struct vidsrc_prm src_prm = {
            .fmt = cliprec_vid_fmt,
            .fps = r->ctx->vid_rate,
    };

    const struct vidsrc *vs = vidsrc_find(baresip_vidsrcl(),
                                          cliprec_vid_src);
    if (!vs) {
        warning("cliprec: source not found: %s\n",
                cliprec_vid_src);
        err = ENOENT;
        goto out;
    }

    err = vs->alloch(&r->vidsrc, vs, &src_prm,
                     &r->ctx->vid_size, NULL, cliprec_vid_orientation,
                     vidsrc_frame_handler, NULL,
                     vidsrc_error_handler, r);
    if (err) {
        warning("cliprec: video source alloc failed: %d\n", err);
        goto out;
    }

    out:
    return err;
}

static int init_vid_encoder(struct vid_rec_t *r) {
    int err;
    char *cliprec_vid_encode = r->ctx->vid_enc;
    unsigned w = r->ctx->vid_size.w;
    unsigned h = r->ctx->vid_size.h;
    uint32_t fps = r->ctx->vid_rate;
    double factor = 0.08;
    int32_t cliprec_vid_bitrate = w * h * fps * factor;
    struct videnc_param enc_prm = {
            .fps = r->ctx->vid_rate,
            .pktsize = 1280,
            .max_fs = -1,
            .bitrate = cliprec_vid_bitrate,
    };

    r->vcd = (struct vidcodec *)
            vidcodec_find_encoder(baresip_vidcodecl(), cliprec_vid_encode);
    if (!r->vcd) {
        warning("cliprec: encoder not found: %s\n", cliprec_vid_encode);
        err = ENOENT;
        goto out;
    }

    err = r->vcd->encupdh(&r->enc, r->vcd, &enc_prm, NULL,
                          packet_handler, (void *) r);
    if (err) {
        warning("video: encoder alloc failed: %d\n", err);
        goto out;
    }

    r->mb_frame = mbuf_alloc(MaxWriteMp4BufferCount);
    if (!r->mb_frame) {
        err = ENOMEM;
        goto out;
    }

    r->payload_decoder = rtp_payload_decoder_find_decoder(r->ctx->vid_enc);
    if (!r->payload_decoder) {
        err = ENOMEM;
        goto out;
    }

    out:
    return err;
}

static int init_vid_display(struct vid_rec_t *r) {
    int err;
    char *cliprec_vid_display = r->ctx->vid_display;
    r->vvd = vidisp_find(baresip_vidispl(), cliprec_vid_display);
    if (!r->vvd) {
        warning("cliprec: display not found: %s\n", cliprec_vid_display);
        err = ENOENT;
        goto out;
    }

    struct vidisp_prm dis_prm;
    err = r->vvd->alloch(&r->vidisp, r->vvd, &dis_prm, cliprec_vid_display,
                         NULL, r);
    if (err) {
        warning("video: display alloc failed: %d\n", err);
        goto out;
    }

    out:
    return err;
}

int vid_src_alloc(struct vid_rec_t **vr, struct cliprec_ctx_t *ctx) {
    int err;
    struct vid_rec_t *vrt = NULL;
    vrt = mem_zalloc(sizeof(*vrt), destructor);
    if (!vrt) {
        err = ENOMEM;
        goto out;
    }
    vrt->ctx = ctx;
    err = init_vid_source(vrt);
    if (err)
        goto out;
    err = init_vid_encoder(vrt);
    if (err)
        goto out;
    if (ctx->preview_enable) {
        err = init_vid_display(vrt);
        if (err)
            goto out;
    }

    out:
    if (err) {
        mem_deref(vrt);
    } else {
        *vr = vrt;
    }
    return err;
}
