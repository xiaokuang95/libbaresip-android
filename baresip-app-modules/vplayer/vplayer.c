#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <stdint.h>

struct auplay_st {
	char _unused;
};

static void destructor(void *arg) {
	(void) arg;
}

static int vplayer_alloc(struct auplay_st **stp, const struct auplay *ap,
                  struct auplay_prm *prm, const char *device,
                  auplay_write_h *wh, void *arg) {
  (void) ap;
  (void) prm;
  (void) device;
  (void) wh;
  (void) arg;
  struct auplay_st *st;
  st = mem_zalloc(sizeof(*st), destructor);
  if (!st)
    return ENOMEM;

  *stp = st;
  return 0;
}

/* Module glue */
static struct auplay *auplay;

static int module_init(void) {
  return auplay_register(&auplay, baresip_auplayl(), "vplayer", vplayer_alloc);
}

static int module_close(void) {
  auplay = mem_deref(auplay);
  return 0;
}

EXPORT_SYM const struct mod_export DECL_EXPORTS(vplayer) = {
    "vplayer", "audio", module_init, module_close};
