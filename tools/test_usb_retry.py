#!/usr/bin/env python3
"""Exercise production NAK handler -> SOF -> fresh DMA channel, fake MMIO only."""
from pathlib import Path
import sys,re,tempfile,subprocess
root=Path(__file__).resolve().parents[1]
dwc=Path(sys.argv[1])/'src/portable/synopsys/dwc2'
s=(dwc/'hcd_dwc2.c').read_text()
def function(name):
 m=re.search(r'[^\n]*\b'+name+r'\([^;]*?\)\s*\{',s);assert m,name
 end=m.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
types=s[s.index('typedef struct {'):s.index('static tuh_configure_dwc2_t')]
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#define TU_ATTR_PACKED __attribute__((packed))
#define TU_ATTR_ALWAYS_INLINE
#define TU_VERIFY_STATIC(c,m) _Static_assert(c,m)
#define TU_BIT(n) (1u<<(n))
#define TU_ASSERT(c,...) do { if(!(c)) return false; } while(0)
#define TU_BREAKPOINT() assert(0)
#define CFG_TUH_DWC2_DMA_ENABLE 1
#define CFG_TUH_DWC2_ENDPOINT_MAX 16
#define DWC2_CHANNEL_COUNT_MAX 16
#define HCD_XFER_PERIOD_SPLIT_NYET_MAX 3
#define HCD_XFER_ERROR_MAX 3
#define TUSB_INDEX_INVALID_8 255
#define TUSB_XFER_INTERRUPT 3
#define TUSB_DIR_IN 1
#define TUSB_DIR_OUT 0
typedef enum {TUSB_SPEED_FULL,TUSB_SPEED_LOW,TUSB_SPEED_HIGH,TUSB_SPEED_INVALID} tusb_speed_t;
enum {XFER_RESULT_SUCCESS,XFER_RESULT_FAILED,XFER_RESULT_STALLED,XFER_RESULT_INVALID};
#include "dwc2_type.h"
static dwc2_regs_t regs;
#define DWC2_REG(port) ((void)(port),&regs)
static unsigned retries,sofs;
void adv_usb_diag_retry(void) { retries++; }
void adv_usb_diag_sof(void) { sofs++; }
static void tu_memclr(void *p,size_t n) { memset(p,0,n); }
static unsigned tu_div_ceil(unsigned n,unsigned d) {return (n+d-1)/d;}
static uint8_t tu_min8(uint8_t a,uint8_t b) {return a<b?a:b;}
static uint32_t tu_min32(uint32_t a,uint32_t b) {return a<b?a:b;}
static bool adv_usb_wait_queue(const volatile uint32_t *p,bool periodic) {(void)p;(void)periodic;return true;}
static void hcd_dcache_clean(void *p,unsigned n) {(void)p;(void)n;}
static bool channel_disable(dwc2_regs_t *p,dwc2_channel_t *ch) {(void)p;(void)ch;assert(0);return false;}
'''
names=['dwc2_channel_count','hprt_speed_get','dma_host_enabled','channel_alloc','channel_is_periodic','channel_dealloc','channel_send_in_token','cal_packet_count','cal_next_pid','channel_xfer_start','edpt_xfer_kickoff','channel_xfer_in_retry','handle_channel_in_dma','handle_sof_irq']
main=r'''
int main(void) {
 for(unsigned interval=1;interval<=8;interval++) {
  memset(&regs,0,sizeof(regs));memset(&_hcd_data,0,sizeof(_hcd_data));
  regs.ghwcfg2=(7u<<14)|(GHWCFG2_ARCH_INTERNAL_DMA<<3);
  regs.hprt=HPRT_SPEED_FULL<<17;
  hcd_endpoint_t *ep=&_hcd_data.edpt[0];
  ep->hcchar_bm.enable=1;ep->hcchar_bm.ep_type=TUSB_XFER_INTERRUPT;
  ep->hcchar_bm.ep_dir=1;ep->hcchar_bm.ep_num=1;ep->hcchar_bm.ep_size=8;
  ep->uframe_interval=interval*8;ep->buflen=8;ep->speed=TUSB_SPEED_FULL;
  assert(edpt_xfer_kickoff(&regs,0));
  for(unsigned n=0;n<10000;n++) {
   dwc2_channel_t *ch=&regs.channel[0];
   // Hardware halted, payload not consumed; emulate its residual status.
   ch->hcchar &= ~HCCHAR_CHENA;
   ch->hctsiz=0x80080008u; // Same residual value observed on the device.
   assert(!handle_channel_in_dma(&regs,0,HCINT_NAK|HCINT_HALTED));
   assert(!_hcd_data.xfer[0].allocated && ep->uframe_countdown>0);
   assert(regs.gintmsk&GINTMSK_SOFM);
   unsigned frames=0;
   while(ep->uframe_countdown) { assert(++frames<=8);regs.hfnum++;handle_sof_irq(0,true); }
   assert(_hcd_data.xfer[0].allocated && (ch->hcchar&HCCHAR_CHENA));
   assert(ch->hctsiz==0x00080008u); // Fresh size/PID; no stale DOPNG bit.
   assert(!_hcd_data.xfer[0].xferred_bytes && ep->buflen==8);
  }
  regs.channel[0].hctsiz=0; // Simulate a complete 8-byte report after repeated NAKs.
  assert(handle_channel_in_dma(&regs,0,HCINT_XFER_COMPLETE|HCINT_HALTED));
  assert(_hcd_data.xfer[0].xferred_bytes==8 && _hcd_data.xfer[0].result==XFER_RESULT_SUCCESS);
 }
 assert(retries==80000 && sofs>=retries);
 puts("PASS: actual DMA NAK -> bounded SOF -> fresh channel path, 80000 retries across intervals 1..8, completion after NAK");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+types+'\n'.join(function(n) for n in names)+main)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-pointer-to-int-cast','-fsanitize=address,undefined','-I'+str(dwc),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
