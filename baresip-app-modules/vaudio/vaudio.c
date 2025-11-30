#include <stdint.h>
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

struct ausrc_st {
	ausrc_read_h *rh;
	void *arg;
	struct ausrc_prm prm;  /* copy of original parameters */
	size_t sampc;           /* total samples per tick */
	size_t sampsz;          /* bytes per sample */
	void *buf;              /* zeroed PCM buffer */
	bool started;
	struct tmr tmr;
    uint64_t samps;
	uint32_t ptime;
};

static void tmr_handler(void *arg)
{
    struct ausrc_st *st = arg;

    if (!st->started)
        return;

    if (st->rh) {
        struct auframe af;
        /* 初始化 auframe */
        auframe_init(&af, st->prm.fmt, st->buf,
                     st->sampc * st->prm.ch * st->sampsz,
                     st->prm.srate, st->prm.ch);
        af.sampc = st->sampc;
        af.timestamp = st->samps * AUDIO_TIMEBASE /
		       (st->prm.srate * st->prm.ch);
	    st->samps += af.sampc;
        st->rh(&af, st->arg);
    }

    /* 下次 tick */
    tmr_start(&st->tmr, st->ptime, tmr_handler, st);
}


static void destructor(void *arg)
{
	struct ausrc_st *st = arg;

	st->started = false;
	tmr_cancel(&st->tmr);
	if (st->buf) {
		mem_deref(st->buf);
	}
}

static int vaudio_alloc(struct ausrc_st **stp, const struct ausrc *as,
			  struct ausrc_prm *prm, const char *dev,
			  ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	(void) as; 
	(void) dev;
	(void) errh;
	uint32_t ptime = 3000;
	conf_get_u32(conf_cur(), "vaudio_ptime", &ptime);

	if (!stp || !prm || !rh)
		return EINVAL;

	struct ausrc_st *st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->ptime = ptime;
	st->rh  = rh;
	st->arg = arg;
	st->prm = *prm;  /* 保存参数副本 */
	st->sampsz = aufmt_sample_size(prm->fmt);
	st->sampc  = prm->srate * prm->ptime / 1000;  /* 每个 tick 的采样数 */
    st->samps=0;
	st->buf    = mem_zalloc(st->sampc * prm->ch * st->sampsz, NULL);
	if (!st->buf) {
		mem_deref(st);
		return ENOMEM;
	}

	st->started = true;
	tmr_start(&st->tmr, st->ptime, tmr_handler, st);

	*stp = st;

	return 0;
}

/* Module glue */
static struct ausrc *ausrc;

static int module_init(void)
{
	return ausrc_register(&ausrc, baresip_ausrcl(),
	                      "vaudio", vaudio_alloc);
}

static int module_close(void)
{
	ausrc = mem_deref(ausrc);
	return 0;
}

EXPORT_SYM const struct mod_export DECL_EXPORTS(vaudio) = {
	"vaudio",
	"audio",
	module_init,
	module_close
};
