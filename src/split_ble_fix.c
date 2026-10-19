/*
 * SPDX-License-Identifier: MIT
 */

/*
 * ZMK v0.3.0 の分割 BLE の不具合を、ZMK を変えずに回避する。
 *
 * ZMK から別のファイルの関数を呼んでいるところに、リンカの --wrap で割り込む (CMakeLists.txt)。
 * --wrap が差し替えるのは翻訳単位をまたぐ呼び出しだけなので、Zephyr の中どうしの呼び出し
 * (gatt.c の中の bt_gatt_discover など) は変わらない。どれも Kconfig (CONFIG_ZMK_SPLIT_BLE_FIX_*) で
 * 1 つずつ止められる。ZMK を上げるときは、上流で直ったものを外す。
 *
 * セントラル (右手側):
 *   SCAN           ペリフェラル (左手側) が切れている間、ZMK は 60ms ごとに 30ms (50%) スキャンし続ける。
 *                  PC との接続の送信の機会が減り、カーソルのレポートがまとめて届いて動きが粗くなる。
 *                  320ms ごとに 15ms にする。起きた左手側は 1.28 秒間、高頻度の directed 広告を出すので、
 *                  その間に窓が 4 回来て、すぐにつながる。
 *   SUBSCRIBE      ZMK は位置・センサー・電池の購読で、CCC を探すための領域を 1 つ共用している。
 *                  探し終わる前に次の購読が来ると前の購読が上書きされ、Zephyr の購読の一覧に入らない
 *                  (zmkfirmware/zmk#3156)。位置の購読が抜けると、つながっていても左手側のキーが届かない。
 *                  購読ごとに別の領域を使わせる。
 *   RECOVERY       キャラクタリスティックの探索は、途中の要求がバッファ不足 (-ENOMEM) になると、Zephyr が
 *                  「完了」として終わらせる (#3156)。バッファは PC へのマウス・キーの通知と取り合うので、
 *                  ボールを動かしながら左手側がつながると起きやすい。探索が途中で終わったら続きから探し直す。
 *                  それでも、つながってから決めた時間のうちに位置の購読を確かめられなければ、接続を切って
 *                  最初からやり直させる (左手側の電源を入れ直したのと同じ)。
 *   BATTERY_SOURCE ZMK は PC との切断でも、ペリフェラルの電池のイベントを番号 234 (-EINVAL) で出し、
 *                  配列の範囲外に書き込む (zmkfirmware/zmk#3522)。範囲外の番号のイベントを捨てる。
 *
 * ペリフェラル (左手側):
 *   ADV_RETRY      切断の直後は前の接続がまだ残っていて、広告の開始に失敗することがある (-EINVAL / -ENOMEM)。
 *                  ZMK は再試行しないので、左手側は電源を入れ直すまでつながらず、右手側はスキャンし続ける。
 *                  成功するまで再試行する。
 *
 * BT の受信スレッドとシステムのワークキューはどちらも協調スレッドで、互いに割り込まないので、
 * ここの状態はロックなしで触る。
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include <zmk/split/bluetooth/uuid.h>
#include <zmk/split/transport/central.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ---- SCAN ---------------------------------------------------------------------------------- */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_SCAN)

BUILD_ASSERT(CONFIG_ZMK_SPLIT_BLE_FIX_SCAN_WINDOW <= CONFIG_ZMK_SPLIT_BLE_FIX_SCAN_INTERVAL,
             "CONFIG_ZMK_SPLIT_BLE_FIX_SCAN_WINDOW must not exceed the scan interval");

int __real_bt_le_scan_start(const struct bt_le_scan_param *param, bt_le_scan_cb_t cb);

int __wrap_bt_le_scan_start(const struct bt_le_scan_param *param, bt_le_scan_cb_t cb) {
    // ZMK の start_scanning() (split/bluetooth/central.c) が渡す BT_LE_SCAN_PASSIVE だけを変える
    if (param == NULL || param->type != BT_LE_SCAN_TYPE_PASSIVE ||
        param->interval != BT_GAP_SCAN_FAST_INTERVAL || param->window != BT_GAP_SCAN_FAST_WINDOW) {
        return __real_bt_le_scan_start(param, cb);
    }

    // Zephyr は値を写して使うので、スタック上の写しでよい
    struct bt_le_scan_param slow = *param;
    slow.interval = CONFIG_ZMK_SPLIT_BLE_FIX_SCAN_INTERVAL;
    slow.window = CONFIG_ZMK_SPLIT_BLE_FIX_SCAN_WINDOW;

    LOG_INF("split BLE fix: scanning for peripherals with window %u / interval %u (x0.625 ms)",
            slow.window, slow.interval);
    return __real_bt_le_scan_start(&slow, cb);
}

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_SCAN)

/* ---- BATTERY_SOURCE ------------------------------------------------------------------------ */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_BATTERY_SOURCE)

int __real_zmk_split_transport_central_peripheral_event_handler(
    const struct zmk_split_transport_central *transport, uint8_t source,
    struct zmk_split_transport_peripheral_event ev);

int __wrap_zmk_split_transport_central_peripheral_event_handler(
    const struct zmk_split_transport_central *transport, uint8_t source,
    struct zmk_split_transport_peripheral_event ev) {
    if (source >= CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS) {
        LOG_WRN("split BLE fix: dropped a peripheral event (type %d) with invalid source %u",
                ev.type, source);
        return -EINVAL;
    }

    return __real_zmk_split_transport_central_peripheral_event_handler(transport, source, ev);
}

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_BATTERY_SOURCE)

/* ---- RECOVERY ------------------------------------------------------------------------------ */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY)

// 途中で終わった探索を続きから探し直す回数と間隔
#define RESUME_MAX 3
#define RESUME_DELAY_MS 100
// 続けて接続をやり直させる上限 (やり直しが繰り返されないように。位置の購読を確かめたら 0 に戻す)
#define RECONNECT_MAX 5

struct link_state {
    // ペリフェラルとの接続 (参照を持つ)。NULL なら空き
    struct bt_conn *conn;
    // ZMK のキャラクタリスティックの探索の引数 (slot->discover_params)
    struct bt_gatt_discover_params *chrc_params;
    // 位置のキャラクタリスティックの値のハンドル。0 ならまだ見つかっていない
    uint16_t position_handle;
    // 位置の購読を確かめた
    bool position_ok;
    uint8_t resumes;
    // 接続・切断のたびに増やす。待っている間に切断・再接続したかを見分ける (conn のポインタは使い回される)
    uint32_t generation;
    const char *failure;
    struct k_work_delayable resume_work;
    struct k_work_delayable check_work;
};

static struct link_state links[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];
static uint8_t reconnects;

// 探索の続きは専用のワークキューで探し直す。ATT の受信スレッドの外からの bt_gatt_discover は、
// 要求の領域が空くまで最大 30 秒 (BT_ATT_TIMEOUT) 待つので、システムのワークキュー (BT の送信完了や
// check_work も動く) を止めないようにする。協調スレッドなので、状態を触る途中で割り込まれない
K_THREAD_STACK_DEFINE(resume_queue_stack, CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY_STACK_SIZE);
static struct k_work_q resume_queue;

static void schedule_resume(struct link_state *link) {
    link->resumes++;
    k_work_reschedule_for_queue(&resume_queue, &link->resume_work, K_MSEC(RESUME_DELAY_MS));
}

// ZMK の split_central_chrc_discovery_func (static なので、最初の探索で覚える)
static bt_gatt_discover_func_t zmk_chrc_func;

static const struct bt_uuid_128 position_uuid =
    BT_UUID_INIT_128(ZMK_SPLIT_BT_CHAR_POSITION_STATE_UUID);

int __real_bt_gatt_discover(struct bt_conn *conn, struct bt_gatt_discover_params *params);

static struct link_state *link_for_conn(const struct bt_conn *conn) {
    for (size_t i = 0; i < ARRAY_SIZE(links); i++) {
        if (links[i].conn == conn) {
            return &links[i];
        }
    }

    return NULL;
}

static void confirm_position(struct link_state *link) {
    if (link->position_ok) {
        return;
    }

    LOG_DBG("split BLE fix: key positions of the peripheral are subscribed");
    link->position_ok = true;
    reconnects = 0;
    k_work_cancel_delayable(&link->check_work);
}

// 失敗の理由を覚えるだけで、すぐには切らない。初めてのペアリングの途中などで切らないように、
// 接続をやり直させるかは期限の check_work で決める
static void note_failure(struct link_state *link, const char *failure) {
    if (link->position_ok) {
        return;
    }

    LOG_WRN("split BLE fix: %s", failure);
    link->failure = failure;
}

static void check_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct link_state *link = CONTAINER_OF(dwork, struct link_state, check_work);

    if (link->conn == NULL || link->position_ok) {
        return;
    }

    const char *failure =
        link->failure != NULL ? link->failure : "key positions were not subscribed in time";

    if (reconnects >= RECONNECT_MAX) {
        LOG_ERR("split BLE fix: %s; gave up reconnecting after %d attempts", failure,
                RECONNECT_MAX);
        return;
    }

    reconnects++;
    LOG_WRN("split BLE fix: %s; reconnecting the peripheral (%d/%d)", failure, reconnects,
            RECONNECT_MAX);

    int err = bt_conn_disconnect(link->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err < 0) {
        LOG_ERR("split BLE fix: failed to disconnect the peripheral (%d)", err);
    }
}

// 探索をあきらめるとき。ZMK に終わりを伝える。位置が見つかっていなければ、期限の check_work が
// 接続をやり直させる
static void finish_discovery(struct link_state *link, struct bt_conn *conn,
                             struct bt_gatt_discover_params *params) {
    if (zmk_chrc_func != NULL) {
        zmk_chrc_func(conn, NULL, params);
    }

    if (link != NULL && link->position_handle == 0) {
        note_failure(link, "the key position characteristic was not found");
    }
}

static void resume_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct link_state *link = CONTAINER_OF(dwork, struct link_state, resume_work);

    if (link->conn == NULL || link->chrc_params == NULL) {
        return;
    }

    // 待っている間に切断されても conn を放されないように、参照を持っておく
    struct bt_conn *conn = bt_conn_ref(link->conn);
    struct bt_gatt_discover_params *params = link->chrc_params;
    uint32_t generation = link->generation;

    LOG_INF("split BLE fix: resuming the characteristic discovery at handle 0x%04x (%d/%d)",
            params->start_handle, link->resumes, RESUME_MAX);

    int err = __real_bt_gatt_discover(conn, params);

    if (err < 0 && link->generation == generation) {
        if (link->resumes < RESUME_MAX) {
            schedule_resume(link);
        } else {
            LOG_WRN("split BLE fix: could not resume the characteristic discovery (%d)", err);
            finish_discovery(link, conn, params);
        }
    }

    bt_conn_unref(conn);
}

// ZMK のキャラクタリスティックの探索のコールバックの代わりに呼ばれ、ZMK に中継する
static uint8_t chrc_discovery_relay(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    struct bt_gatt_discover_params *params) {
    struct link_state *link = link_for_conn(conn);

    if (attr != NULL) {
        if (link != NULL && params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
            const struct bt_gatt_chrc *chrc = attr->user_data;
            if (bt_uuid_cmp(chrc->uuid, &position_uuid.uuid) == 0) {
                link->position_handle = chrc->value_handle;
            }
        }

        return zmk_chrc_func(conn, attr, params);
    }

    // ZMK が必要なものを見つけきる前に探索が終わった。範囲が残っていれば、要求の失敗で
    // 打ち切られたかもしれないので、少し待って続きから探し直す
    if (link != NULL && link->conn != NULL && params->start_handle <= params->end_handle &&
        link->resumes < RESUME_MAX) {
        schedule_resume(link);
        return BT_GATT_ITER_STOP;
    }

    finish_discovery(link, conn, params);
    return BT_GATT_ITER_STOP;
}

int __wrap_bt_gatt_discover(struct bt_conn *conn, struct bt_gatt_discover_params *params) {
    struct link_state *link = NULL;

    // ZMK の split_central_service_discovery_func が始めるキャラクタリスティックの探索
    if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC && params->func != chrc_discovery_relay) {
        link = link_for_conn(conn);
        if (link != NULL) {
            zmk_chrc_func = params->func;
            params->func = chrc_discovery_relay;
            link->chrc_params = params;
            link->resumes = 0;
        }
    }

    int err = __real_bt_gatt_discover(conn, params);

    if (err < 0 && link != NULL) {
        // ZMK はエラーを記録するだけなので、こちらで探し直す
        schedule_resume(link);
    }

    return err;
}

static void position_subscribed(struct bt_conn *conn, uint8_t err,
                                struct bt_gatt_subscribe_params *params) {
    struct link_state *link = link_for_conn(conn);
    if (link == NULL) {
        return;
    }

    if (err) {
        note_failure(link, "writing the key position CCC failed");
    } else if (params->value) {
        confirm_position(link);
    }
}

static void recovery_connected(struct bt_conn *conn, uint8_t err) {
    struct bt_conn_info info;

    // PC との接続 (こちらがペリフェラル側) は見ない
    if (err || bt_conn_get_info(conn, &info) < 0 || info.role != BT_CONN_ROLE_CENTRAL) {
        return;
    }

    struct link_state *link = link_for_conn(NULL);
    if (link == NULL) {
        LOG_WRN("split BLE fix: no free state for a peripheral connection");
        return;
    }

    link->conn = bt_conn_ref(conn);
    link->generation++;
    link->chrc_params = NULL;
    link->position_handle = 0;
    link->position_ok = false;
    link->resumes = 0;
    link->failure = NULL;
    k_work_reschedule(&link->check_work, K_MSEC(CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY_TIMEOUT_MS));
}

// 暗号化 (初めてならペアリング) が済んでから購読が進むので、期限をそこから測り直す
static void recovery_security_changed(struct bt_conn *conn, bt_security_t level,
                                      enum bt_security_err err) {
    struct link_state *link = link_for_conn(conn);
    if (link == NULL || link->position_ok) {
        return;
    }

    k_work_reschedule(&link->check_work, K_MSEC(CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY_TIMEOUT_MS));
}

static void recovery_disconnected(struct bt_conn *conn, uint8_t reason) {
    struct link_state *link = link_for_conn(conn);
    if (link == NULL) {
        return;
    }

    k_work_cancel_delayable(&link->resume_work);
    k_work_cancel_delayable(&link->check_work);
    bt_conn_unref(link->conn);
    link->conn = NULL;
    link->chrc_params = NULL;
    link->generation++;

    // こちらから切ったのでなければ (左手側の電源を切った、離れたなど)、やり直しの回数を数え直す
    if (reason != BT_HCI_ERR_LOCALHOST_TERM_CONN) {
        reconnects = 0;
    }
}

// ZMK が bt_conn_cb_register() で登録したコールバックの後に呼ばれる
BT_CONN_CB_DEFINE(split_ble_fix_recovery) = {
    .connected = recovery_connected,
    .disconnected = recovery_disconnected,
    .security_changed = recovery_security_changed,
};

static int recovery_init(void) {
    static const struct k_work_queue_config resume_queue_config = {.name = "split_ble_fix"};

    k_work_queue_init(&resume_queue);
    k_work_queue_start(&resume_queue, resume_queue_stack,
                       K_THREAD_STACK_SIZEOF(resume_queue_stack),
                       K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1), &resume_queue_config);

    for (size_t i = 0; i < ARRAY_SIZE(links); i++) {
        k_work_init_delayable(&links[i].resume_work, resume_work_cb);
        k_work_init_delayable(&links[i].check_work, check_work_cb);
    }

    return 0;
}

SYS_INIT(recovery_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY)

/* ---- SUBSCRIBE (と RECOVERY の購読の確認) ------------------------------------------------ */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_SUBSCRIBE) || IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_SUBSCRIBE)

// ペリフェラルごとに位置・センサー・電池と予備 1 つ。ZMK の購読の引数は static なので、放さずに使い続ける
struct subscribe_slot {
    struct bt_gatt_subscribe_params *params;
    struct bt_gatt_discover_params disc_params;
};

static struct subscribe_slot subscribe_slots[4 * CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];

static struct bt_gatt_discover_params *own_disc_params(struct bt_gatt_subscribe_params *params) {
    struct subscribe_slot *empty = NULL;

    for (size_t i = 0; i < ARRAY_SIZE(subscribe_slots); i++) {
        if (subscribe_slots[i].params == params) {
            return &subscribe_slots[i].disc_params;
        }
        if (subscribe_slots[i].params == NULL && empty == NULL) {
            empty = &subscribe_slots[i];
        }
    }

    if (empty == NULL) {
        return NULL;
    }

    empty->params = params;
    return &empty->disc_params;
}

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_SUBSCRIBE)

int __real_bt_gatt_subscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *params);

int __wrap_bt_gatt_subscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *params) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_SUBSCRIBE)
    // CCC のハンドルが分からないときだけ Zephyr が disc_params で探す
    if (params->ccc_handle == 0 && params->disc_params != NULL) {
        struct bt_gatt_discover_params *disc_params = own_disc_params(params);
        if (disc_params != NULL) {
            params->disc_params = disc_params;
        } else {
            LOG_WRN("split BLE fix: no free CCC discovery parameters for handle 0x%04x",
                    params->value_handle);
        }
    }
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY)
    struct link_state *link = link_for_conn(conn);
    bool is_position = link != NULL && link->position_handle != 0 &&
                       params->value_handle == link->position_handle;
    if (is_position) {
        // CCC を書いた結果を受け取る (ZMK は使っていない)
        params->subscribe = position_subscribed;
    }
#endif

    int err = __real_bt_gatt_subscribe(conn, params);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_RECOVERY)
    if (is_position) {
        if (err == -EALREADY) {
            // 前の接続からの購読が残っている (ボンド済みの相手は、切断しても購読が残る)
            confirm_position(link);
        } else if (err < 0) {
            note_failure(link, "subscribing to the key positions failed");
        }
    }
#endif

    return err;
}

#endif // SUBSCRIBE || RECOVERY

/* ---- ADV_RETRY ----------------------------------------------------------------------------- */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_ADV_RETRY)

int __real_bt_le_adv_start(const struct bt_le_adv_param *param, const struct bt_data *ad,
                           size_t ad_len, const struct bt_data *sd, size_t sd_len);

// 最後に失敗した広告の引数の写し。ad / sd は ZMK の static const の配列か NULL なので、ポインタのままでよい
static struct {
    struct bt_le_adv_param param;
    bt_addr_le_t peer;
    const struct bt_data *ad;
    size_t ad_len;
    const struct bt_data *sd;
    size_t sd_len;
} adv_retry;

static void adv_retry_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(adv_retry_work, adv_retry_work_cb);

static void adv_retry_work_cb(struct k_work *work) {
    int err = __real_bt_le_adv_start(&adv_retry.param, adv_retry.ad, adv_retry.ad_len,
                                     adv_retry.sd, adv_retry.sd_len);
    if (err == 0 || err == -EALREADY) {
        LOG_INF("split BLE fix: advertising restarted");
        return;
    }

    LOG_WRN("split BLE fix: advertising failed again (%d)", err);
    k_work_reschedule(&adv_retry_work, K_MSEC(CONFIG_ZMK_SPLIT_BLE_FIX_ADV_RETRY_MS));
}

int __wrap_bt_le_adv_start(const struct bt_le_adv_param *param, const struct bt_data *ad,
                           size_t ad_len, const struct bt_data *sd, size_t sd_len) {
    // 新しい広告の要求が来たら、前の再試行は要らない
    k_work_cancel_delayable(&adv_retry_work);

    int err = __real_bt_le_adv_start(param, ad, ad_len, sd, sd_len);
    if (err == 0 || err == -EALREADY) {
        return err;
    }

    adv_retry.param = *param;
    if (param->peer != NULL) {
        bt_addr_le_copy(&adv_retry.peer, param->peer);
        adv_retry.param.peer = &adv_retry.peer;
    }
    adv_retry.ad = ad;
    adv_retry.ad_len = ad_len;
    adv_retry.sd = sd;
    adv_retry.sd_len = sd_len;

    LOG_WRN("split BLE fix: advertising failed (%d), retrying every %d ms", err,
            CONFIG_ZMK_SPLIT_BLE_FIX_ADV_RETRY_MS);
    k_work_reschedule(&adv_retry_work, K_MSEC(CONFIG_ZMK_SPLIT_BLE_FIX_ADV_RETRY_MS));
    return err;
}

int __real_bt_le_adv_stop(void);

// ZMK が広告を止めたら (分割の通信を止めたとき)、再試行もやめる
int __wrap_bt_le_adv_stop(void) {
    k_work_cancel_delayable(&adv_retry_work);
    return __real_bt_le_adv_stop();
}

static void adv_retry_connected(struct bt_conn *conn, uint8_t err) {
    // 高頻度の directed 広告の終わりも err 付きの connected で届くので、つながったときだけ止める
    if (!err) {
        k_work_cancel_delayable(&adv_retry_work);
    }
}

BT_CONN_CB_DEFINE(split_ble_fix_adv_retry) = {
    .connected = adv_retry_connected,
};

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_FIX_ADV_RETRY)
