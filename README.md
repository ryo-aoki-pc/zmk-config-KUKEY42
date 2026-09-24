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
| `settings_reset-seeeduino_xiao_ble-zmk.uf2` | 設定リセット用 |

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
