"""Reviewed, reversible edits to the pinned TinyUSB DWC2 implementation."""
HCD_SHA = "3f8291ddcb47f85527f473126aeb84e6f6fcce7ae2f8cfce15fb686d8f5aaad9"
COMMON_SHA = "3971bcb1b0fd639dad056dab97a2671aa430a5892347938427a11d864679559b"
HEADER_SHA = "1bbfe05848a9de8275fc8650176c847a89f5b5d550556f7f47085e1ce31af637"

HCD_EDITS = [
    ('    const dwc2_channel_split_t hcsplt = {.value = channel->hcsplt};\n    // retry immediately for periodic split NYET',
     '    const dwc2_channel_split_t hcsplt = {.value = channel->hcsplt};\n    // ADV: a direct FS/LS interrupt IN NAK is not a completed report.\n    // Rebuild the DMA channel on a later SOF instead of re-enabling its\n    // halted register state in the same ISR. Preserve the unconsumed PID.\n    if (dma_host_enabled(dwc2) && !hcsplt.split_en &&\n        hcchar.ep_type == TUSB_XFER_INTERRUPT && (hcint & HCINT_NAK) &&\n        hprt_speed_get(dwc2) != TUSB_SPEED_HIGH) {\n      const dwc2_channel_tsize_t remaining = {.value = channel->hctsiz};\n      edpt->next_pid = remaining.pid;\n      edpt->uframe_countdown = edpt->uframe_interval > 8 ? edpt->uframe_interval - 8 : 8;\n      adv_usb_diag_retry();\n      if (!(dwc2->gintmsk & GINTMSK_SOFM)) dwc2->gintsts = GINTSTS_SOF;\n      dwc2->gintmsk |= GINTMSK_SOFM;\n      channel_dealloc(dwc2, ch_id);\n      return;\n    }\n    // retry immediately for periodic split NYET'),
    ("  bool more_isr = false;", "  bool more_isr = false;\n  adv_usb_diag_sof();"),
    ("  const uint32_t gintsts = dwc2->gintsts & gintmsk;",
     "  const uint32_t gintsts = dwc2->gintsts & gintmsk;\n"
     "  adv_usb_diag_bus(gintsts, dwc2->hprt, dwc2->hfnum);"),
    ("      const uint32_t hcint = channel->hcint;\n      channel->hcint = hcint; // clear interrupt",
     "      const uint32_t hcint = channel->hcint;\n"
     "      adv_usb_diag_channel(ch_id, hcint, channel->hcchar, channel->hctsiz);\n"
     "      channel->hcint = hcint; // clear interrupt\n"
     "      // The DMA handlers do not complete AHB errors. Stop instead of\n"
     "      // leaving an allocated, halted endpoint waiting forever.\n"
     "      if (is_dma && (hcint & HCINT_AHB_ERR)) { adv_usb_dma_error(); return; }"),
    ("      hcd_event_device_attach(rhport, in_isr);",
     "      adv_usb_trace(ADV_USB_TRACE_ATTACH);\n      hcd_event_device_attach(rhport, in_isr);"),
    ("    dfifo_top -= ghwcfg2.num_host_ch;",
     "    dfifo_top -= ghwcfg2.num_host_ch + 1; // field encodes channels minus one"),
    ("  uint16_t rxfsiz = 2 * (ptx_largest + 2) + ghwcfg2.num_host_ch;",
     "  uint16_t rxfsiz = 2 * (ptx_largest + 2) + ghwcfg2.num_host_ch + 1;"),
    ("  dfifo_top -= rxfsiz;\n  dwc2->grxfsiz = rxfsiz;",
     "  // RX always begins at zero. TX allocations descend from the usable top.\n"
     "  dwc2->grxfsiz = rxfsiz;"),
    ("  while ((dwc2->gintsts & GINTSTS_CMOD) != GINTSTS_CMODE_HOST) {}",
     "  adv_usb_trace(ADV_USB_TRACE_HOST_MODE);\n"
     "  TU_ASSERT(adv_usb_wait_bits(&dwc2->gintsts, GINTSTS_CMOD, GINTSTS_CMODE_HOST,\n"
     "                              100000, ADV_USB_TRACE_HOST_MODE));"),
    ("  dfifo_host_init(rhport, is_hs_phy);",
     "  adv_usb_trace(ADV_USB_TRACE_FIFO);\n  dfifo_host_init(rhport, is_hs_phy);"),
    ("  dwc2->gintmsk |= GINTSTS_OTGINT | GINTSTS_HPRTINT | GINTSTS_HCINT | GINTSTS_DISCINT;",
     "  // Forced host needs port/channel/disconnect IRQs. OTGINT has no handler here.\n"
     "  dwc2->gintmsk |= GINTSTS_HPRTINT | GINTSTS_HCINT | GINTSTS_DISCINT;"),
    ("  dwc2_core_deinit(rhport);\n  return true;",
     "  dwc2_core_deinit(rhport);\n"
     "  // tuh_deinit closes devices after resetting/gating the controller. They\n"
     "  // must not try to halt stale channels or wait on the now gated hardware.\n"
     "  tu_memclr(&_hcd_data, sizeof(_hcd_data));\n  return true;"),
    ("    while (0 == req_queue_avail(dwc2, is_period)) {\n"
     "      // blocking wait for request queue available\n    }",
     "    const volatile uint32_t *status = is_period ? &dwc2->hptxsts : &dwc2->hnptxsts;\n"
     "    if (!adv_usb_wait_queue(status, is_period)) return false;"),
    ("  while (0 == req_queue_avail(dwc2, channel_is_periodic(channel->hcchar))) {\n"
     "    // blocking wait for request queue available\n  }",
     "  const bool periodic = channel_is_periodic(channel->hcchar);\n"
     "  const volatile uint32_t *status = periodic ? &dwc2->hptxsts : &dwc2->hnptxsts;\n"
     "  if (!adv_usb_wait_queue(status, periodic)) return false;"),
]

COMMON_EDITS = [
    ("static void reset_core(dwc2_regs_t* dwc2) {",
     "static bool reset_core(dwc2_regs_t* dwc2) {"),
    ("  while (!(dwc2->grstctl & GRSTCTL_AHBIDL)) {\n  }",
     "  adv_usb_trace(ADV_USB_TRACE_RESET_IDLE);\n"
     "  if (!adv_usb_wait_bits(&dwc2->grstctl, GRSTCTL_AHBIDL, GRSTCTL_AHBIDL,\n"
     "                         100000, ADV_USB_TRACE_RESET_IDLE)) return false;"),
    ("    while (dwc2->grstctl & GRSTCTL_CSRST) {}",
     "    adv_usb_trace(ADV_USB_TRACE_RESET_DONE);\n"
     "    if (!adv_usb_wait_bits(&dwc2->grstctl, GRSTCTL_CSRST, 0,\n"
     "                           100000, ADV_USB_TRACE_RESET_DONE)) return false;"),
    ("    while (!(dwc2->grstctl & GRSTCTL_CSRST_DONE)) {}",
     "    adv_usb_trace(ADV_USB_TRACE_RESET_DONE);\n"
     "    if (!adv_usb_wait_bits(&dwc2->grstctl, GRSTCTL_CSRST_DONE, GRSTCTL_CSRST_DONE,\n"
     "                           100000, ADV_USB_TRACE_RESET_DONE)) return false;"),
    ("  while (!(dwc2->grstctl & GRSTCTL_AHBIDL)) {} // wait for AHB master IDLE\n}",
     "  adv_usb_trace(ADV_USB_TRACE_RESET_FINAL);\n"
     "  return adv_usb_wait_bits(&dwc2->grstctl, GRSTCTL_AHBIDL, GRSTCTL_AHBIDL,\n"
     "                           100000, ADV_USB_TRACE_RESET_FINAL);\n}"),
    ("static void phy_fs_init(dwc2_regs_t* dwc2) {", "static bool phy_fs_init(dwc2_regs_t* dwc2) {"),
    ("static void phy_hs_init(dwc2_regs_t* dwc2) {", "static bool phy_hs_init(dwc2_regs_t* dwc2) {"),
    ("  // Reset core after selecting PHY\n  reset_core(dwc2);\n\n  // USB turnaround",
     "  // Reset core after selecting PHY\n  if (!reset_core(dwc2)) return false;\n\n  // USB turnaround"),
    ("  // Reset core after selecting PHY\n  reset_core(dwc2);\n\n  // Set turn-around",
     "  // Reset core after selecting PHY\n  if (!reset_core(dwc2)) return false;\n\n  // Set turn-around"),
    ("  dwc2_phy_update(dwc2, GHWCFG2_HSPHY_NOT_SUPPORTED);\n}",
     "  dwc2_phy_update(dwc2, GHWCFG2_HSPHY_NOT_SUPPORTED);\n  return true;\n}"),
    ("  dwc2_phy_update(dwc2, ghwcfg2.hs_phy_type);\n}",
     "  dwc2_phy_update(dwc2, ghwcfg2.hs_phy_type);\n  return true;\n}"),
    ("  // disable global interrupt\n  dwc2->gahbcfg &= ~GAHBCFG_GINT;\n\n  if (is_hs_phy) {\n"
     "    phy_hs_init(dwc2);\n  } else {\n    phy_fs_init(dwc2);\n  }",
     "  // Ungate retained clocks before PHY reset, including a second Host session.\n"
     "  dwc2->pcgcctl &= ~(PCGCCTL_STOPPCLK | PCGCCTL_GATEHCLK | PCGCCTL_PWRCLMP | PCGCCTL_RSTPDWNMODULE);\n"
     "  dwc2->gahbcfg &= ~GAHBCFG_GINT;\n\n"
     "  TU_ASSERT(is_hs_phy ? phy_hs_init(dwc2) : phy_fs_init(dwc2));"),
    ("  dfifo_flush_tx(dwc2, 0x10); // all tx fifo\n  dfifo_flush_rx(dwc2);",
     "  TU_ASSERT(dfifo_flush_tx(dwc2, 0x10)); // all tx fifo\n  TU_ASSERT(dfifo_flush_rx(dwc2));"),
    ("  reset_core(dwc2);\n\n  // Stop PHY clock",
     "  (void) reset_core(dwc2); // bounded; the caller will restore the debug PHY\n\n  // Stop PHY clock"),
]

HEADER_EDITS = [
    ('#include "dwc2_type.h"', '#include "dwc2_type.h"\n#include "adv_usb_dwc2.h"'),
    ("static inline void dfifo_flush_tx", "static inline bool dfifo_flush_tx"),
    ("  while (0 != (dwc2->grstctl & GRSTCTL_TXFFLSH_Msk)) {}",
     "  adv_usb_trace(ADV_USB_TRACE_TX_FLUSH);\n"
     "  return adv_usb_wait_bits(&dwc2->grstctl, GRSTCTL_TXFFLSH_Msk, 0,\n"
     "                           100000, ADV_USB_TRACE_TX_FLUSH);"),
    ("static inline void dfifo_flush_rx", "static inline bool dfifo_flush_rx"),
    ("  while (0 != (dwc2->grstctl & GRSTCTL_RXFFLSH_Msk)) {}",
     "  adv_usb_trace(ADV_USB_TRACE_RX_FLUSH);\n"
     "  return adv_usb_wait_bits(&dwc2->grstctl, GRSTCTL_RXFFLSH_Msk, 0,\n"
     "                           100000, ADV_USB_TRACE_RX_FLUSH);"),
]
