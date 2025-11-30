#include "cliprec.h"
#include "h264.h"

static int rtp_h264_unpack_stap(struct mbuf *mb, bool marker, void *arg) {
    int err = 0;
    struct vid_rec_t *r = arg;
    const uint8_t sc4[] = {0, 0, 0, 1};
    while (mbuf_get_left(mb) >= sizeof(uint16_t)) {
        uint16_t len = ntohs(mbuf_read_u16(mb));

        if (len < 1 || len > mbuf_get_left(mb))
            return EBADMSG;
        err = mbuf_write_mem(r->mb_frame, sc4, sizeof(sc4));
        err |= mbuf_write_mem(r->mb_frame, mbuf_buf(mb), len);
        err |= r->payload_decoder->writer(r->mb_frame, arg);
        if (err)
            return err;
        mbuf_rewind(r->mb_frame);
        mbuf_advance(mb, len);
    }
    if (marker) {
        mbuf_rewind(r->mb_frame);
    }
    return err;
}

static int rtp_h264_unpack_fu(struct mbuf *mb, bool marker, void *arg) {
    struct vid_rec_t *r = arg;
    const uint8_t sc4[] = {0, 0, 0, 1};
    int err;
    struct h264_nal_header h264_hdr;
    struct h264_fu fu;
    err = h264_nal_header_decode(&h264_hdr, mb);
    if (err)
        return err;
    err = h264_fu_hdr_decode(&fu, mb);
    if (err)
        return err;
    h264_hdr.type = fu.type;

    if (fu.s) {
        mbuf_write_mem(r->mb_frame, sc4, sizeof(sc4));
        /* encode NAL header back to buffer */
        err = h264_nal_header_encode(r->mb_frame, &h264_hdr);
        if (err)
            return err;
    }
    err = mbuf_write_mem(r->mb_frame, mbuf_buf(mb),
                         mbuf_get_left(mb));
    if (err)
        return err;

    if (fu.e) {
        if (marker) {
            err = r->payload_decoder->writer(r->mb_frame, arg);
            mbuf_rewind(r->mb_frame);
        }
    }

    return err;
}

static int rtp_h264_unpack_nalu(struct mbuf *mb, bool marker, void *arg) {
    int err;
    const uint8_t sc4[] = {0, 0, 0, 1};
    struct vid_rec_t *r = arg;
    err = mbuf_write_mem(r->mb_frame, sc4, sizeof(sc4));
    err |= mbuf_write_mem(r->mb_frame, mbuf_buf(mb),
                          mbuf_get_left(mb));
    if (marker) {
        err = r->payload_decoder->writer(r->mb_frame, arg);
        mbuf_rewind(r->mb_frame);
    }
    return err;
}

static int h264_input_payload(struct mbuf *mb, bool marker, void *arg) {
    switch (mb->buf[0] & 0x1F) {
        case 0:     // reserved
        case 31: // reserved
            assert(0);
            return 0; // packet discard

        case 24: // STAP-A
            return rtp_h264_unpack_stap(mb, marker, arg);
        case 25: // STAP-B
            // return rtp_h264_unpack_stap(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 1);
            return 0;
        case 26: // MTAP16
            // return rtp_h264_unpack_mtap(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 2);
            return 0;
        case 27: // MTAP24
            // return rtp_h264_unpack_mtap(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 3);
            return 0;
        case 28: // FU-A
            return rtp_h264_unpack_fu(mb, marker, arg);
        case 29: // FU-B
            // return rtp_h264_unpack_fu(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 1);
            return 0;

        default: // 1-23 NAL unit
            return rtp_h264_unpack_nalu(mb, marker, arg);
    }
}

static int h264_writer_mp4(struct mbuf *mb, void *arg) {
//    warning("h264_writer_mp4:%zu ts:%llu\n", mb->pos, ts);
    int err = 0;
    int vcl = 0, update = 0;
    struct vid_rec_t *r = arg;
    if (!r->ctx->open_flag || !r->ctx->mov)
        return EINVAL;
    uint8_t *avcc_data = mem_zalloc(mb->end, NULL);
    int avcc_len = h264_annexbtomp4(&r->avc, mb->buf, mb->pos,
                                    avcc_data, mb->end,
                                    &vcl, &update);
    if (r->ctx->vid_track == -1) {
        if (r->avc.nb_sps < 1 || r->avc.nb_pps < 1) {
            err = EINVAL;
            goto out;
        }

        uint8_t extra_data[1024];
        int extra_data_size = mpeg4_avc_decoder_configuration_record_save(
                &r->avc, extra_data, sizeof(extra_data));

        if (extra_data_size <= 0) {
            err = EINVAL;
            goto out;
        }

        int w = r->ctx->vid_size.w;
        int h = r->ctx->vid_size.h;

        r->ctx->vid_track = fmp4_writer_add_video(r->ctx->mov, MOV_OBJECT_H264,
                                                  w, h,
                                                  extra_data, extra_data_size);
        if (r->ctx->vid_track == -1) {
            err = EINVAL;
            goto out;
        }
    }
    // int64_t dts = ts * 1000 / 90000;
    fmp4_writer_write(r->ctx->mov, r->ctx->vid_track,
                      avcc_data, avcc_len,
                      r->ctx->vid_dts, r->ctx->vid_dts,
                      (vcl == 1) ? MOV_AV_FLAG_KEYFREAME : 0);
    uint32_t step = 1000 / r->ctx->vid_rate;
    r->ctx->vid_dts += step;
    out:
    if (avcc_data) mem_deref(avcc_data);
    return err;
}

static struct rtp_payload_decoder_t h264_decoder = {
        .name = "H264",
        .payload = 96,
        .input = h264_input_payload,
        .writer = h264_writer_mp4
};

void h264_register(void) {
    rtp_payload_decoder_register(&h264_decoder);
}

void h264_unregister(void) {
    rtp_payload_decoder_unregister(&h264_decoder);
}
