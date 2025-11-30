#include "cliprec.h"
#include "aac.h"
#include "h264.h"
#include <rem.h>

static struct cliprec_ctx_t *ctx=NULL;

static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	int err;
	(void)arg;
	const char *text = bevent_get_text(event);

	switch (ev) {
	case BEVENT_CUSTOM:
		if (0 == str_casecmp(text, "cliprec")) {
			struct cliprec_t *rec = bevent_get_apparg(event); 
			if (rec->state == START) {
				if (ctx) {
					ctx = mem_deref(ctx);
                    sys_msleep(50);
				}
				if (rec->duration == 0) {
					rec->duration = 1800; // 默认30分钟
				}
				// 音频
				if (rec->aud_rate == 0) {
					rec->aud_rate = 48000;
				}
				if (rec->aud_ch == 0) {
					rec->aud_ch = 1;
				}
				if (!rec->aud_src) {
					str_dup(&rec->aud_src,"opensles");
				}
				if (!rec->aud_enc) {
					str_dup(&rec->aud_enc,"mpeg4-generic");
				}
				// 视频
				if (rec->vid_rate == 0) {
					rec->vid_rate = 30; // 默认30帧
				}
				if (rec->vid_size.w == 0 && rec->vid_size.h == 0) {
					rec->vid_size = (struct vidsz){1280,720};
				}
				if (!rec->vid_src) {
					str_dup(&rec->vid_src,"android_camera2");
				}
				if (!rec->vid_src_orientation) {
					str_dup(&rec->vid_src_orientation,"0");
				}
				if (!rec->vid_enc) {
					str_dup(&rec->vid_enc,"H264");
				}
				if (!rec->vid_display) {
					str_dup(&rec->vid_display,"opengles");
				}
				err = rec_alloc(&ctx, rec);
				if (err) {
					warning("cliprec: CUSTOM event received: START recording failed: %m\n", err);
					break;
				}
				info("cliprec: CUSTOM event received: START recording\n");
			} else if (rec->state == STOP) {
				info("cliprec: CUSTOM event received: STOP recording\n");
				ctx = mem_deref(ctx);
			}
		}
		break;
		
	default:
		break;
	}
}

static int module_init(void)
{
	int err;

	//
	// list_init(&sessionl);
	// 注册 编解码器 
	rtp_payload_decoder_register(&aac_decoder);
	// rtp_payload_decoder_register(&h264_decoder);
	h264_register();


	err = bevent_register(event_handler, 0);
	if (err)
		return err;

	debug("cliprec: module loaded\n");

	return 0;
}

static int module_close(void)
{
	debug("cliprec: module closing..\n");

	// if (!list_isempty(&sessionl)) {

	// 	info("echo: flushing %u sessions\n", list_count(&sessionl));
	// 	list_flush(&sessionl);
	// }
	rtp_payload_decoder_unregister(&aac_decoder);
	// rtp_payload_decoder_unregister(&h264_decoder);
	h264_unregister();
	bevent_unregister(event_handler);
	return 0;
}

const struct mod_export DECL_EXPORTS(cliprec) = {
	"cliprec",
	"application",
	module_init,
	module_close
};
