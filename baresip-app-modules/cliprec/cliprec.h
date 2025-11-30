#ifndef CLIPREC_CLIPREC_H
#define CLIPREC_CLIPREC_H

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libmov/mov-writer.h>
#include <libmov/mov-format.h>
#include <libflv/mpeg4-avc.h>
#include <libflv/mpeg4-aac.h>
#include <libflv/mpeg4-hevc.h>
#include <libmov/fmp4-writer.h>
#include "common.h"

struct cliprec_ctx_t;

#define  MaxWriteMp4BufferCount  1024*1024*3

typedef int (rec_segment)(struct cliprec_ctx_t *ctx);

struct cliprec_ctx_t {
    uint32_t vid_rate;
    char *file_dir;
    char *file_name;
    struct vidsz vid_size;
    uint32_t flash_duration; // seconds
    int32_t vid_track;
    int32_t aud_track;
    uint32_t vid_dts;
    uint32_t aud_dts;
    fmp4_writer_t *mov;
    FILE *fp;
    bool open_flag;
    uint64_t rec_date_time;
    struct aud_rec_t *ar;
    struct vid_rec_t *vr;
    uint32_t aud_rate;
    uint32_t aud_ch;
    char *aud_enc;
    char *vid_enc;
    char *aud_src;
    char *vid_src;
    char *vid_src_orientation;
    int vid_src_fmt;
    char *vid_display;
    bool aud_enable;
    bool vid_enable;
    bool preview_enable;

    rec_segment *segment;
    mtx_t *mtx;
};

struct aud_rec_t {
    struct ausrc_st *ausrc;
    struct auenc_state *enc;
    const struct aucodec *acd;
    struct cliprec_ctx_t *ctx;
    struct mbuf *mb;
    struct mpeg4_aac_t aac;
    const struct rtp_payload_decoder_t *payload_decoder;
};

struct vid_rec_t {
    /* 8-byte aligned pointers */
    struct vidsrc_st *vidsrc;
    struct vidisp_st *vidisp;
    struct videnc_state *enc;
    const struct vidcodec *vcd;
    const struct vidisp *vvd;
    struct cliprec_ctx_t *ctx;
    struct mbuf *mb_frame;
    const struct rtp_payload_decoder_t *payload_decoder;

    /* 8-byte aligned structs (depends on mpeg4_avc_t definition) */
    struct mpeg4_avc_t avc;
};

enum cliprec_state_t {
    NONE = 0,
    START,
    STOP
};

struct cliprec_t {
    int state;
    char *file_dir;
    uint32_t duration; // seconds
    uint32_t vid_rate;
    struct vidsz vid_size;
    uint32_t aud_rate;
    uint32_t aud_ch;
    char *aud_enc;
    char *vid_enc;
    char *aud_src;
    char *vid_src;
    char *vid_src_orientation;
    int vid_src_fmt;
    char *vid_display;
    bool aud_enable;
    bool vid_enable;
    bool preview_enable;
};

struct cliprec_callback_t {
    char *file_dir;
    char *file_name;
//    uint32_t duration; // seconds
};

int rec_alloc(struct cliprec_ctx_t **cc, struct cliprec_t *c);

int vid_src_alloc(struct vid_rec_t **vr, struct cliprec_ctx_t *cliprec_ctx);

int aud_src_alloc(struct aud_rec_t **ar, struct cliprec_ctx_t *cliprec_ctx);

#endif
