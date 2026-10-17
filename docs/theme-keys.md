# theme-keys.md - Phase 4 themes and keybinding config.
# Module: docs (Markdown).
# Owns: file formats, search paths, and x64dbg-ish default keys for the TUI.
# Depends: re_theme, re_keys, docs/examples/*.ini.

## Search order

1. Env: `ANTINOMIA_THEME` / `ANTINOMIA_KEYS` (full path)
2. CWD: `theme.json` then `theme.ini` (same for `keys.*`)
3. Config dir: `$XDG_CONFIG_HOME/antinomia/` or `~/.config/antinomia/`
   (Windows: `%APPDATA%/antinomia/`)

Missing files keep built-in defaults. GUI logs `theme default` / `keys x64dbg-ish`.

## Theme

Style values are **SGR parameter bodies** (e.g. `1;36`), installed as `ESC[<sgr>m`.
Full CSI escapes starting with ESC are also accepted.

INI sample: [`docs/examples/theme.ini`](examples/theme.ini).

JSON shape:

```json
{
  "schema": "antinomia/1",
  "kind": "theme",
  "name": "default",
  "styles": {
    "accent": "1;36",
    "bad": "1;31",
    "title": "1;7"
  }
}
```

Slots: `title`, `head`, `label`, `accent`, `good`, `warn`, `bad`, `hover`,
`press`, `kw`/`keyword`, `type`, `cmt`/`comment`, `str`/`string`, `num`/`number`,
`call`.

## Keybindings

Defaults keep TUI chrome (`Tab` / `S-Tab` focus, `q`/`Esc` quit) and map x64dbg-ish
debug keys that do not steal arrows or Ctrl-C:

| Key | Action |
|---|---|
| F9 / F5 | run (continue) |
| F7 | step into |
| F8 | step over |
| F2 | toggle breakpoint at RIP |
| F12 | pause |

INI sample: [`docs/examples/keys.ini`](examples/keys.ini).

JSON shape:

```json
{
  "schema": "antinomia/1",
  "kind": "keys",
  "bindings": [
    {"key": "F9", "action": "run"},
    {"key": "F7", "action": "step_into"},
    {"key": "F8", "action": "step_over"},
    {"key": "F2", "action": "toggle_bp"},
    {"key": "F12", "action": "pause"},
    {"key": "Tab", "action": "focus_next"},
    {"key": "q", "action": "quit"}
  ]
}
```

Actions: `quit`, `focus_next`, `focus_prev`, `run`, `step_into`, `step_over`,
`pause`, `toggle_bp`. Modifiers: `Ctrl+`, `Alt+`, `Shift+` prefixes on the key
token (e.g. `Ctrl+F9`).
