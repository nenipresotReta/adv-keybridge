/* Compile the exact board-scoped policy injected into pinned ZMK. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ZMK_BLE_PROFILE_COUNT 5
#define BIT(n) (1u<<(n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define BT_LE_ADV_OPT_FILTER_CONN 2u
typedef unsigned bt_addr_le_t;
struct bt_le_adv_param { unsigned options; };
#define ZMK_ADV_CONN_NAME (&(struct bt_le_adv_param){1})
static struct { bt_addr_le_t peer; } profiles[5];
static unsigned active_profile,mask=31,disconnects,starts,clears,adds,last_options;
static bt_addr_le_t allowed;
static int clear_error,add_error;
static int zmk_ble_ad[1];
unsigned adv_pages_enabled_mask(void) { return mask; }
static bool zmk_ble_profile_is_open(unsigned i) { return !profiles[i].peer; }
static bool zmk_ble_active_profile_is_open(void) { return zmk_ble_profile_is_open(active_profile); }
static int bt_addr_le_cmp(const bt_addr_le_t *a,const bt_addr_le_t *b) { return *a!=*b; }
static int zmk_ble_prof_disconnect(unsigned i) { disconnects|=BIT(i);return 0; }
static bt_addr_le_t *zmk_ble_active_profile_addr(void) { return &profiles[active_profile].peer; }
static int bt_le_filter_accept_list_clear(void) { clears++;allowed=0;return clear_error; }
static int bt_le_filter_accept_list_add(const bt_addr_le_t *p) { adds++;allowed=*p;return add_error; }
static int bt_le_adv_start(const struct bt_le_adv_param *p,const void *data,unsigned n,const void *scan,unsigned count) {
    (void)data;(void)n;(void)scan;(void)count;starts++;last_options=p->options;return 0;
}
#include "../tools/ble_profile_policy.inc"
int main(void) {
    assert(adv_policy_key==UINT32_MAX);
    for(unsigned i=0;i<5;i++)profiles[i].peer=i+1;
    for(mask=1;mask<32;mask++) {
        disconnects=0;adv_disconnect_disabled();assert(disconnects==((~mask)&31));
        for(unsigned i=0;i<5;i++)assert(adv_peer_disabled(&profiles[i].peer)==!(mask&BIT(i)));
    }
    mask=3;active_profile=1;
    assert(!adv_start_filtered() && allowed==2 && (last_options&BT_LE_ADV_OPT_FILTER_CONN));
    assert(clears==1 && adds==1 && starts==1);
    clear_error=-EIO;assert(adv_start_filtered()==-EIO && starts==1);clear_error=0;
    add_error=-ENOMEM;assert(adv_start_filtered()==-ENOMEM && starts==1);add_error=0;
    profiles[1].peer=0;assert(!adv_start_filtered() && !(last_options&BT_LE_ADV_OPT_FILTER_CONN));
    bt_addr_le_t unknown=99;assert(!adv_peer_disabled(&unknown));
    mask=31;assert(!adv_peer_disabled(&profiles[4].peer));
    assert(profiles[4].peer==5); /* Disable never deletes bonds. */
    puts("PASS: all profile masks disconnect only disabled peers; reconnect allowlist; pairing discovery; filter failures fail closed");
}
