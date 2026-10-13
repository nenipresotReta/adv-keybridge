/* SPDX-License-Identifier: MIT
 * USB service owner. Public commands and fault reports are atomic mailboxes;
 * the one owner thread alone starts, polls and stops the transport.
 */
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include "adv_usb.h"
#include "adv_usb_dwc2.h"
#include "usb/board.h"
#include "usb/diagnostics.h"
#include "usb/input.h"
#include "usb/session.h"
#include "usb/transport.h"

LOG_MODULE_REGISTER(adv_usb, LOG_LEVEL_INF);
static atomic_t requested, state, reports, media_reports, fault;
static struct usb_session session;
K_SEM_DEFINE(host_wake,0,1);
static void wake_owner(void) { k_sem_give(&host_wake); }

void adv_usb_request_toggle(void) { atomic_xor(&requested, 1); wake_owner(); }
void adv_usb_get_status(struct adv_usb_status *status) {
    status->error = atomic_get(&fault);
    status->state = status->error ? ADV_USB_ERROR : atomic_get(&state);
    status->reports = atomic_get(&reports);
    status->media_reports = atomic_get(&media_reports);
    status->previous_stage = usb_diag_previous();
}

static void publish_state(enum adv_usb_state next) { atomic_set(&state, next); }
static void count_reports(unsigned received, unsigned media) {
    atomic_add(&reports, received);
    atomic_add(&media_reports, media);
}
static void report_fault(int error) { atomic_cas(&fault, 0, error); wake_owner(); }
static const struct usb_transport_sink sink = {
    .state=publish_state, .report=count_reports,
    .input=adv_usb_submit_values, .fault=report_fault, .wake=wake_owner,
};

static int start_transport(void) {
    atomic_clear(&fault);
    atomic_clear(&reports);
    atomic_clear(&media_reports);
    publish_state(ADV_USB_STARTING);
    usb_diag_begin();
    LOG_INF("Host start: route PHY and initialize transport");
    usb_board_enter_host();
    int error = usb_transport_start(&sink);
    if (!error) {
        adv_usb_trace(ADV_USB_TRACE_READY);
        publish_state(ADV_USB_WAITING);
    }
    LOG_INF("Host start result: %d", error);
    return error;
}
static void stop_transport(void) {
    usb_diag_finish();
    usb_diag_stop_stage(1);
    LOG_INF("Host stop: transport teardown");
    usb_transport_stop();
    usb_diag_stop_stage(2);
    LOG_INF("Host stop: restore debug PHY");
    usb_board_restore_debug();
    usb_diag_stop_stage(3);
    publish_state(ADV_USB_OFF);
    LOG_INF("Host stopped: RX=%ld MEDIA=%ld error=%ld",
            (long)atomic_get(&reports), (long)atomic_get(&media_reports),
            (long)atomic_get(&fault));
}
static const struct usb_session_ops ops = {
    .input=adv_usb_input_reset, .start=start_transport,
    .stop=stop_transport, .poll=usb_transport_poll,
};

/* Repeated while running and off: Serial/JTAG can take time to reconnect, so a
 * single log at teardown would often be lost. Never log from the USB ISR. */
static void log_diagnostics(void) {
    struct usb_diag_snapshot d;
    LOG_INF("USB state=%ld requested=%ld RX=%ld MEDIA=%ld error=%ld",
            (long)atomic_get(&state), (long)atomic_get(&requested),
            (long)atomic_get(&reports), (long)atomic_get(&media_reports),
            (long)atomic_get(&fault));
    if (!usb_diag_read(&d)) return;
    LOG_INF("USB capture stage=%u stop=%u faults=%u irq=%u pending=%08x port=%08x frame=%08x",
            d.stage, d.stop_stage, d.faults, d.interrupts, d.pending, d.port, d.frame);
    LOG_INF("USB retry=%u SOF=%u", d.retries, d.sof);
    for (unsigned i=0; i<USB_DIAG_CHANNELS; i++) {
        const struct usb_diag_channel *ch = &d.channel[i];
        if (!ch->events) continue;
        LOG_INF("USB ch%u n=%u last=%08x seen=%08x char=%08x size=%08x",
                i, ch->events, ch->interrupt, ch->seen, ch->characteristics, ch->size);
    }
}

static void host_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    int64_t next_diagnostics = 0;
    for (;;) {
        int error = atomic_get(&fault);
        if (error && session.phase == USB_SESSION_RUNNING) atomic_clear(&requested);
        usb_session_step(&session, atomic_get(&requested), error, &ops);
        if (session.phase == USB_SESSION_FAILED) atomic_clear(&requested);
        if (k_uptime_get() >= next_diagnostics) {
            log_diagnostics();
            next_diagnostics = k_uptime_get() + 60000;
        }
        if (session.phase == USB_SESSION_RUNNING) {
            /* Yield only allows equal/higher priority work. Blocking also lets
             * BLE RX (priority 8) run when this priority-7 queue stays busy. */
            /* Events wake immediately; 10ms bounds TinyUSB software timers.
             * Keep the original fairness sleep under continuous traffic. */
            if (!k_sem_take(&host_wake,K_MSEC(10))) k_msleep(1);
        } else {
            (void)k_sem_take(&host_wake,K_SECONDS(1));
        }
    }
}
K_THREAD_STACK_DEFINE(host_stack, 4096);
static struct k_thread host_task;
static int start_usb(void) {
    LOG_INF("Previous USB stage: %u; Host defaults off", usb_diag_previous());
    k_thread_create(&host_task, host_stack, K_THREAD_STACK_SIZEOF(host_stack),
                    host_thread, NULL, NULL, NULL, 7, 0, K_MSEC(2000));
    k_thread_name_set(&host_task, "usb_host");
    return 0;
}
SYS_INIT(start_usb, APPLICATION, 99);
