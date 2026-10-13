#include <assert.h>
#include <stdio.h>
#define TEST_CLOCK
#include "../src/screen.c"
int64_t test_clock=100;

const struct device test_lcd={0};
static struct k_work_q lowprio;
static bool active, ready=true, blank;
static int pwm_error, blank_error, save_error;
static unsigned pulse, scheduled, writes, persisted;
bool device_is_ready(const struct device *d) { (void)d;return ready; }
bool pwm_is_ready_dt(const struct pwm_dt_spec *p) { (void)p;return ready; }
int pwm_set_pulse_dt(const struct pwm_dt_spec *p,uint32_t n) {
    assert(n<=p->period);
    if(pwm_error)return pwm_error;
    pulse=n;return 0;
}
int display_blanking_on(const struct device *d) { (void)d;if(blank_error)return blank_error;blank=true;return 0; }
int display_blanking_off(const struct device *d) { (void)d;if(blank_error)return blank_error;blank=false;return 0; }
bool zmk_keymap_layer_active(unsigned n) { assert(n==2);return active; }
int zmk_keymap_layer_activate(unsigned n,bool lock) { assert(n==2 && !lock);active=true;return 0; }
int zmk_keymap_layer_deactivate(unsigned n,bool lock) { assert(n==2 && !lock);active=false;return 0; }
struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &lowprio; }
int k_work_reschedule_for_queue(struct k_work_q *q,struct k_work_delayable *w,int delay) {
    assert(q==&lowprio && w==&save_work && delay==2000);scheduled++;return 0;
}
int settings_save_one(const char *name,const void *p,size_t n) {
    assert(!strcmp(name,"adv_screen/brightness") && n==1);
    persisted=*(const uint8_t *)p;writes++;return save_error;
}
static int read_value(void *p,void *buffer,size_t size) {
    assert(size==1);*(uint8_t *)buffer=*(uint8_t *)p;return 1;
}
static void action(unsigned n) { assert(!adv_screen_request(n));adv_screen_tick(); }
int main(void) {
    assert(!adv_screen_init() && pulse==1000000 && !blank);
    assert(brightness_pulse(0)==0); /* Off must remain distinct from dim. */
    unsigned previous=0;
    for(unsigned level=10;level<=100;level+=10) {
        unsigned next=brightness_pulse(level);
        assert(next>previous && next>600000 && next<=1000000);
        previous=next;
    }
    assert(state.page==ADV_PAGE_KEYBOARD && !state.asleep);
    assert(adv_screen_menu_action(&state)==-1);
    assert(!adv_screen_request(ADV_SCREEN_OPEN) && active); /* Isolation before UI tick. */
    atomic_set(&loaded_brightness,100); /* Preference restoration cannot undo queued page isolation. */
    adv_screen_tick();assert(state.page==ADV_PAGE_SETTINGS);
    assert(active);
    assert(adv_screen_menu_action(&state)==ADV_SCREEN_BACK);
    for(unsigned n=0;n<100;n++)action(ADV_SCREEN_DIM);
    assert(state.brightness==10 && pulse==662745 && writes==0);
    save_brightness(NULL);assert(writes==1 && persisted==10);
    for(unsigned n=0;n<100;n++)action(ADV_SCREEN_BRIGHTEN);
    assert(state.brightness==100 && pulse==1000000);
    action(ADV_SCREEN_DIM);assert(state.brightness==90);
    action(ADV_SCREEN_NEXT);action(ADV_SCREEN_BRIGHTEN);assert(state.brightness==90);
    action(ADV_SCREEN_SELECT);
    assert(state.asleep && blank && pulse==0 && !active && state.page==ADV_PAGE_KEYBOARD);
    assert(adv_screen_menu_action(&state)==ADV_SCREEN_WAKE);
    action(ADV_SCREEN_WAKE);assert(!state.asleep && !blank && pulse==960784 && !active);
    action(ADV_SCREEN_OPEN);action(ADV_SCREEN_PREVIOUS);assert(state.selected==2);
    action(ADV_SCREEN_SELECT);assert(!active && state.page==ADV_PAGE_KEYBOARD);
    action(ADV_SCREEN_OPEN);action(ADV_SCREEN_BACK);assert(!active);
    assert(!adv_screen_request(ADV_SCREEN_WAKE));
    assert(!adv_screen_request(ADV_SCREEN_OPEN));
    adv_screen_tick();assert(active && state.page==ADV_PAGE_SETTINGS);
    action(ADV_SCREEN_BACK);
    uint8_t restored=40;
    assert(!load_brightness("brightness",1,read_value,&restored));
    adv_screen_tick();assert(state.brightness==40 && pulse==776470);
    unsigned old_scheduled=scheduled;
    adv_screen_tick();assert(scheduled==old_scheduled); /* No save on restore. */
    restored=0;assert(load_brightness("brightness",1,read_value,&restored)==-EINVAL);
    restored=25;assert(load_brightness("brightness",1,read_value,&restored)==-EINVAL);
    assert(load_brightness("brightness",2,read_value,&restored)==-EINVAL);
    action(ADV_SCREEN_OPEN);pwm_error=-EIO;action(ADV_SCREEN_DIM);
    assert(state.brightness==40 && screen_error==-EIO);
    action(ADV_SCREEN_NEXT);assert(screen_error==-EIO); /* Navigation cannot clear hardware fault. */
    pwm_error=0;blank_error=-EIO;action(ADV_SCREEN_SELECT);
    assert(!state.asleep && pulse==776470 && !blank && screen_error==-EIO);
    blank_error=0;
    k_msgq_purge(&screen_commands);active=false;
    unsigned value=ADV_SCREEN_NEXT;
    for(unsigned n=0;n<16;n++)assert(!k_msgq_put(&screen_commands,&value,K_NO_WAIT));
    assert(adv_screen_request(ADV_SCREEN_OPEN)==-ENOBUFS && !active);
    k_msgq_purge(&screen_commands);
    assert(adv_screen_request(99)==-EINVAL);
    save_error=-EIO;save_brightness(NULL);
    struct adv_screen_state snapshot;int error;
    adv_screen_get_state(&snapshot,&error);assert(error==-EIO);
    screen_error=0;save_error=0;
    action(ADV_SCREEN_WAKE);
    test_clock+=59999;adv_screen_tick();assert(!state.asleep);
    adv_screen_activity();test_clock+=59999;adv_screen_tick();assert(!state.asleep);
    test_clock++;adv_screen_tick();assert(state.asleep && blank && !active);
    adv_screen_activity();adv_screen_tick();assert(state.asleep); /* Typing stays dark. */
    action(ADV_SCREEN_WAKE);assert(!state.asleep);
    action(ADV_SCREEN_OPEN);test_clock+=60000;adv_screen_tick();
    assert(state.asleep && !active && state.page==ADV_PAGE_KEYBOARD);
    test_clock=UINT32_MAX-100;action(ADV_SCREEN_WAKE);
    test_clock+=60000;adv_screen_tick();assert(state.asleep); /* Clock wrap. */
    puts("PASS: production settings isolation, PWM limits/off/wake, persistence, bounded queue and hardware failure rollback");
}
