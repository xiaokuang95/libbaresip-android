#include "vidisp.h"

static struct vidisp *vid;

static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
    (void)arg;
    const char *text = bevent_get_text(event);

    switch (ev) {
        case BEVENT_CUSTOM:
            if (0 == str_casecmp(text, "opengles")) {
                struct opengles_t *gles = bevent_get_apparg(event);
                if (gles->state == START) {
                    set_display_window(gles->env, gles->surface);
                    info("opengles: START\n");
                } else if (gles->state == STOP) {
                    set_display_window(gles->env, gles->surface);
                    info("opengles: STOP\n");
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

    err = bevent_register(event_handler, 0);
    if (err)
        return err;

    debug("opengles: module loaded\n");
    err = vidisp_register(
            &vid, baresip_vidispl(), "opengles", opengles_alloc, NULL, opengles_display, NULL);
    return err;
}


static int module_close(void)
{
    debug("opengles: module closing..\n");
    bevent_unregister(event_handler);
    vid = mem_deref(vid);
    return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(a_opengles) = {
       "a_opengles",
       "vidisp",
       module_init,
       module_close,
};
