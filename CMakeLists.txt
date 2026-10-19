target_sources_ifdef(CONFIG_ZMK_USB_BOOTMAGIC app PRIVATE src/usb_bootmagic.c)

if(CONFIG_DT_HAS_ZMK_USB_BOOTMAGIC_ENABLED AND NOT CONFIG_ZMK_USB_BOOTMAGIC)
    message(WARNING "zmk,usb-bootmagic is in the devicetree, but CONFIG_ZMK_USB_BOOTMAGIC is disabled (CONFIG_USB_DEVICE_STACK is required)")
endif()

# ZMK v0.3.0 の分割 BLE の不具合の回避。ZMK から呼ばれる関数を、リンカの --wrap で
# src/split_ble_fix.c の __wrap_<関数> に差し替える。ここの条件は split_ble_fix.c の #if と揃える
# (__wrap_<関数> が無いとリンクで失敗する)
target_sources_ifdef(CONFIG_ZMK_SPLIT_BLE_FIX app PRIVATE src/split_ble_fix.c)

if(CONFIG_ZMK_SPLIT_BLE_FIX_SCAN)
    zephyr_link_libraries(-Wl,--wrap=bt_le_scan_start)
endif()

if(CONFIG_ZMK_SPLIT_BLE_FIX_SUBSCRIBE OR CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY)
    zephyr_link_libraries(-Wl,--wrap=bt_gatt_subscribe)
endif()

if(CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY)
    zephyr_link_libraries(-Wl,--wrap=bt_gatt_discover)
endif()

if(CONFIG_ZMK_SPLIT_BLE_FIX_BATTERY_SOURCE)
    zephyr_link_libraries(-Wl,--wrap=zmk_split_transport_central_peripheral_event_handler)
endif()

if(CONFIG_ZMK_SPLIT_BLE_FIX_ADV_RETRY)
    zephyr_link_libraries(-Wl,--wrap=bt_le_adv_start -Wl,--wrap=bt_le_adv_stop)
endif()
