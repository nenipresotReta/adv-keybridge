/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define TU_ATTR_PACKED __attribute__((packed))
#define TU_ATTR_ALWAYS_INLINE
#define TU_VERIFY_STATIC(cond,msg) _Static_assert(cond,msg)
#define TU_BIT(n) (1u<<(n))
#define ASSERT_1(cond) do { if(!(cond))return false; } while(0)
#define ASSERT_2(cond,ret) do { if(!(cond))return ret; } while(0)
#define ASSERT_PICK(_1,_2,NAME,...) NAME
#define TU_ASSERT(...) ASSERT_PICK(__VA_ARGS__,ASSERT_2,ASSERT_1)(__VA_ARGS__)
#define CFG_TUH_DWC2_DMA_ENABLE 1
#define CFG_TUH_DWC2_ENDPOINT_MAX 16
#define TUSB_EPSIZE_BULK_FS 64
#define TUSB_EPSIZE_BULK_HS 512
#define TUSB_EPSIZE_ISO_HS_MAX 1024
#include "dwc2_type.h"
#include "adv_usb_dwc2.h"
static dwc2_regs_t regs;
static dwc2_controller_t _dwc2_controller[1];
#define DWC2_REG(port) ((void)(port), &regs)
static uint32_t cycle;
static unsigned faults, last_fault, last_trace;
static bool simulate_reset;
uint32_t k_cycle_get_32(void) {
    cycle++;
    if(simulate_reset && (cycle%3)==0)
        regs.grstctl=(regs.grstctl & ~(GRSTCTL_CSRST|GRSTCTL_TXFFLSH|GRSTCTL_RXFFLSH))|GRSTCTL_AHBIDL;
    return cycle;
}
void adv_usb_hw_timeout(unsigned stage) { faults++;last_fault=stage; }
void adv_usb_trace(unsigned stage) { last_trace=stage; }
static uint32_t tu_u32_from_u16(uint16_t high,uint16_t low) { return ((uint32_t)high<<16)|low; }
static void tu_memclr(void *p,size_t size) { memset(p,0,size); }
enum { TUSB_SPEED_HIGH, TUSB_SPEED_AUTO, TUSB_SPEED_FULL };
typedef struct { unsigned role, speed; } tusb_rhport_init_t;
static struct { bool use_hs_phy; } _tuh_cfg;
typedef struct { unsigned allocated, ep_id, closing, result, xferred_bytes; } hcd_xfer_t;
typedef struct { unsigned unused; } hcd_endpoint_t;
typedef unsigned xfer_result_t;
static struct { hcd_xfer_t xfer[8]; hcd_endpoint_t edpt[16]; } _hcd_data;
enum { TUSB_DIR_OUT, TUSB_DIR_IN };
static unsigned dma_faults, dma_handlers, completions, observations, observed_irq;
void adv_usb_dma_error(void) { dma_faults++; }
void adv_usb_diag_channel(unsigned ch,uint32_t irq,uint32_t chr,uint32_t size) {
    assert(ch<8 && chr==regs.channel[ch].hcchar && size==regs.channel[ch].hctsiz);
    observations++;observed_irq=irq;
}
static bool tu_bit_test(uint32_t v,unsigned b) { return (v&(1u<<b))!=0; }
static uint8_t dwc2_channel_count(const dwc2_regs_t *p) { (void)p;return 8; }
static bool handle_channel_in_dma(dwc2_regs_t *p,uint8_t ch,uint32_t irq) {
    (void)p;(void)ch;(void)irq;dma_handlers++;return false;
}
static bool handle_channel_out_dma(dwc2_regs_t *p,uint8_t ch,uint32_t irq) {
    return handle_channel_in_dma(p,ch,irq);
}
static uintptr_t tu_align4(uintptr_t v) { return v&~(uintptr_t)3; }
static void hcd_dcache_invalidate(void *p,unsigned n) { (void)p;(void)n; }
static void edpt_dealloc(hcd_endpoint_t *ep) { (void)ep; }
static uint8_t tu_edpt_addr(unsigned ep,unsigned dir) { return ep|(dir<<7); }
static void hcd_event_xfer_complete(unsigned dev,unsigned ep,unsigned n,xfer_result_t result,bool isr) {
    (void)dev;(void)ep;(void)n;(void)result;(void)isr;completions++;
}
static void channel_dealloc(dwc2_regs_t *p,unsigned ch) { (void)p;_hcd_data.xfer[ch].allocated=0; }
static void dwc2_clock_init(uint8_t port,unsigned role) { (void)port;(void)role; }
static bool dwc2_core_is_highspeed_phy(dwc2_regs_t *p,bool hs) { (void)p;return hs; }
static bool dwc2_core_init(uint8_t port,bool hs,bool dma) { (void)port;(void)hs;(void)dma;return true; }
static void dwc2_core_deinit(uint8_t port) { (void)port; regs.pcgcctl=PCGCCTL_STOPPCLK|PCGCCTL_GATEHCLK; }
enum { HPRT_W1_MASK=HPRT_CONN_DETECT|HPRT_ENABLE|HPRT_ENABLE_CHANGE|HPRT_OVER_CURRENT_CHANGE|HPRT_SUSPEND };
#include "production.inc"

static bool disjoint(void) {
    unsigned rx_end=regs.grxfsiz;
    unsigned np_start=regs.gnptxfsiz&0xffff, np_end=np_start+(regs.gnptxfsiz>>16);
    unsigned p_start=regs.hptxfsiz&0xffff, p_end=p_start+(regs.hptxfsiz>>16);
    unsigned fifo_end=regs.gdfifocfg&0xffff;
    return np_start>=rx_end && p_start>=rx_end &&
        (np_end<=p_start || p_end<=np_start) && np_end<=fifo_end && p_end<=fifo_end;
}
static void fifo(void) {
    _dwc2_controller[0].otg_dfifo_depth=256;
    regs.ghwcfg2=(7u<<14)|(GHWCFG2_ARCH_INTERNAL_DMA<<3);
    legacy_fifo_init(0,false);
    assert(!disjoint()); // reproduces the production 0.1.7 bug
    dfifo_host_init(0,false);
    assert(disjoint());
    assert((regs.gdfifocfg&0xffff)==248); // all eight DMA channel words reserved
    assert(regs.grxfsiz==140);
    assert((regs.hptxfsiz&0xffff)==140);
    assert((regs.gnptxfsiz&0xffff)==216);
    for(unsigned channels=1;channels<=16;channels++) {
        regs.ghwcfg2=((channels-1)<<14)|(GHWCFG2_ARCH_INTERNAL_DMA<<3);
        dfifo_host_init(0,false);assert(disjoint());
        assert((regs.gdfifocfg&0xffff)==256-channels);
        regs.ghwcfg2=(channels-1)<<14;dfifo_host_init(0,false);assert(disjoint());
        assert((regs.gdfifocfg&0xffff)==256);
    }
    _dwc2_controller[0].otg_dfifo_depth=2048;
    regs.ghwcfg2=(7u<<14)|(GHWCFG2_ARCH_INTERNAL_DMA<<3);
    dfifo_host_init(0,true);assert(disjoint());
}
static void waits(void) {
    volatile uint32_t value=1;
    faults=0;cycle=UINT32_MAX-10;
    assert(adv_usb_wait_bits(&value,1,1,20,ADV_USB_TRACE_HOST_MODE));
    assert(!adv_usb_wait_bits(&value,1,0,20,ADV_USB_TRACE_HOST_MODE));
    assert(faults==1 && last_fault==ADV_USB_TRACE_HOST_MODE);
    value=0x10000;assert(adv_usb_wait_queue(&value,true));assert(adv_usb_wait_queue(&value,false));
    value=0x800000;assert(adv_usb_wait_queue(&value,false));
    assert(!adv_usb_wait_queue(&value,true)); // PTX terminate bit is not queue capacity
    value=0;assert(!adv_usb_wait_queue(&value,false));assert(last_fault==ADV_USB_TRACE_QUEUE);
    regs.grstctl=0;simulate_reset=false;
    assert(!reset_core(&regs));assert(last_fault==ADV_USB_TRACE_RESET_IDLE);
    regs.grstctl=GRSTCTL_AHBIDL;regs.gsnpsid=DWC2_CORE_REV_2_92a;
    assert(!reset_core(&regs));assert(last_fault==ADV_USB_TRACE_RESET_DONE);
    regs.grstctl=GRSTCTL_AHBIDL;simulate_reset=true;
    assert(reset_core(&regs));assert(last_trace==ADV_USB_TRACE_RESET_FINAL);
    assert(dfifo_flush_tx(&regs,0x10));assert(dfifo_flush_rx(&regs));
    simulate_reset=false;
    assert(!dfifo_flush_tx(&regs,0x10));assert(last_fault==ADV_USB_TRACE_TX_FLUSH);
    assert(!dfifo_flush_rx(&regs));assert(last_fault==ADV_USB_TRACE_RX_FLUSH);
}
static void lifecycle(void) {
    memset(&regs,0,sizeof(regs));_dwc2_controller[0].otg_dfifo_depth=256;
    regs.ghwcfg2=(7u<<14)|(GHWCFG2_ARCH_INTERNAL_DMA<<3);
    regs.gsnpsid=DWC2_CORE_REV_2_92a;regs.gintsts=GINTSTS_CMODE_HOST;
    tusb_rhport_init_t init={.role=1,.speed=TUSB_SPEED_FULL};
    assert(hcd_init(0,&init));assert(disjoint());
    assert(!(regs.gintmsk&GINTSTS_OTGINT));
    assert(regs.gintmsk==(GINTSTS_HPRTINT|GINTSTS_HCINT|GINTSTS_DISCINT));
    memset(&_hcd_data,0x55,sizeof(_hcd_data));
    assert(hcd_deinit(0));
    for(unsigned i=0;i<8;i++)assert(_hcd_data.xfer[i].allocated==0);
    regs.gintsts=0;assert(!hcd_init(0,&init));assert(last_fault==ADV_USB_TRACE_HOST_MODE);
}
static void dma_error_dispatch(void) {
    memset(&regs,0,sizeof(regs));memset(&_hcd_data,0,sizeof(_hcd_data));
    regs.ghwcfg2=(7u<<14)|(GHWCFG2_ARCH_INTERNAL_DMA<<3);
    regs.haint=1;
    for(unsigned direction=0;direction<2;direction++) {
        regs.channel[0].hcchar=direction<<15;
        regs.channel[0].hcint=HCINT_HALTED|HCINT_AHB_ERR;
        unsigned before=dma_faults;
        handle_channel_irq(0,true);
        assert(dma_faults==before+1 && dma_handlers==direction && !completions);
        assert(observed_irq==(HCINT_HALTED|HCINT_AHB_ERR));
        regs.channel[0].hcint=HCINT_HALTED|HCINT_NAK;
        handle_channel_irq(0,true);
        assert(dma_handlers==direction+1 && dma_faults==before+1);
    }
    assert(observations==4);
}
int main(void) {
    fifo();waits();lifecycle();dma_error_dispatch();
    puts("PASS: actual DWC2 FIFO registers (legacy overlap reproduced), DMA reserve, bounded waits/wrap, IRQ mask, shutdown cleanup");
}
