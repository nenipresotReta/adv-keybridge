/* SPDX-License-Identifier: MIT */
#include "session.h"

static void stop(struct usb_session *s, const struct usb_session_ops *ops) {
    s->phase = USB_SESSION_STOPPING;
    ops->input(false); /* Invalidate/release before any hardware teardown. */
    ops->stop();
    s->phase = s->error ? USB_SESSION_FAILED : USB_SESSION_OFF;
}

void usb_session_step(struct usb_session *s, bool enabled, int fault,
                      const struct usb_session_ops *ops) {
    if (s->phase == USB_SESSION_RUNNING) {
        if (fault || !enabled) {
            s->error = fault;
            stop(s, ops);
        } else {
            ops->poll();
        }
        return;
    }
    if (!enabled) return;

    s->phase = USB_SESSION_STARTING;
    s->error = 0;
    ops->input(true);
    s->error = ops->start();
    if (s->error) {
        stop(s, ops); /* Includes partial-start cleanup. */
    } else {
        s->phase = USB_SESSION_RUNNING;
    }
}
