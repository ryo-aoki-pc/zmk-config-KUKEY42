# zmk-config-KUKEY42

KUKEY42 のファームウェアです。

## キー割り当て一覧

各レイヤーのキー割り当ては [KEYMAP.html](KEYMAP.html) にまとめています。ブラウザでレンダリング表示する場合は以下のリンクから閲覧できます。

https://htmlpreview.github.io/?https://github.com/ryo-aoki-pc/zmk-config-KUKEY42/blob/custom/KEYMAP.html

## 命名規則（カスタムビヘイビア）

`config/KUKEY42.keymap` の Macro / Tap Dance / Mod Morph は以下の規則で命名します。

- **構造**：`<prefix>_vim_<id>` 形式。`<prefix>` は `macro_`（マクロ）/ `td_`（タップダンス）/ `mm_`（モッドモーフ）。ノードラベル・ノード名・`label` を一致させ、`label` はラベルの大文字にする（例：`mm_vim_g` → `label = "MM_VIM_G"`）。
- **Mod Morph の `<id>`**：キーに直接割り当てるモーフは無修飾時の vim キーで命名（`mm_vim_d` `mm_vim_g` など）。ベースが `&none`（修飾時のみ動作）またはネスト用ヘルパーは、修飾＋キーストロークで命名する（`mm_vim_shift_4` `mm_vim_ctrl_r` `mm_vim_shift_d`）。

## 生成されるファームウェア一覧

| ファームウェア名 | 説明 |
| --- | --- |
| `KUKEY42_left_peripheral.uf2` | 左側 ペリフェラル |
| `KUKEY42_right_central.uf2` | 右側 セントラル |
| `KUKEY42_right_central_studio.uf2` | 右側 セントラル (ZMK Studio 対応) |
| `KUKEY42_right_central_logging.uf2` | 右側 セントラル (ログ版) |
| `settings_reset-seeeduino_xiao_ble-zmk.uf2` | 設定リセット用 |

`_logging` 版は、USB の COM ポートにデバッグログ (`zmk-usb-logging`) を出すセントラルです。ZMK Studio は入っていません。[zmk-config-keyboards](https://github.com/ryo-aoki-pc/zmk-config-keyboards) の `tools/keyboard-check.cmd` の「レイヤーの動きを見る」で、押したキーのレイヤーの遷移と解決を表示するのに使います。ログを出すとスレッドのスタックの使用量が増えるので、ログ版だけスタックを大きくし、スレッドごとのスタックの最大使用量を 30 秒ごとにログに出します (`<inf> thread_analyzer:` の行。設定は `build.yaml`)。調べ終わったら通常版に戻してください。ローカルビルドでは Studio 版と同じく `make` / `make all` では作らず、`make all_studio_p` / `make all_studio` で作ります。

## 分割 BLE の不具合の回避 (`src/split_ble_fix.c`)

ZMK v0.3.0 の分割 BLE には、次の症状の原因になる不具合があります。ZMK を変えずに、リンカの `--wrap` で ZMK の呼び出しに割り込んで回避しています (`CMakeLists.txt`)。

| 症状 | 原因 | 回避 (`CONFIG_ZMK_SPLIT_BLE_FIX_*`) |
| --- | --- | --- |
| 右手側だけがつながっているとき、カーソルの動きが粗い | 左手側が切れている間、右手側が 60ms ごとに 30ms (50%) スキャンし続け、PC との送信の機会を奪う | `SCAN`: 320ms ごとに 15ms にする |
| 左手側がつながっているのに、キーが届かない (左手側の電源を入れ直すと直る) | 再接続のときの GATT の購読の上書き ([zmkfirmware/zmk#3156](https://github.com/zmkfirmware/zmk/issues/3156))、バッファ不足での探索の打ち切り | `SUBSCRIBE`: 購読ごとに別の領域を使う。`RECOVERY`: 探索の続きを探し直し、つながって (暗号化が済んで) から 5 秒のうちにキーの購読を確かめられなければ接続をやり直す |
| PC との切断の後におかしくなる | 電池のイベントを範囲外の番号 (234) で配列に書き込む ([zmkfirmware/zmk#3522](https://github.com/zmkfirmware/zmk/issues/3522)) | `BATTERY_SOURCE`: 範囲外の番号のイベントを捨てる |
| 左手側が切れたあと、電源を入れ直すまでつながらない | 切断の直後に広告の開始に失敗しても、ZMK は再試行しない | `ADV_RETRY` (左手側): 1 秒ごとに再試行する |

回避が働くと、ログ版のセントラルが `split BLE fix:` で始まる行をログに出します。左右とも書き込み直してください (`ADV_RETRY` は左手側、ほかは右手側に入ります)。ZMK を上げるときは、上流で直ったものを外します。

## ローカルビルド手順

GitHub Actions でのビルドは毎回 2〜3 分かかりますが、ローカル環境では 40 秒〜1 分で完了します (PC スペックによって前後します)。
キーマップを少し試したいだけでもローカルビルドなら素早く試行錯誤ができます。

### 必要なもの

- [Visual Studio Code](https://code.visualstudio.com/)
- [Docker Desktop](https://www.docker.com/products/docker-desktop/)
- VS Code 拡張機能: [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)

### 手順

1. **準備**
   1. このリポジトリを PC に clone します。
   2. Docker Desktop を起動します。
   3. VS Code でこのフォルダを開きます。
   4. 右下に表示される「Reopen in Container (コンテナーで再度開く)」をクリックします (初回は環境構築に時間がかかります)。

2. **ビルド**

   VS Code のターミナルで以下のいずれかを実行します。

   > [!TIP]
   > ビルドは CPU コアを使って並列実行できます。並列数は自動で CPU コア数になりますが、
   > 環境変数 `PARALLEL` で指定することもできます (例: `PARALLEL=4 make all_p`)。

   | コマンド | 内容 |
   | --- | --- |
   | `make` | 全ファームウェアを並列ビルド (ZMK Studio 版を除く) |
   | `make all` | 全ファームウェアを逐次ビルド (ZMK Studio 版を除く) |
   | `make all_studio_p` | ZMK Studio 版も含めて並列ビルド |
   | `make all_studio` | ZMK Studio 版も含めて逐次ビルド |
   | `make single` | 一覧から番号を選んで 1 つだけビルド |
   | `make clean` | `firmware_builds/` を削除 |

   キーマップ変更だけを試すなら `KUKEY42_right_central` のみで十分です。

3. **完成**

   `firmware_builds/` に `.uf2` が生成されます。これをキーボードに書き込みます。
