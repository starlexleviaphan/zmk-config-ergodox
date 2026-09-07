import sys
import re
import json
import os
import urllib.request

# QMK to ZMK Keycode Dictionary
QMK_TO_ZMK = {
    "KC_NO": "&none",
    "KC_TRANSPARENT": "&trans",
    "KC_TRNS": "&trans",
    
    # Letters
    **{f"KC_{c}": f"&kp {c}" for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
    # Numbers
    **{f"KC_{i}": f"&kp N{i}" for i in range(10)},
    # Function keys
    **{f"KC_F{i}": f"&kp F{i}" for i in range(1, 25)},
    
    # Punctuation & Symbols
    "KC_ENTER": "&kp RET",
    "KC_ENT": "&kp RET",
    "KC_ESCAPE": "&kp ESC",
    "KC_ESC": "&kp ESC",
    "KC_BSPC": "&kp BSPC",
    "KC_TAB": "&kp TAB",
    "KC_SPACE": "&kp SPACE",
    "KC_SPC": "&kp SPACE",
    "KC_MINUS": "&kp MINUS",
    "KC_MINS": "&kp MINUS",
    "KC_EQUAL": "&kp EQUAL",
    "KC_EQL": "&kp EQUAL",
    "KC_LBRC": "&kp LBKT",
    "KC_RBRC": "&kp RBKT",
    "KC_BSLS": "&kp BSLH",
    "KC_NONUS_HASH": "&kp NONUS_HASH",
    "KC_SCLN": "&kp SEMI",
    "KC_QUOTE": "&kp SQT",
    "KC_QUOT": "&kp SQT",
    "KC_GRAVE": "&kp GRAVE",
    "KC_GRV": "&kp GRAVE",
    "KC_COMMA": "&kp COMMA",
    "KC_COMM": "&kp COMMA",
    "KC_DOT": "&kp DOT",
    "KC_SLASH": "&kp FSLH",
    "KC_SLSH": "&kp FSLH",
    "KC_TILDE": "&kp TILDE",
    "KC_TILD": "&kp TILDE",
    "KC_EXLM": "&kp EXCL",
    "KC_AT": "&kp AT",
    "KC_HASH": "&kp HASH",
    "KC_DLR": "&kp DLLR",
    "KC_PERC": "&kp PRCNT",
    "KC_CIRC": "&kp CARET",
    "KC_AMPR": "&kp AMPS",
    "KC_ASTR": "&kp STAR",
    "KC_LPRN": "&kp LPAR",
    "KC_RPRN": "&kp RPAR",
    "KC_UNDS": "&kp UNDER",
    "KC_PLUS": "&kp PLUS",
    "KC_LCBR": "&kp LBRC",
    "KC_RCBR": "&kp RBRC",
    "KC_PIPE": "&kp PIPE",
    "KC_COLN": "&kp COLON",
    "KC_DQUO": "&kp DQT",
    "KC_LT": "&kp LT",
    "KC_GT": "&kp GT",
    "KC_QUES": "&kp QMARK",

    # Modifiers
    "KC_LCTRL": "&kp LCTRL",
    "KC_LEFT_CTRL": "&kp LCTRL",
    "KC_LSHIFT": "&kp LSHFT",
    "KC_LEFT_SHIFT": "&kp LSHFT",
    "KC_LALT": "&kp LALT",
    "KC_LEFT_ALT": "&kp LALT",
    "KC_LGUI": "&kp LGUI",
    "KC_LEFT_GUI": "&kp LGUI",
    "KC_RCTRL": "&kp RCTRL",
    "KC_RIGHT_CTRL": "&kp RCTRL",
    "KC_RSHIFT": "&kp RSHFT",
    "KC_RIGHT_SHIFT": "&kp RSHFT",
    "KC_RALT": "&kp RALT",
    "KC_RIGHT_ALT": "&kp RALT",
    "KC_RGUI": "&kp RGUI",
    "KC_RIGHT_GUI": "&kp RGUI",
    "KC_CAPS": "&kp CLCK",
    "KC_CAPSLOCK": "&kp CLCK",

    # Navigation & Editing
    "KC_INSERT": "&kp INS",
    "KC_INS": "&kp INS",
    "KC_DELETE": "&kp DEL",
    "KC_DEL": "&kp DEL",
    "KC_RIGHT": "&kp RIGHT",
    "KC_LEFT": "&kp LEFT",
    "KC_DOWN": "&kp DOWN",
    "KC_UP": "&kp UP",
    "KC_PAGE_UP": "&kp PG_UP",
    "KC_PGUP": "&kp PG_UP",
    "KC_PAGE_DOWN": "&kp PG_DN",
    "KC_PGDN": "&kp PG_DN",
    "KC_HOME": "&kp HOME",
    "KC_END": "&kp END",
    "KC_PRINT_SCREEN": "&kp PSCRN",
    "KC_PSCR": "&kp PSCRN",
    "KC_PAUSE": "&kp PAUSE_BREAK",
    "KC_APPLICATION": "&kp K_APP",
    "KC_APP": "&kp K_APP",

    # Media & Audio
    "KC_AUDIO_MUTE": "&kp C_MUTE",
    "KC_MUTE": "&kp C_MUTE",
    "KC_AUDIO_VOL_UP": "&kp C_VOL_UP",
    "KC_VOLU": "&kp C_VOL_UP",
    "KC_AUDIO_VOL_DOWN": "&kp C_VOL_DN",
    "KC_VOLD": "&kp C_VOL_DN",
    "KC_MEDIA_NEXT_TRACK": "&kp C_NEXT",
    "KC_MNXT": "&kp C_NEXT",
    "KC_MEDIA_PREV_TRACK": "&kp C_PREV",
    "KC_MPRV": "&kp C_PREV",
    "KC_MEDIA_PLAY_PAUSE": "&kp C_PP",
    "KC_MPLY": "&kp C_PP",
    "KC_MEDIA_STOP": "&kp C_STOP",
    "KC_MSTP": "&kp C_STOP",

    # Bootloader / Reset
    "RESET": "&bootloader",
    "QK_BOOT": "&bootloader",
}

# Permutation mapping: our key index (0..75) -> Oryx key index (0..75)
OUR_TO_ORYX = [
    # Row 0 (0..13)
    0, 1, 2, 3, 4, 5, 6,            38, 39, 40, 41, 42, 43, 44,
    # Row 1 (14..27)
    7, 8, 9, 10, 11, 12, 13,        45, 46, 47, 48, 49, 50, 51,
    # Row 2 (28..41)
    14, 15, 16, 17, 18, 19, 26,     58, 52, 53, 54, 55, 56, 57,
    # Row 3 (42..53)
    20, 21, 22, 23, 24, 25,         59, 60, 61, 62, 63, 64,
    # Row 4 (54..63)
    27, 28, 29, 30, 31,             65, 66, 67, 68, 69,
    # Row 5 (64..67 - Thumbs upper 1u)
    32, 33,                         70, 71,
    # Row 6 (68..73 - Thumbs middle 2u & 1u)
    34, 35, 36,                     72, 73, 74,
    # Row 7 (74..75 - Thumbs lower 1u)
    37,                             75
]

def parse_oryx_key(key_obj):
    """Converts an Oryx key JSON object to a ZMK binding string."""
    tap = key_obj.get("tap") or {}
    hold = key_obj.get("hold") or {}

    t_code = tap.get("code") or ""
    h_code = hold.get("code") or ""
    t_layer = tap.get("layer")
    h_layer = hold.get("layer")

    # Layer activations
    if t_code in ("TO", "TG", "DF"):
        target_layer = t_layer if t_layer is not None else 0
        return f"&to {target_layer}" if t_code in ("TO", "DF") else f"&tog {target_layer}"
    if t_code == "MO":
        target_layer = t_layer if t_layer is not None else 1
        return f"&mo {target_layer}"
    if h_code == "MO" or h_layer is not None:
        target_layer = h_layer if h_layer is not None else 1
        tap_zmk = QMK_TO_ZMK.get(t_code, f"&kp {t_code.replace('KC_', '')}")
        tap_arg = tap_zmk.replace("&kp ", "")
        return f"&lt {target_layer} {tap_arg}"
    
    # Mod-tap (e.g. MT, LCTL_T, etc.)
    if h_code:
        mod_zmk = QMK_TO_ZMK.get(h_code, h_code.replace("KC_", ""))
        mod_arg = mod_zmk.replace("&kp ", "")
        tap_zmk = QMK_TO_ZMK.get(t_code, f"&kp {t_code.replace('KC_', '')}")
        tap_arg = tap_zmk.replace("&kp ", "")
        return f"&mt {mod_arg} {tap_arg}"

    # Simple tap key
    if t_code in QMK_TO_ZMK:
        return QMK_TO_ZMK[t_code]
    elif t_code.startswith("KC_"):
        clean_code = t_code[3:]
        return f"&kp {clean_code}"
    elif not t_code or t_code == "NONE":
        return "&none"
    else:
        return f"&kp {t_code}"

def fetch_oryx_layout(hash_id):
    query = """
    query getLayout($hashId: String!, $revisionId: String!, $geometry: String) {
      layout(hashId: $hashId, geometry: $geometry, revisionId: $revisionId) {
        title
        revision {
          layers {
            position
            title
            keys
          }
        }
      }
    }
    """
    req = urllib.request.Request(
        "https://oryx.zsa.io/graphql",
        data=json.dumps({
            "query": query,
            "variables": {"hashId": hash_id, "geometry": "ergodox-ez", "revisionId": "latest"}
        }).encode("utf-8"),
        headers={"Content-Type": "application/json", "User-Agent": "Mozilla/5.0"}
    )
    with urllib.request.urlopen(req) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    
    if "errors" in data:
        raise ValueError(f"Oryx API Error: {data['errors']}")
    layout_data = data.get("data", {}).get("layout")
    if not layout_data:
        raise ValueError(f"Layout with hash '{hash_id}' not found.")
    return layout_data

def format_layer_block(name, bindings, layout_spec):
    table = {}
    for i, code in enumerate(bindings):
        r = layout_spec[i].get("row", 0)
        c = layout_spec[i].get("col")
        if r not in table:
            table[r] = {}
        col_idx = c if c is not None else len(table[r])
        table[r][col_idx] = code

    max_row = max(table.keys())
    max_col = max(max(row_dict.keys()) for row_dict in table.values())

    lines = []
    lines.append(f"        {name} {{")
    lines.append("            bindings = <")
    for r in range(max_row + 1):
        if r not in table:
            continue
        row_str = []
        for c in range(max_col + 1):
            val = table[r].get(c, "")
            row_str.append(f"{val:12s}" if val else "            ")
        lines.append("            " + " ".join(row_str).rstrip())
    lines.append("            >;")
    lines.append("        };")
    return "\n".join(lines)

def convert_oryx_to_zmk(hash_or_url, apply_to_file=False, output_path=None, selected_layers=None, print_only=False, info_only=False):
    # Extract hash from URL if full URL is given
    m = re.search(r"layouts/([a-zA-Z0-9_-]+)", hash_or_url)
    hash_id = m.group(1) if m else hash_or_url.strip()

    print(f"Fetching layout from Oryx: {hash_id}...")
    layout_data = fetch_oryx_layout(hash_id)
    title = layout_data.get("title", "Imported Oryx Layout")
    layers = layout_data["revision"]["layers"]
    layer_names = [l.get("title") or f"layer_{l.get('position')}" for l in layers]
    print(f"Found layout: '{title}' ({len(layers)} layers: {layer_names})")

    if info_only:
        print("\nLayers in layout:")
        for l in layers:
            print(f"  Layer {l.get('position')}: {l.get('title') or '(untitled)'}")
        return

    # Read layout spec for pretty formatting
    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    info_path = os.path.join(root_dir, "config", "info.json")
    with open(info_path, "r", encoding="utf-8") as f:
        info_json = json.load(f)
    layout_spec = info_json["layouts"]["default_layout"]["layout"]

    layer_blocks = []
    for layer in layers:
        pos = layer["position"]
        if selected_layers is not None and pos not in selected_layers:
            continue

        raw_title = layer.get("title") or f"layer_{pos}"
        l_title = re.sub(r"[^a-zA-Z0-9_]", "_", raw_title).lower().strip("_")
        if not l_title or l_title.isdigit():
            l_title = f"layer_{pos}"

        oryx_keys = layer["keys"]
        zmk_bindings = []
        for our_idx in range(76):
            oryx_idx = OUR_TO_ORYX[our_idx]
            k_obj = oryx_keys[oryx_idx] if oryx_idx < len(oryx_keys) else {}
            zmk_bindings.append(parse_oryx_key(k_obj))

        layer_blocks.append(format_layer_block(l_title, zmk_bindings, layout_spec))

    if not layer_blocks:
        print("No matching layers found.")
        return

    keymap_snippet = "\n\n".join(layer_blocks)

    if print_only:
        print("\n================== CONVERTED ZMK LAYERS ==================\n")
        print(keymap_snippet)
        print("\n==========================================================\n")
        return

    if output_path:
        out_abs = os.path.abspath(output_path)
        with open(out_abs, "w", encoding="utf-8") as f:
            f.write(keymap_snippet + "\n")
        print(f"Saved {len(layer_blocks)} layer(s) to {out_abs}")

    if apply_to_file:
        keymap_path = os.path.join(root_dir, "config", "ergodox.keymap")
        with open(keymap_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Replace keymap body
        new_keymap_body = "    keymap {\n        compatible = \"zmk,keymap\";\n\n" + keymap_snippet + "\n    };"
        new_content = re.sub(r"    keymap\s*\{[^}]*compatible = \"zmk,keymap\";.*?\n    \};", new_keymap_body, content, flags=re.DOTALL)

        with open(keymap_path, "w", encoding="utf-8") as f:
            f.write(new_content)
        
        # Also sync to shield keymap
        shield_km_path = os.path.join(root_dir, "boards", "shields", "ergodox", "ergodox.keymap")
        with open(shield_km_path, "w", encoding="utf-8") as f:
            f.write(new_content)

        print(f"Applied layout '{title}' ({len(layer_blocks)} layers) directly to config/ergodox.keymap!")
    elif not output_path and not print_only:
        print("\n--- Converted Keymap Layers Preview (Pass --print to view all, --apply to save) ---")
        for lb in layer_blocks:
            print(lb[:250] + "...\n")

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Import layouts from ZSA Oryx (Ergodox EZ) directly into ZMK")
    parser.add_argument("url_or_hash", nargs="?", default="default", help="Oryx layout URL or hash ID (e.g. bO05X)")
    parser.add_argument("--apply", action="store_true", help="Apply directly to config/ergodox.keymap")
    parser.add_argument("--layer", type=int, action="append", dest="layers", help="Extract only specific layer index (can be specified multiple times, e.g. --layer 0 --layer 2)")
    parser.add_argument("--output", "-o", type=str, help="Save converted ZMK layer snippet to a file")
    parser.add_argument("--print", "-p", action="store_true", help="Print converted ZMK code to console")
    parser.add_argument("--info", "-i", action="store_true", help="List available layers in the layout")
    
    args = parser.parse_args()
    convert_oryx_to_zmk(
        args.url_or_hash,
        apply_to_file=args.apply,
        output_path=args.output,
        selected_layers=args.layers,
        print_only=args.print,
        info_only=args.info
    )
