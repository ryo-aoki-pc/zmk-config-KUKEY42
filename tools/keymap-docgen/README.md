# keymap-docgen

Generate human-readable key-assignment docs (Excel **.xlsx** + a self-contained
**.html**) from a ZMK `.keymap` file. The tool parses standard ZMK behaviours —
`&kp`, `&mt` (mod-tap), `&lt` (layer-tap), `&mo`/`&to` (layers), mod-morph,
tap-dance and macros — resolves them recursively, and lays every layer out to
match the board's real physical arrangement (including the split gap).

The script itself contains **no keyboard-specific data**. Everything particular
to a board — each key's physical position *and its display label* — lives in a
small per-keyboard layout JSON, so the same `keymap_docgen.py` can be vendored
(or used as a git submodule) across multiple keyboards unchanged.

## Requirements

- Python **3.10+** (uses `X | Y` type syntax).
- [`openpyxl`](https://pypi.org/project/openpyxl/) — optional, only for `.xlsx`
  output (`pip install openpyxl`). Without it the `.html` is still generated.

## Usage

```sh
python keymap_docgen.py <keymap_file> [<layer_name> ...] [-l layout.json] [-o output.xlsx]
```

- `<keymap_file>` — path to the `.keymap`.
- `<layer_name> ...` — zero or more layers to render. Omit to render **all**
  layers in definition order.
- `-l, --layout` — path to the physical-layout JSON (see below). If omitted, the
  tool looks for `<keymap_file>`'s name with a `.json` suffix.
- `-o, --output` — output `.xlsx` path. A `.html` of the same base name is always
  written next to it. Defaults to `<layer>_keymap.xlsx` (single layer) or
  `keymap.xlsx`.

```sh
# All layers, explicit layout, both KEYMAP.xlsx and KEYMAP.html:
python keymap_docgen.py config/MyBoard.keymap -l tools/MyBoard.layout.json -o KEYMAP.xlsx

# Only specific layers:
python keymap_docgen.py config/MyBoard.keymap DEFAULT LOWER -l tools/MyBoard.layout.json -o KEYMAP.xlsx
```

## Layout JSON schema

A ZMK-style physical-layout object. The tool reads the `default_layout` (or the
first layout if there is no `default_layout`):

```json
{
  "layouts": {
    "default_layout": {
      "layout": [
        { "x": 0, "y": 0, "label": "Q" },
        { "x": 1, "y": 0, "label": "W" }
      ]
    }
  }
}
```

Per entry:

| field   | required | meaning                                                            |
|---------|----------|-------------------------------------------------------------------|
| `x`     | yes      | column position (any numeric scale — logical slots or millimetres) |
| `y`     | yes      | row position (smaller = higher)                                    |
| `label` | no       | the key's DEFAULT-layer identity shown in the docs (e.g. `Q`)      |

Rules:

- **Order matters.** `layout[i]` describes keymap binding `i`; the array order
  must match the binding order in every layer.
- **Rows** are the distinct `y` values (top to bottom); **columns** are the
  distinct `x` values (left to right).
- **The split gap is auto-detected** at the widest horizontal `x` gap and shown
  as a blank separator column. Extra ZMK fields (`row`, `col`, `r`, …) are
  ignored.
- If the layout is missing or its key count doesn't match the bindings, the tool
  falls back to a single keymap-order row and labels default to `pos N`.

See [`example/`](example/) for a complete, runnable 15-key sample:

```sh
cd example
python ../keymap_docgen.py sample.keymap -l sample.layout.json -o /tmp/sample.xlsx
```

## Using it in a keyboard repo

Vendor this folder at `tools/keymap-docgen/` and keep your board's layout JSON as
a **sibling** (outside the folder), e.g. `tools/MyBoard.layout.json`. A GitHub
Action can then regenerate the docs on every keymap change:

```yaml
- run: python tools/keymap-docgen/keymap_docgen.py config/MyBoard.keymap \
         -l tools/MyBoard.layout.json -o KEYMAP.xlsx
```

## Extract to a standalone repo / git submodule

Because the script is keyboard-agnostic, you can lift this folder into its own
repository and reference it from each keyboard as a submodule:

```sh
# 1) Export this folder, with its history, onto a branch:
git subtree split --prefix=tools/keymap-docgen -b keymap-docgen-export

#    Create an empty GitHub repo (e.g. youruser/zmk-keymap-docgen), then:
git push git@github.com:youruser/zmk-keymap-docgen.git keymap-docgen-export:main

# 2) In each keyboard repo, swap the vendored copy for the submodule:
git rm -r tools/keymap-docgen
git commit -m "chore: replace vendored keymap-docgen with submodule"
git submodule add https://github.com/youruser/zmk-keymap-docgen tools/keymap-docgen
git commit -m "chore: add keymap-docgen submodule"
```

The keyboard's `tools/<board>.layout.json` stays put (it's a sibling, not part of
the submodule), and the workflow already checks out submodules
(`actions/checkout` with `submodules: recursive`), so CI keeps working unchanged.

## License

MIT — see [LICENSE](LICENSE).
