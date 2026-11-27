# Highlight rules

WinSSH colors keywords in remote output. Rules live in one file:

```text
%APPDATA%\WinSSH\highlight.json
```

(`%WINSSH_CONFIG_DIR%\highlight.json` when that variable is set, e.g. for a portable setup.)

Without the file, WinSSH uses its built-in `default` rule set (the same keywords as WinSerial).

## Quick start

```powershell
winssh --init-rules                     # create a commented starter file
notepad "$env:APPDATA\WinSSH\highlight.json"
winssh --check-rules                    # validate and list rule sets with a color preview
winssh --test-rules "ERR fc-io.service link down at 10.0.0.108" --highlight default,journal
winssh --highlight default,journal root@10.0.0.108
```

While a session is open, WinSSH checks the file every second. When you save it, the session
shows `[winssh] highlight rules reloaded: N rules` (plus any errors) and uses the new rules at once.

## File format

The file is JSON. Comments (`//` and `/* */`) are allowed.

```jsonc
{
  "ruleSets": {
    "default": [
      { "name": "errors", "keywords": ["error", "failed", "denied"], "style": "red" },
      { "name": "ipv4", "regex": "\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b", "style": "yellow" }
    ],
    "journal": [
      { "name": "priority-error", "regex": "\\b(ERR|CRIT|ALERT|EMERG)\\b", "caseSensitive": true,
        "style": { "fg": "white", "bg": "red", "bold": true } }
    ]
  }
}
```

### Rule sets

- `ruleSets` is an object of named arrays of rules.
- Sessions use the set `default` unless `--highlight a,b,...` says otherwise.
- A `default` set in the file **replaces** the built-in one. Other names are added.
- With several sets, their rules are joined in the order given on the command line.

### Rule fields

| Field | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | `<set>[<index>]` | Shown by `--check-rules` and in error messages |
| `keywords` | array of strings | — | Whole words or phrases, matched literally (`ip address` works) |
| `regex` | string | — | A [PCRE2](https://www.pcre.org/current/doc/html/pcre2pattern.html) regular expression |
| `caseSensitive` | bool | `false` | Match case exactly |
| `enabled` | bool | `true` | `false` keeps the rule in the file but turns it off |
| `style` | string or object | no change | See below |

Each rule needs `keywords` **or** `regex`, not both.

In JSON, a backslash must be doubled: the regex `\bfc-\w+\b` is written `"\\bfc-\\w+\\b"`.

### Style

`"style": "red"` is short for `"style": { "fg": "red" }`. The object form:

| Field | Value |
|---|---|
| `fg`, `bg` | Color: `black` `red` `green` `yellow` `blue` `magenta` `cyan` `white`, `bright-<color>`, `gray`; `"#rrggbb"`; or a number `0`–`255` (256-color palette) |
| `bold`, `underline`, `italic` | `true` / `false` |

Named colors follow your Windows Terminal color scheme. `#rrggbb` and numbers are fixed colors.

## How matching works

- Only printable text is matched, never escape sequences. The server's own colors are restored
  after each highlighted word.
- When two rules match the same text, the **earlier rule wins**. Put specific rules first:
  `link down` before `down`.
- Matches never span a line break.
- Highlighting pauses while a full-screen app (vim, less, htop) is open.
- `Ctrl+]` then `f` turns highlighting off and on for the current session; `--no-highlight` starts
  without it.

## Errors

A rule with a mistake (bad regex, unknown color, wrong type) is skipped; the other rules still
work. Errors show when a session starts, when the file is reloaded, and in `winssh --check-rules`
(which exits with code 1 when there are problems). Unknown field names (typos such as `colour`)
are warnings. If the file is not valid JSON, the built-in rules are used.

A regex that takes too long on a line (catastrophic backtracking, e.g. `(a+)+$`) stops at a
limit and leaves the rest of that line plain instead of freezing the session.
