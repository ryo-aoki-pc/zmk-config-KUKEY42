/*
 * SPDX-License-Identifier: MIT
 */

/*
 * 試験用 (マージしない)。左手側がスリープから起きない原因を調べるため、USB でつないだまま
 * スリープさせ、スリープに入る前後の様子をログに出す。
 *
 * - zmk_usb_get_conn_state: activity.c が USB 給電中はスリープしないので、給電なしと答える
 * - zmk_pm_suspend_devices: 前後でキーの行・列とエンコーダのピンの設定 (SENSE など)、
 *   停止したデバイスを出す
 * - sys_poweroff: 呼ばれたことと、その直前のピンの設定を出す
 * - 起動時に RESETREAS (System OFF から起きたか) を出す
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/logging/log.h>

#include <soc.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct debug_pin {
    uint8_t port;
    uint8_t pin;
    const char *name;
};

// KUKEY42_L: 行は D1 D2 D3 D6、列は D10 D9 D8 D7 P0.10 P0.09、エンコーダは D5 D0
static const struct debug_pin debug_pins[] = {
    {0, 3, "row0"},  {0, 28, "row1"}, {0, 29, "row2"}, {1, 11, "row3"},
    {1, 15, "col0"}, {1, 14, "col1"}, {1, 13, "col2"}, {1, 12, "col3"},
    {0, 10, "col4"}, {0, 9, "col5"},  {0, 5, "encA"},  {0, 2, "encB"},
};

static void dump_gpio(const char *when) {
    for (size_t i = 0; i < ARRAY_SIZE(debug_pins); i++) {
        NRF_GPIO_Type *port = debug_pins[i].port ? NRF_P1 : NRF_P0;
        uint32_t cnf = port->PIN_CNF[debug_pins[i].pin];
        LOG_INF("sleep debug %s: %s P%u.%02u cnf 0x%08x dir %u pull %u sense %u in %u out %u", when,
                debug_pins[i].name, debug_pins[i].port, debug_pins[i].pin, cnf, cnf & 1,
                (cnf >> 2) & 3, (cnf >> 16) & 3, (port->IN >> debug_pins[i].pin) & 1,
                (port->OUT >> debug_pins[i].pin) & 1);
    }

    LOG_INF("sleep debug %s: P0 latch 0x%08x detectmode %u / P1 latch 0x%08x detectmode %u", when,
            NRF_P0->LATCH, NRF_P0->DETECTMODE, NRF_P1->LATCH, NRF_P1->DETECTMODE);

    for (int ch = 0; ch < 8; ch++) {
        uint32_t config = NRF_GPIOTE->CONFIG[ch];
        if (config & 3) {
            LOG_INF("sleep debug %s: GPIOTE ch %d config 0x%08x", when, ch, config);
        }
    }
    LOG_INF("sleep debug %s: GPIOTE intenset 0x%08x", when, NRF_GPIOTE->INTENSET);
}

// zmk/usb.h は zmk/keys.h を読み込み、soc.h のマクロとぶつかるので、enum zmk_usb_conn_state を int で受ける
// (0 = ZMK_USB_CONN_NONE)
int __wrap_zmk_usb_get_conn_state(void) { return 0; }

int __real_zmk_pm_suspend_devices(void);

int __wrap_zmk_pm_suspend_devices(void) {
    LOG_INF("sleep debug: going to sleep");
    dump_gpio("before suspend");
    k_msleep(500);

    int err = __real_zmk_pm_suspend_devices();
    LOG_INF("sleep debug: zmk_pm_suspend_devices returned %d", err);

    const struct device *devs;
    size_t count = z_device_get_all_static(&devs);
    for (size_t i = 0; i < count; i++) {
        enum pm_device_state state;
        if (pm_device_state_get(&devs[i], &state) == 0 && state != PM_DEVICE_STATE_ACTIVE) {
            LOG_INF("sleep debug: device %s is %s", devs[i].name, pm_device_state_str(state));
        }
    }

    dump_gpio("after suspend");
    k_msleep(1500);
    return err;
}

FUNC_NORETURN void __real_sys_poweroff(void);

FUNC_NORETURN void __wrap_sys_poweroff(void) {
    LOG_INF("sleep debug: sys_poweroff");
    dump_gpio("poweroff");
    k_msleep(1000);
    __real_sys_poweroff();
}

// 5 秒ごとに 50ms、ピンを細かく読む (システムのワークキューより低い優先度なので、キーマトリクスの走査は止めない)。
// 列の出力が変わっていれば、キーマトリクスは割り込み待ちではなく走査 (ポーリング) を続けている。
// 列が 1 のときに 1 だった行を、列ごとに出す (押されたと読めているキー)
static void sample_matrix(void) {
    uint32_t samples = 0, changes = 0;
    uint32_t col_high = 0, row_high = 0;
    uint32_t pair[6] = {0};
    uint32_t last0 = NRF_P0->OUT, last1 = NRF_P1->OUT;
    uint32_t start = k_cycle_get_32();
    uint32_t span = k_ms_to_cyc_ceil32(50);

    while (k_cycle_get_32() - start < span) {
        uint32_t in0 = NRF_P0->IN, in1 = NRF_P1->IN, out0 = NRF_P0->OUT, out1 = NRF_P1->OUT;
        samples++;
        if (out0 != last0 || out1 != last1) {
            changes++;
            last0 = out0;
            last1 = out1;
        }

        uint32_t rows = 0;
        for (size_t r = 0; r < 4; r++) {
            const struct debug_pin *p = &debug_pins[r];
            if (((p->port ? in1 : in0) >> p->pin) & 1) {
                rows |= BIT(r);
            }
        }
        row_high |= rows;

        for (size_t c = 0; c < 6; c++) {
            const struct debug_pin *p = &debug_pins[4 + c];
            if (((p->port ? out1 : out0) >> p->pin) & 1) {
                col_high |= BIT(c);
                pair[c] |= rows;
            }
        }
    }

    LOG_INF("matrix sample: %u samples, output changes %u, cols high 0x%02x, rows high 0x%02x, "
            "row sense %u%u%u%u",
            samples, changes, col_high, row_high, (NRF_P0->PIN_CNF[3] >> 16) & 3,
            (NRF_P0->PIN_CNF[28] >> 16) & 3, (NRF_P0->PIN_CNF[29] >> 16) & 3,
            (NRF_P1->PIN_CNF[11] >> 16) & 3);
    for (size_t c = 0; c < 6; c++) {
        if (pair[c]) {
            LOG_INF("matrix sample: col%u high with rows 0x%x", (unsigned int)c, pair[c]);
        }
    }
}

static void sampler_thread(void *a, void *b, void *c) {
    while (true) {
        k_msleep(5000);
        sample_matrix();
    }
}

K_THREAD_DEFINE(sleep_debug_sampler, 1024, sampler_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

static int sleep_debug_init(void) {
    LOG_INF("sleep debug: boot, RESETREAS 0x%08x", NRF_POWER->RESETREAS);
    return 0;
}

SYS_INIT(sleep_debug_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
