# zmk-config-KUKEY42

KUKEY42 のファームウェアです。

## キー割り当て一覧

各レイヤーのキー割り当ては [KEYMAP.html](KEYMAP.html) にまとめています。ブラウザでレンダリング表示する場合は以下のリンクから閲覧できます。

https://htmlpreview.github.io/?https://github.com/ryo-aoki-pc/zmk-config-KUKEY42/blob/custom/KEYMAP.html

## 命名規則（カスタムビヘイビア）

`config/KUKEY42.keymap` の Macro / Tap Dance / Mod Morph は以下の規則で命名します。

- **構造**：`<prefix>_vim_<id>` 形式。`<prefix>` は `macro_`（マクロ）/ `td_`（タップダンス）/ `mm_`（モッドモーフ）。ノードラベル・ノード名・`label` を一致させ、`label` はラベルの大文字にする（例：`mm_vim_g` → `label = "MM_VIM_G"`）。
- **Mod Morph の `<id>`**：キーに直接割り当てるモーフは無修飾時の vim キーで命名（`mm_vim_d` `mm_vim_g` など）。ベースが `&none`（修飾時のみ動作）またはネスト用ヘルパーは、修飾＋キーストロークで命名する（`mm_vim_shift_4` `mm_vim_ctrl_r` `mm_vim_shift_d`）。
