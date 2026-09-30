/*
 * SPDX-License-Identifier: MIT
 */

/*
 * REL_X / REL_Y に 2x2 行列を掛ける入力プロセッサ。
 *   X' = (m11 * X + m12 * Y) / divisor
 *   Y' = (m21 * X + m22 * Y) / divisor
 *
 * 入力プロセッサは 1 イベントずつ処理し、listener は REL_X を受けた時点で値を
 * 足し込んでしまう。PMW3610 ドライバは 1 回の報告ごとに REL_X (sync なし) →
 * REL_Y (sync あり) の順で送るため、X を受けた時点では X' を正しく計算できない。
 *
 * そこで、X と Y はここで止めて (ZMK_INPUT_PROC_STOP)、組がそろった時点で
 * このプロセッサ自身を入力デバイスとして X' / Y' を出し直す。
 * overlay 側で、このプロセッサを device にした input-listener を別に用意し、
 * そちらがカーソルを動かす。WHEEL などほかのイベントはそのまま元の listener に流す。
 */

#define DT_DRV_COMPAT zmk_input_processor_xy_matrix

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>
#include <drivers/input_processor.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct xy_matrix_config {
    int32_t m11;
    int32_t m12;
    int32_t m21;
    int32_t m22;
    int32_t divisor;
};

struct xy_matrix_data {
    int32_t x; // Y を待っている X
    bool has_x;
    int32_t remainder_x;
    int32_t remainder_y;
};

static int32_t divide_with_remainder(int32_t numerator, int32_t divisor, int32_t *remainder) {
    numerator += *remainder;
    int32_t quotient = numerator / divisor;
    *remainder = numerator - quotient * divisor;

    return CLAMP(quotient, INT16_MIN, INT16_MAX);
}

static void xy_matrix_emit(const struct device *dev, int32_t x, int32_t y, bool sync) {
    const struct xy_matrix_config *cfg = dev->config;
    struct xy_matrix_data *data = dev->data;

    int32_t out_x =
        divide_with_remainder(cfg->m11 * x + cfg->m12 * y, cfg->divisor, &data->remainder_x);
    int32_t out_y =
        divide_with_remainder(cfg->m21 * x + cfg->m22 * y, cfg->divisor, &data->remainder_y);

    LOG_DBG("xy matrix: (%d, %d) -> (%d, %d)", x, y, out_x, out_y);

    if (out_x == 0 && out_y == 0) {
        return;
    }

    // input スレッド内から呼ぶので待たない (Zephyr も input スレッド内では K_NO_WAIT にする)
    input_report_rel(dev, INPUT_REL_X, out_x, false, K_NO_WAIT);
    input_report_rel(dev, INPUT_REL_Y, out_y, sync, K_NO_WAIT);
}

static int xy_matrix_handle_event(const struct device *dev, struct input_event *event,
                                  uint32_t param1, uint32_t param2,
                                  struct zmk_input_processor_state *state) {
    struct xy_matrix_data *data = dev->data;

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    switch (event->code) {
    case INPUT_REL_X:
        if (data->has_x) {
            // 前の X に Y が来なかった
            xy_matrix_emit(dev, data->x, 0, true);
        }

        if (event->sync) {
            // この報告には Y が来ない
            data->has_x = false;
            xy_matrix_emit(dev, event->value, 0, true);
        } else {
            data->x = event->value;
            data->has_x = true;
        }
        return ZMK_INPUT_PROC_STOP;

    case INPUT_REL_Y: {
        int32_t x = data->has_x ? data->x : 0;
        data->has_x = false;

        xy_matrix_emit(dev, x, event->value, event->sync);
        return ZMK_INPUT_PROC_STOP;
    }

    default:
        return ZMK_INPUT_PROC_CONTINUE;
    }
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
