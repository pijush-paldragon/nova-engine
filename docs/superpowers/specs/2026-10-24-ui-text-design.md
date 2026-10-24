# Nova text (UI sub-project B) — design

Status: approved in conversation 2026-10-24; written spec pending review.
Builds on sub-project A (`docs/superpowers/specs/2026-10-24-ui-core-design.md`), which reserved
label `wrap`, `@key` text and rich markup for this spec.

## 1. Intent

Games ship in Latin, Cyrillic/Greek, CJK and right-to-left/complex scripts (Arabic, Hebrew,
Persian, Urdu, Hindi, Thai). They need translated strings that can switch at runtime, text that
wraps and shrinks to fit long translations, and rich text with inline icons
("Buy for [icon=sprites/coin] 250").

**Success:** Brick Breaker runs in English, German, Japanese and Arabic (mirrored HUD) with every
string from translation files, a language picker in its settings popup, and a rich "Best" line with
an inline icon; the game template ships `en.json`; every test in §8 passes, including zero
allocations for a keyed HUD label changing every frame.

**Non-goals:** text input/caret/IME (sub-project D), vertical CJK text, colour emoji fonts
(plutosvg stays off), full mixed-direction (bidi) layout *inside rich text* (plain text has it).

## 2. Global constraints

- C++20, no exceptions/RTTI; zero heap allocations in steady-state gameplay frames. Loading a
  language, switching language and first layout of a label are setup work (`LoadingScope`).
- Missing keys, fonts, icons or bad markup never crash: dev builds log once, release builds fall
  back silently.
- Assets go through the existing loaders (`openAsset`, `Assets`), so release builds read language
  files and fonts from `game.pak`.
- Public API stays SDL-free; HarfBuzz and SDL_ttf stay private to the engine.

## 3. Approach

SDL_ttf with its vendored HarfBuzz enabled does shaping, fallback fonts, direction/script and
plain-text wrapping. Nova adds localization, a BBCode markup parser and its own line layout for
rich text (runs of styled text and inline icons). Rich text inside a right-to-left paragraph lays
its runs right to left; mixing directions inside one *rich* paragraph is not supported (plain text
supports it fully).

## 4. Shaping and fonts

- `cmake/NovaThirdParty.cmake`: `SDLTTF_HARFBUZZ ON` (vendored); the
  `third_party/SDL_ttf/external/harfbuzz` submodule is checked out at SDL_ttf's pinned commit.
- **Language fonts:** a language file may list `"fonts"` and `"boldFonts"` (asset paths). While
  that language is active they are attached with `TTF_AddFallbackFont` behind every style font, in
  order, so glyphs the game font has keep its look and the rest come from the fallbacks.
- **Bold:** a style may set `"boldFont"` (theme.json); `[b]` uses it, else SDL_ttf's synthetic
  bold (`TTF_STYLE_BOLD`). `[i]` uses synthetic italic.
- **Direction, script, language:** the active language sets the base direction (`ar`, `he`, `fa`,
  `ur` → right-to-left), the script (`TTF_StringToTag`, from the language) and the BCP-47 tag
  (`TTF_SetFontLanguage`) on every font it uses.
- **Font cache key** becomes `path@size#language`; switching language loads that language's chain;
  fonts of the previous language are released with their scene scope as today.
- Games ship only the fonts of languages they include; docs explain subsetting large fonts
  (e.g. `pyftsubset`).

## 5. Localization

- **Files:** `assets/text/<lang>.json` (lang = BCP-47 like `en`, `pt-BR`, `ja`, `ar`):

  ```json
  { "language": "en", "fonts": [], "boldFonts": [],
    "strings": { "menu": { "play": "Play" },
                 "hud": { "score": "Score {score}",
                          "lives": { "one": "{count} life", "other": "{count} lives" } } } }
  ```

  Nested objects flatten to dotted keys; an object whose keys are only plural categories
  (`zero one two few many other`) is a plural entry. JSON comments/trailing commas allowed (yyjson).
- **API** (`nova/text/text.hpp`, reached with `Scene::text().lang()`):
  - `std::string_view get(std::string_view key) const;`
  - `size_t format(std::span<char> out, std::string_view key, std::initializer_list<Arg> args) const;`
    and `plural(std::span<char> out, std::string_view key, i64 count, args)` — write into caller
    buffers (no allocation); `Arg` = `{name, value}` with value `i64`, `f64` or `string_view`.
  - `bool setLanguage(std::string_view lang);` `std::string_view language() const;`
    `std::span<const std::string> available() const;` `bool rightToLeft() const;`
    `u32 generation() const;` (bumped by every switch or reload).
- **Plural rules (CLDR):** one/other (en, de, es, it, pt, nl, sv, tr, el, …); French-style
  (fr, pt-BR: 0 and 1 are one); one/few/many (ru, uk, be, pl, cs, sk — with each language's rule);
  zero/one/two/few/many/other (ar); other only (ja, zh, ko, th, id, vi). Unknown → one/other.
- **Placeholders:** `{name}`; a missing argument stays literal; `{{` is a literal brace.
- **Keys in text:** text starting with `@` is a key (`"text": "@menu.play"`); `@@` is a literal `@`.
  `LabelRef::setKey(key, args)` and `TextLabel::setKey(key, args)` keep the key and arguments and
  re-render on language change; `setText` stays literal.
- **Language choice at startup:** saved `language` (SaveStore) → first of
  `SDL_GetPreferredLocales()` with a file (exact `pt-BR`, then `pt`) → `en`. `setLanguage` saves the
  choice. Dev builds: `NOVA_LANG=<lang>` overrides.
- **Fallbacks:** missing key in the language → `en` → the key as `[menu.play]`; dev builds log each
  missing key once.
- **Live reload:** the active language file reloads like layouts (dev desktop builds).
- **Tool:** `tools/check_strings.py <assets/text>` lists keys missing or extra per language versus
  `en.json`, plural categories a language needs but lacks, and placeholder mismatches; exit 1 on
  problems.

## 6. Text layout

- **New fields** (UI `UiSpec`/layout files for labels and buttons; same on `TextLabel`):
  `wrap` (bool), `maxLines` (int, 0 = no limit; overflow ends with "…"), `overflow`
  (`"clip"` default | `"shrink"`), `minScale` (default 0.6), `lineSpacing` (default 1.0),
  `textAlign` gains `"start"` / `"end"`, `mirror` (UI elements, default true).
- **Plain text** (no markup) uses one SDL_ttf text object: `TTF_SetTextWrapWidth` for wrapping
  (HarfBuzz/SDL_ttf break rules), direction/script from the language.
- **Shrink:** at layout time, the font size steps down by 2 design px until the text fits the box
  (respecting wrap/maxLines) or reaches `minScale × size`; each size is a cached font.
- **Measuring:** wrapping labels are measured at their assigned width; UI layout gains a second
  measure step for wrapping labels (width first, then fit height).
- **RTL screens:** when the language is right-to-left, `row` layouts reverse child order and
  horizontal anchors, `start`/`end` alignment and `justify` mirror, unless an element sets
  `"mirror": false`. Sprites never flip automatically; an image may name `"spriteRtl"`.
- **Re-layout** only when the text, its arguments, width, style or language changes. Re-rendering
  text no longer than before and switching between cached shrink sizes don't allocate.

## 7. Rich text

- A label is rich when its (translated) text contains markup.
- **Tags:** `[b]`, `[i]`, `[color=#rrggbb(aa)|name]`, `[size=<scale>]`, `[style=<style>]` (font,
  size, colour from a theme style), `[icon=<atlas>/<region> scale=<s>]` (sprite sized to the line
  height × scale, on the baseline). Tags nest; `[[` is a literal `[`. Named colours come from a
  `"colors"` object in theme.json. Unknown or unclosed tags render as plain text and log once (dev).
- **Placeholder safety:** `{name}` values are substituted before parsing and escaped, so a
  player-provided name can't inject tags.
- **Layout:** markup → runs (styled text or icon) → pieces at break opportunities (spaces; between
  CJK ideographs/kana/hangul; after `-` `–` `—`), with kinsoku rules (no line starting with
  `。、，．・：；？！）」』】〕ー` or ending with `（「『【〔`). Lines fill greedily; wrap, maxLines,
  shrink and alignment as §6. Each text piece is a shaped SDL_ttf text object (HarfBuzz, fallbacks).
- **RTL:** in a right-to-left language each line's pieces are placed right to left; one rich
  paragraph cannot mix directions (documented).
- Rich labels cache their pieces; changing text re-parses (setup-cost). Steady state with unchanged
  text never allocates.

## 8. Testing

Headless doctest with small OFL test fonts (subsets, a few KB each) under
`engine/tests/data/fonts/` and test language files under `engine/tests/data/text/`:

- **Localization:** nested and plural keys; every plural rule against CLDR sample numbers;
  placeholders and `{{`; `@` keys in layouts and `@@`; `setKey` re-render on switch; fallback to
  `en` then `[key]` (logged once); preferred-locale matching (`pt-BR` → `pt`); saved choice;
  `NOVA_LANG`; live reload of the language file.
- **Shaping:** Arabic shapes (glyph count < codepoints, laid out right to left); Japanese renders
  through a fallback font with no missing glyphs; Devanagari clusters with HarfBuzz.
- **Layout:** wrap at width; fit height grows with lines; `maxLines` ellipsis; shrink to
  `minScale`; `start`/`end` and row/anchor mirroring in RTL; `mirror: false`; CJK breaking and
  kinsoku.
- **Rich text:** every tag; nesting; `[[`; unknown/unclosed tags; placeholder escaping; icon size
  and baseline; RTL run order.
- **Tool:** `tools/test_check_strings.py` for `check_strings.py`.
- **Performance:** a keyed HUD label updated every frame allocates nothing; language switch runs in
  a setup scope.
- **Devices:** Brick Breaker screenshots in en, de, ja, ar on macOS, the iOS Simulator and the
  Android emulator.

## 9. Rollout

- Brick Breaker: all UI and world text through keys; `en`, `de`, `ja`, `ar` files with subset
  Noto fonts for ja/ar; a language picker in the settings popup (a row of buttons); the menu's best
  line as rich text with an icon.
- Template: `assets/text/en.json`; the title screen uses `@title.start`.
- Docs: "Localization and text" in `docs/NEW_GAME.md` (files, keys, plurals, fonts and subsetting,
  rich text, RTL); README mention.
