#ifndef CLIPREC_COMMON_H
#define CLIPREC_COMMON_H

#include <re.h>

struct rtp_payload_decoder_t {
    char *name;
    int payload;
    struct le le;
    int (*input)(struct mbuf *mb, bool marker, void *arg);
    int (*writer)(struct mbuf *mb, void *arg);
};

const struct rtp_payload_decoder_t *rtp_payload_decoder_find_decoder(const char *name);

void rtp_payload_decoder_register(struct rtp_payload_decoder_t *handler);

void rtp_payload_decoder_unregister(struct rtp_payload_decoder_t *handler);

#endif
