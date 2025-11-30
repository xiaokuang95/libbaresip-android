#include "common.h"
#include <baresip.h>
#include <librtp/rtp-payload.h>

static struct list decoderl;

static struct list *baresip_rtp_payload_decoderl(void)
{
	return &decoderl;
}

const struct rtp_payload_decoder_t *rtp_payload_decoder_find_decoder(const char *name)
{
	struct le *le;

	for (le=list_head(baresip_rtp_payload_decoderl()); le; le=le->next) {

		struct rtp_payload_decoder_t *handler = le->data;

		if (name && 0 != str_casecmp(name, handler->name))
			continue;

		if (handler->input && handler->writer)
			return handler;
	}

	return NULL;
}

void rtp_payload_decoder_register(struct rtp_payload_decoder_t *handler)
{
	if (!handler)
		return;

	list_append(baresip_rtp_payload_decoderl(), &handler->le, handler);

	info("rtp_payload_decoder name: %s payload_type: %d\n", handler->name,handler->payload);
}

void rtp_payload_decoder_unregister(struct rtp_payload_decoder_t *handler)
{
	if (!handler)
		return;

	list_unlink(&handler->le);
}
