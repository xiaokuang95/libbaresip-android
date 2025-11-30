#include "cliprec.h"
#include "aac.h"

struct au_hdr {
    uint16_t offset;
    uint16_t size;
    uint16_t count;
};

static int hdr_decode(struct au_hdr *au_data, const uint8_t *p,
                      const size_t plen) {
    uint16_t au_headers_length;
    uint16_t au_data_offset;
    uint16_t au_data_length;
    uint16_t bits;

    if (plen < sizeof(uint16_t) * 2)
        return EPROTO;

    au_headers_length = ntohs(*(uint16_t *) (void *) &p[0]);
    au_data_offset = sizeof(uint16_t) + (au_headers_length / 8);
    au_data_length = plen - au_data_offset;
    au_data->count = (au_headers_length / (sizeof(uint16_t) * 8));

    au_data->offset = au_data_offset;

    bits = ntohs(*(uint16_t *) (void *) &p[2]);

    au_data->size = bits >> ((sizeof(uint16_t) * 8) - 13);

    if (au_data->size == 0) {
        warning("cliprec: aac: decode: invalid access unit size (zero)\n",
                au_data->size);
        return EBADMSG;
    }

    if (au_data->size > au_data_length) {
        debug("cliprec: aac: decode: fragmented access unit "
              "(au-data-size: %zu > packet-data-size: %zu)\n",
              au_data->size, au_data_length);
    }

    if (au_data->size != au_data_length) {
        debug("cliprec: aac: decode: multiple access units per packet (%zu)\n",
              au_data->count);
    }

    return 0;
}

static int aac_input_payload(struct mbuf *mb, bool marker, void *arg) {
    (void) marker;
    int err;
    struct aud_rec_t *r = arg;
    struct au_hdr au_data;
    err = hdr_decode(&au_data, mbuf_buf(mb), mbuf_get_left(mb));
    if (err) {
        warning("cliprec: audio: hdr_decode failed: %d\n", err);
        return err;
    }

    for (int i = 0; i < au_data.count; i++) {
        mbuf_advance(mb, -3);
        int aac_len = au_data.size;
        int profile = 2;
        int samplerate = r->ctx->aud_rate;
        int ch = r->ctx->aud_ch;

        int freq_idx;
        switch (samplerate) {
            case 96000:
                freq_idx = 0;
                break;
            case 88200:
                freq_idx = 1;
                break;
            case 64000:
                freq_idx = 2;
                break;
            case 48000:
                freq_idx = 3;
                break;
            case 44100:
                freq_idx = 4;
                break;
            case 32000:
                freq_idx = 5;
                break;
            case 24000:
                freq_idx = 6;
                break;
            case 22050:
                freq_idx = 7;
                break;
            case 16000:
                freq_idx = 8;
                break;
            case 12000:
                freq_idx = 9;
                break;
            case 11025:
                freq_idx = 10;
                break;
            case 8000:
                freq_idx = 11;
                break;
            default:
                freq_idx = 3;
                break; // 默认 48k
        }

        int frame_length = aac_len + 7;

        mbuf_write_u8(mb, 0xFF);
        mbuf_write_u8(mb, 0xF0 | (0 << 3) | (0x00 << 2) | 0x01);
        mbuf_write_u8(mb, ((profile - 1) << 6) | ((freq_idx & 0x0F) << 2) | ((ch >> 2) & 0x01));
        mbuf_write_u8(mb, ((ch & 0x03) << 6) | ((frame_length >> 11) & 0x03));
        mbuf_write_u8(mb, (uint8_t) (frame_length >> 3));
        mbuf_write_u8(mb, ((frame_length & 0x07) << 5) | 0x1F);
        mbuf_write_u8(mb, 0xFC | ((frame_length / 1024) & 0x03));
        mbuf_advance(mb, -7);
        mbuf_set_end(mb, mbuf_pos(mb) + frame_length);
        r->payload_decoder->writer(mb, r);
        au_data.offset += aac_len;
    }
    return 0;
}

static int aac_writer_mp4(struct mbuf *mb, void *arg) {
    struct aud_rec_t *r = arg;
    int frameLen = mpeg4_aac_adts_frame_length(mbuf_buf(mb), mbuf_get_left(mb));
    if (frameLen < 0) return false;
    if (r->ctx->aud_track == -1) {
        uint8_t asc[16];
        mpeg4_aac_adts_load(mbuf_buf(mb), mbuf_get_left(mb), &r->aac);
        int len = mpeg4_aac_audio_specific_config_save(&r->aac, asc, sizeof(asc));

        r->ctx->aud_track = fmp4_writer_add_audio(
                r->ctx->mov, MOV_OBJECT_AAC,
                r->aac.channels, 16,
                r->aac.sampling_frequency,
                asc, len);

        if (r->ctx->aud_track == -1) return false;
    }

    mbuf_advance(mb, 7);
    fmp4_writer_write(r->ctx->mov, r->ctx->aud_track,
                      mbuf_buf(mb), mbuf_get_left(mb),
                      r->ctx->aud_dts, r->ctx->aud_dts, 0);

    int step = 1024 * 1000 / r->aac.sampling_frequency;
    r->ctx->aud_dts += step;
    return 0;
}

struct rtp_payload_decoder_t aac_decoder = {
        .name = "mpeg4-generic",
        .payload = 97,
        .input = aac_input_payload,
        .writer = aac_writer_mp4,
};
