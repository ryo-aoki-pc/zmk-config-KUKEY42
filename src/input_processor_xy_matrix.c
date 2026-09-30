/*
 * SPDX-License-Identifier: MIT
 */

/*
 * REL_X / REL_Y に 2x2 行列を掛ける入力プロセッサ。
 *   X' = (m11 * X + m12 * Y) / divisor
 *   Y' = (m21 * X + m22 * Y) / divisor
 *
 * 入力プロセッサは 1 イベントずつ処理するため、X と Y を同時には扱えない。
 * PMW3610 ドライバは 1 回の報告ごとに REL_X (sync なし) → REL_Y (sync あり) の順で
 * 必ず両方を送るので、次のように計算する。
 *   - Y' は、同じ報告の X を保持しておいて計算する (正確)
 *   - X' の混合項 (m12 * Y) は、1 つ前の報告の Y で計算する
 *     (X は Y より先に listener へ渡るため)
 * 動きが途切れたら、前回の Y は次の動きに持ち越さない。
 */

#define DT_DRV_COMPAT zmk_input_processor_xy_matrix

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <drivers/input_processor.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define XY_MATRIX_IDLE_RESET_MS 100

struct xy_matrix_config {
    int32_t m11;
    int32_t m12;
    int32_t m21;
    int32_t m22;
    int32_t divisor;
};

struct xy_matrix_data {
    int32_t x;      // 今回の報告の X (Y' の計算に使う)
    int32_t prev_y; // 前回の報告の Y (X' の混合項に使う)
    int32_t remainder_x;
    int32_t remainder_y;
    int64_t last_event_ms;
};

static int32_t divide_with_remainder(int32_t numerator, int32_t divisor, int32_t *remainder) {
    numerator += *remainder;
    int32_t quotient = numerator / divisor;
    *remainder = numerator - quotient * divisor;

    return CLAMP(quotient, INT16_MIN, INT16_MAX);
}

static int xy_matrix_handle_event(const struct device *dev, struct input_event *event,
                                  uint32_t param1, uint32_t param2,
                                  struct zmk_input_processor_state *state) {
    const struct xy_matrix_config *cfg = dev->config;
    struct xy_matrix_data *data = dev->data;

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    switch (event->code) {
    case INPUT_REL_X: {
        int64_t now = k_uptime_get();
        if (now - data->last_event_ms > XY_MATRIX_IDLE_RESET_MS) {
            data->prev_y = 0;
        }
        data->last_event_ms = now;

        data->x = event->value;
        int32_t x = divide_with_remainder(cfg->m11 * data->x + cfg->m12 * data->prev_y,
                                          cfg->divisor, &data->remainder_x);

        LOG_DBG("xy matrix: X %d (prev Y %d) -> %d", data->x, data->prev_y, x);
        event->value = x;
        break;
    }
    case INPUT_REL_Y: {
        int32_t raw_y = event->value;
        int32_t y = divide_with_remainder(cfg->m21 * data->x + cfg->m22 * raw_y, cfg->divisor,
                                          &data->remainder_y);

        LOG_DBG("xy matrix: Y %d (X %d) -> %d", raw_y, data->x, y);
        event->value = y;

        data->prev_y = raw_y;
        data->x = 0;
        data->last_event_ms = k_uptime_get();
        break;
    }
    default:
        break;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api xy_matrix_driver_api = {
    .handle_event = xy_matrix_handle_event,
};

// 負の係数は DT 上 32 bit の 2 の補数になっているため、uint32_t を経由して符号付きに戻す
#define XY_MATRIX_COEF(n, idx) ((int32_t)(uint32_t)DT_INST_PROP_BY_IDX(n, matrix, idx))

#define XY_MATRIX_INST(n)                                                                          \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, matrix) == 4, "matrix must be <m11 m12 m21 m22>");            \
    BUILD_ASSERT(DT_INST_PROP(n, divisor) > 0, "divisor must be positive");                        \
    static const struct xy_matrix_config xy_matrix_config_##n = {                                  \
        .m11 = XY_MATRIX_COEF(n, 0),                                                               \
        .m12 = XY_MATRIX_COEF(n, 1),                                                               \
        .m21 = XY_MATRIX_COEF(n, 2),                                                               \
        .m22 = XY_MATRIX_COEF(n, 3),                                                               \
        .divisor = DT_INST_PROP(n, divisor),                                                       \
    };                                                                                             \
    static struct xy_matrix_data xy_matrix_data_##n;                                               \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &xy_matrix_data_##n, &xy_matrix_config_##n, POST_KERNEL,  \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &xy_matrix_driver_api);

DT_INST_FOREACH_STATUS_OKAY(XY_MATRIX_INST)
