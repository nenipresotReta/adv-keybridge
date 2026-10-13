/* Cardputer policy: called from the advertising work queue, never RX callbacks. */
extern unsigned adv_pages_enabled_mask(void);
static unsigned adv_policy_key = UINT32_MAX;
static bool adv_peer_disabled(const bt_addr_le_t *peer) {
    unsigned mask = adv_pages_enabled_mask();
    for (unsigned i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (!(mask & BIT(i)) && !zmk_ble_profile_is_open(i) &&
            !bt_addr_le_cmp(peer, &profiles[i].peer)) return true;
    }
    return false;
}
static void adv_disconnect_disabled(void) {
    unsigned mask = adv_pages_enabled_mask();
    for (unsigned i = 0; i < ZMK_BLE_PROFILE_COUNT; i++)
        if (!(mask & BIT(i))) zmk_ble_prof_disconnect(i);
}
static int adv_start_filtered(void) {
    struct bt_le_adv_param param = *ZMK_ADV_CONN_NAME;
    if (!zmk_ble_active_profile_is_open()) {
        int err = bt_le_filter_accept_list_clear();
        if (err) return err;
        err = bt_le_filter_accept_list_add(zmk_ble_active_profile_addr());
        if (err) return err;
        param.options |= BT_LE_ADV_OPT_FILTER_CONN;
    }
    return bt_le_adv_start(&param, zmk_ble_ad, ARRAY_SIZE(zmk_ble_ad), NULL, 0);
}
