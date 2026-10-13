#!/usr/bin/env python3
"""Compile actual TinyUSB IRQ adapter plus pinned Zephyr enable/disable functions."""
from pathlib import Path
import sys, re, hashlib, tempfile, subprocess
root=Path(__file__).resolve().parents[1]
driver=root/'tests/fixtures/intc_esp32.c'
assert hashlib.sha256(driver.read_bytes()).hexdigest()=='e4b8e7d00d11c272553bc07f12bb7477231c6e7fda93e33e85f36ca956df6fbc'
def function(text,name):
 m=re.search(r'[^\n]*\b'+name+r'\([^;]*?\)\s*\{',text);assert m,name
 end=m.end();depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define IRAM_ATTR
#define TU_ATTR_ALWAYS_INLINE
#define ESP_INTR_FLAG_LEVEL1 1
#define INT_MUX_DISABLED_INTNO 6
#define TUSB_ROLE_HOST 1
typedef unsigned tusb_role_t;
struct shared_vector_desc_t { struct shared_vector_desc_t *next; int disabled,source; };
struct vector_desc_t { unsigned flags; int cpu,intno,source; struct shared_vector_desc_t *shared_vec_info; };
struct intr_handle_data_t { struct vector_desc_t *vector_desc; struct shared_vector_desc_t *shared_vector_desc; };
static int esp_intc_csec, irq_masked, routed, fault, alloc_error;
static unsigned allocations;
static struct vector_desc_t vector={.cpu=0,.intno=2,.source=17};
static struct intr_handle_data_t handle={.vector_desc=&vector};
static struct intr_handle_data_t *usb_ih[1];
static const struct {int irqnum;} _dwc2_controller[]={{17}};
static int irq_lock(void) { int key=irq_masked;irq_masked=1;return key; }
static void irq_unlock(int key) { irq_masked=key; }
static void irq_disable(int n) { (void)n; }
static void irq_enable(int n) { (void)n; }
static int esp_cpu_get_core_id(void) { return 0; }
static void esp_rom_route_intr_matrix(int cpu,int source,int target) { (void)cpu;(void)source;routed=target; }
static void dwc2_int_handler_wrap(void *p) { (void)p; }
static unsigned tu_u16(unsigned high,unsigned low) { return (high<<8)|low; }
static void adv_usb_irq_error(int error) { fault=error; }
static int esp_intr_alloc(int source,int flags,void (*fn)(void *),void *arg,struct intr_handle_data_t **out) {
 assert(source==17 && flags==1 && fn==dwc2_int_handler_wrap && (uintptr_t)arg==256);
 allocations++;if(alloc_error)return alloc_error;*out=&handle;routed=2;return 0;
}
'''
code+='\n'.join(function(driver.read_text(),n) for n in ['esp_intr_lock','esp_intr_unlock','esp_intr_enable','esp_intr_disable'])
code+='\n'+function((Path(sys.argv[1])/'src/portable/synopsys/dwc2/dwc2_esp32.h').read_text(),'dwc2_int_set')
code+=r'''
int main(void) {
 dwc2_int_set(0,TUSB_ROLE_HOST,false);assert(!allocations && !irq_masked);
 alloc_error=-ENOMEM;dwc2_int_set(0,TUSB_ROLE_HOST,true);
 assert(fault==-ENOMEM && !usb_ih[0]);alloc_error=fault=0;allocations=0;
 for(unsigned i=0;i<10000;i++) {
  dwc2_int_set(0,TUSB_ROLE_HOST,true);assert(!irq_masked && routed==2 && !fault);
  dwc2_int_set(0,TUSB_ROLE_HOST,false);assert(!irq_masked && routed==6 && !fault);
 }
 assert(allocations==1 && usb_ih[0]==&handle);
 irq_masked=1;dwc2_int_set(0,TUSB_ROLE_HOST,true);assert(irq_masked);
 dwc2_int_set(0,TUSB_ROLE_HOST,false);assert(irq_masked);irq_masked=0;
 vector.source=-1;vector.cpu=1;
 dwc2_int_set(0,TUSB_ROLE_HOST,true);assert(fault==-EINVAL && !irq_masked);fault=0;
 dwc2_int_set(0,TUSB_ROLE_HOST,false);assert(fault==-EINVAL && !irq_masked);
 puts("PASS: actual IRQ adapter + pinned driver: 10000 toggles, one allocation, IRQ state preserved, alloc/enable/disable error propagation");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'irq.c').write_text(code)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'irq.c'),'-o',str(p/'irq')],check=True)
 subprocess.run([str(p/'irq')],check=True)
