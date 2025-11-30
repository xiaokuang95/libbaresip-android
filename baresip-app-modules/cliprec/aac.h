#ifndef CLIPREC_AAC_H
#define CLIPREC_AAC_H
#include "common.h"

// void *aac_alloc(void *param, int bytes);
// void aac_free(void *param, void *packet);
// int aac_packet(void *param, const void *packet, int bytes, uint32_t timestamp, int flags);
// int aac_writer_frame(uint8_t *buf, size_t len, void *arg);

extern struct rtp_payload_decoder_t aac_decoder;

#endif
