#include "cliprec.h"
#include "common.h"

static void destructor(void *arg) {
    struct aud_rec_t *r = arg;
    if (r) {
        mem_deref(r->ausrc);
        mem_deref(r->enc);
        mem_deref(r->mb);
    }
}

static void ausrc_read_handler(struct auframe *af, void *arg) {
    struct aud_rec_t *r = arg;
    if (!r || !r->acd || !r->enc || !af)
        return;

    int err;
    bool marker = false;
    r->mb->pos = r->mb->end = 16;
    size_t len = mbuf_get_space(r->mb);

    err = r->acd->ench(r->enc, &marker, mbuf_buf(r->mb), &len, af->fmt, af->sampv, af->sampc);
    if ((err & 0xffff0000) == 0x00010000) {
        /* MPA needs some special treatment here */
        af->sampc = 0;
    } else if (err) {
        warning("cliprec: %s encode error: %d samples (%m)\n",
                r->acd->name, af->sampc, err);
        return;
    }
    if (len > 0) {
        mbuf_set_end(r->mb, mbuf_pos(r->mb) + len);
        if (r->ctx->vid_track != -1 || (!r->ctx->vid_enable && r->ctx->aud_enable)) {
            if (tmr_jiffies() - r->ctx->rec_date_time >= (1000 * r->ctx->flash_duration)) {
                mtx_lock(r->ctx->mtx);
				if (r->ctx->segment) {
                	r->ctx->segment(r->ctx);
				}
                mtx_unlock(r->ctx->mtx);
            }
            mtx_lock(r->ctx->mtx);
			if (r->payload_decoder) {
            	r->payload_decoder->input(r->mb, true, r);
			}
            mtx_unlock(r->ctx->mtx);
        }
        mbuf_rewind(r->mb);
    }
}

static void ausrc_error_handler(int err, const char *str, void *arg) {
    (void) str;
    struct aud_rec_t *r = arg;
    warning("ausrc_error_handler err: %d\n", err);
    mem_deref(r);
}

static int init_au_source(struct aud_rec_t *r) {
    int err = 0;

    char *cliprec_aud_src = r->ctx->aud_src;
    int fmt = conf_config()->audio.src_fmt;
    int32_t srate = r->ctx->aud_rate;
    int32_t ch = r->ctx->aud_ch;
    const struct ausrc *as = ausrc_find(baresip_ausrcl(), cliprec_aud_src);
    if (!as) {
        warning("cliprec: source not found: %s\n", cliprec_aud_src);
        err = ENOMEM;
        goto out;
    }

    struct ausrc_prm src_prm = {
            .srate = srate,
            .ch = ch,
            .ptime = 20,
            .fmt = fmt};
    err = as->alloch(&r->ausrc, as, &src_prm, cliprec_aud_src,
                     ausrc_read_handler, ausrc_error_handler, r);
    if (err) {
        warning("cliprec: audio source alloc failed: %d\n", err);
        goto out;
    }
    r->mb = mbuf_alloc(16 + 4096);
    if (!r->mb) {
        err = ENOMEM;
        goto out;
    }

    out:
    return err;
}

static int init_au_encoder(struct aud_rec_t *r) {
    int err = 0;

    char *cliprec_aud_encode = r->ctx->aud_enc;
    int32_t srate = r->ctx->aud_rate;
    int32_t ch = r->ctx->aud_ch;
    struct auenc_param enc_prm = {
            .bitrate = 0};
    r->acd = aucodec_find(baresip_aucodecl(), cliprec_aud_encode, srate, ch);
    if (!r->acd) {
        warning("audio: encoder not found: %s\n", cliprec_aud_encode);
        err = ENOMEM;
        goto out;
    }

    err = r->acd->encupdh(&r->enc, r->acd, &enc_prm, NULL);
    if (err) {
        warning("audio: encoder alloc failed: %d\n", err);
        goto out;
    }
    r->payload_decoder = rtp_payload_decoder_find_decoder(r->ctx->aud_enc);
    if (!r->payload_decoder) {
        err = ENOMEM;
        goto out;
    }

    out:
    return err;
}

int aud_src_alloc(struct aud_rec_t **art, struct cliprec_ctx_t *cliprec_ctx) {
    int err;

    struct aud_rec_t *ar = NULL;
    ar = mem_zalloc(sizeof(*ar), destructor);
    if (!ar) {
        err = ENOMEM;
        goto out;
    }
    ar->ctx = cliprec_ctx;
    err = init_au_source(ar);
    if (err)
        goto out;
    err = init_au_encoder(ar);
    if (err)
        goto out;

    out:
    if (err) {
        ar = mem_deref(ar);
    } else {
        *art = ar;
    }
    debug("start voice recording code: %d\n", err);
    return err;
}
