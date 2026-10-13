/* Portable lifecycle policy: no hardware, OS, HID, BLE or UI dependencies. */
#pragma once
#include <stdbool.h>
enum usb_session_phase { USB_SESSION_OFF, USB_SESSION_STARTING, USB_SESSION_RUNNING,
                         USB_SESSION_STOPPING, USB_SESSION_FAILED };
struct usb_session { enum usb_session_phase phase; int error; };
struct usb_session_ops {
    void (*input)(bool accepting);
    int (*start)(void);
    void (*stop)(void);
    void (*poll)(void);
};
/* Called only by the owner. A failure returns FAILED, requiring the owner to
 * consume its enable request. Only a fresh user request may restart it. */
void usb_session_step(struct usb_session *session, bool enabled, int fault,
                      const struct usb_session_ops *ops);
