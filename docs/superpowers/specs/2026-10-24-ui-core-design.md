# Nova UI core (sub-project A) — design

Status: approved in conversation 2026-10-24; written spec pending review.

## 1. Intent

Nova games need real game UI: HUDs, menus, settings, popups, and later shops, inventories, level
select, leaderboards and text entry. The full UI system is split into four sub-projects, built in
order, each with its own spec, plan and review:

| # | Sub-project | Delivers |
|---|---|---|
| **A** | **UI core (this spec)** | element tree, layout, styles/themes, touch handling, core widgets, popups, transitions, JSON layout files with live reload |
| B | Text | localization (string tables, runtime language switch), wrapping and shrink-to-fit, rich text with inline icons, right-to-left and CJK via HarfBuzz + fallback fonts |
| C | Lists and data views | clipping, scroll views with momentum, recycled lists/grids, tabs, pages |
| D | Text entry | text fields, caret/selection, on-screen keyboard and IME, keyboard avoidance |

A must leave room for B–D without API breaks: labels reserve the fields B needs (`wrap`, `@key`
text, rich markup), and the element model and draw order must accept clipping containers (C) and
focusable text fields (D).

**Success:** Brick Breaker's menu, HUD, pause and game-over screens are rebuilt on the new UI, plus a
new settings popup (volume slider, sound toggle, saved); a new game from `tools/new_game.py` starts
with a working layout-file menu; every test in §9 passes, including zero steady-state allocations.

**Non-goals for A:** everything in B–D; keyboard/gamepad focus navigation; screen-reader
accessibility; element rotation.

## 2. Global constraints

- C++20, no exceptions, no RTTI in engine and game code (as today).
- No heap allocation on the game thread during steady-state frames: `--fail-on-alloc` stays green
  with UI on screen and a label changing every frame. Loading and reloading run inside the existing
  `LoadingScope`.
- All coordinates are design coordinates (`GameConfig.designSize`), mapped to the screen by the
  existing `ScreenFit`; the root covers `Renderer2D::visibleRect()`; `Renderer2D::safeArea()` gives
  the insets.
- Assets are opened with `openAsset` (so release builds read layouts from `game.pak`).
- Missing ids, sprites, styles and bad values never crash: dev builds log one clear error, release
  builds fall back to defaults silently.
- Public headers under `engine/include/nova/ui/`; game code never sees SDL or yyjson.

## 3. Architecture

- **Module:** `nova/ui/` (public: `ui.hpp` with `Canvas`, element refs, option structs; private:
  `engine/src/ui/` for layout, drawing, input, loader, theme).
- **Ownership:** every `Scene` owns one `Canvas`, reached with `ui()` next to `world()`,
  `camera()`, etc. The canvas lives and dies with its scene.
- **Element store:** one pooled store per canvas, elements addressed by generational handles
  (`UiHandle`, same scheme as the engine's `HandlePool`), so a stale handle is detected, never
  dereferenced. Deleting an element deletes its subtree, applied at end of frame (like `World`).
- **Element kinds:** `Panel`, `Image`, `Label`, `Button`, `Toggle`, `Slider`, `ProgressBar`.
- **Common element properties:** optional string `id` (path ids such as `"settings/volume"` are
  allowed), `style`, `visible`, `enabled`, `opacity`, `scale`, `offset`, `blocksInput`. Opacity and
  scale multiply down the tree.
- **Assets:** textures, atlases, fonts and sounds come from the scene's `Assets` cache, so they are
  released with the scene. No raw pointers in the game-facing API.
- **Frame flow per scene, driven by `App` (game code never calls these):**
  1. *Input* (top scene only, before the scene's `onUpdate`): hit-test, update touch ownership,
     mark consumed pointers, queue events.
  2. *Callbacks*: queued events invoke bound callbacks at one safe point; structural changes they
     make (load, push/pop, delete, scene change) apply at end of frame.
  3. *Update*: element animations and layer transitions advance; layout re-runs only if dirty.
  4. *Draw*: after the scene's world sprites and world text, so the UI sits above its own scene;
     the next scene up draws over both.
- **Draw order:** a canvas is a stack of **UI layers** (the base screen, then each open popup). Each
  layer draws in two passes — all of its background/image sprites through the `SpriteBatch`, then
  all of its labels through the existing SDL_ttf renderer-text path — so text is always above its own
  button. Budget: about 2 draw calls per layer plus the backdrop.
- **Steady state allocation:** element storage, the draw list, the hit list and the event queue
  are reserved and reused; setting a label to text no longer than its current capacity does not
  allocate (same rule as `TextLabel`).

## 4. Layout

- **Size per axis:** `fixed` (pixels), `fill` (share of leftover space, `weight`, default 1) or
  `fit` (content size: measured text, sprite size, or children). Optional `min`/`max` clamp it.
  JSON accepts a number (fixed), `"fill"`, `"fill:2"` or `"fit"`.
- **Container modes** (`layout`):
  - `stack` (default): each child is placed by `anchor` (one of `top-left`, `top`, `top-right`,
    `left`, `center`, `right`, `bottom-left`, `bottom`, `bottom-right`; the same point on child and
    parent) plus `offset`. `fill` in stack mode fills the parent's content box.
  - `row` / `column`: children in sequence with `gap`, `padding` (one number or `[t, r, b, l]`),
    `justify` (`start`, `center`, `end`, `space-between`) and `align` (`start`, `center`, `end`,
    `stretch`).
- **Safe area:** `safeArea: true` on a container adds the safe-area insets to its padding.
- **Root:** always `visibleRect()`; anchors and `fill` adapt to any aspect ratio without stretching.
- **Dirty-driven:** layout runs only after resize/orientation change, safe-area change,
  show/hide, text/sprite/size/style change, add/remove, load or reload. Animated `offset`, `scale`
  and `opacity` apply at draw/hit-test time and never dirty layout.
- **Algorithm:** pass 1 bottom-up measures `fit` sizes; pass 2 top-down assigns boxes. Iterative
  (explicit stack), linear in element count, no allocation.
- **Pixel snapping:** final box edges round to whole device pixels (`pixelScale`).

## 5. Widgets, styles, themes

- **Built-in theme textures:** at startup the UI generates small rounded-rectangle textures (corner
  radii 0, 8, 16, 24, 32 design px; filled and 2-px outline) and a circle; drawn as nine-slice sprites
  tinted with style colours.
- **Sprite skins:** a style or element may use an atlas sprite (`"sprite": "ui/button_blue"`) with
  nine-slice insets (`"slice": [t, r, b, l]`; omitted = stretch whole sprite).
- **Styles** live in `assets/ui/theme.json` (optional; a built-in default theme applies without it).
  A style sets: `background` (`{"shape": "rounded", "radius": 16, "color": "#3b82f6"}` or
  `{"sprite": ..., "slice": ...}`), `outline` (color, width), `tint`, `padding`, `font`
  (`"fonts/Fredoka.ttf"`), `fontSize`, `textColor`, `clickSound`, and per-state overrides under
  `states.pressed|disabled|checked`. `"extends": "<style>"` inherits; element-level properties
  override the style. Colours are `#rrggbb` or `#rrggbbaa`.
- **Widgets:**
  - `Panel` — container, optional background. No clipping in A.
  - `Image` — sprite or texture, `tint`, `fit`: `stretch` | `contain` | `cover`.
  - `Label` — one line; style font/size/colour; `align` (`left`, `center`, `right`); `fit` size
    from the measured text. Reserved for B: `wrap`, text values starting with `@` (localization
    key), rich markup.
  - `Button` — background + optional `icon` (sprite) and/or `text`. Pressed state shows
    immediately and the button eases to 0.95 scale (back to 1 on release, 0.08 s); click fires on release inside; the press cancels when
    the finger moves more than 16 design px outside the button's box; disabled buttons ignore
    touches and draw with the `disabled` state (default: 50% opacity). Optional `clickSound`.
  - `Toggle` — `look`: `switch` | `checkbox`; `on`; `onChange(bool)`.
  - `Slider` — track, fill, knob; `min`, `max`, `step`, `value`; press anywhere on the track jumps
    there; `onChange(f32)` while dragging, `onCommit(f32)` on release.
  - `ProgressBar` — `value` 0–1, `direction` (`right`, `left`, `up`, `down`), optional
    `animate` seconds to ease toward the target value.
- **Touch rules:** a touch belongs to the first interactive element it lands on, topmost first,
  until release; multitouch (up to `Input::kMaxPointers`) works across elements. Touches on
  non-interactive UI pass through to gameplay unless the element sets `blocksInput: true`
  (popups' backdrops always block).

## 6. Events and C++ API

- **Typed references:** `ui().button(id)`, `label(id)`, `image(id)`, `toggle(id)`, `slider(id)`,
  `progress(id)`, `panel(id)`, `element(id)`. A missing id or wrong type logs once in dev builds
  (`ui: no button "plya" in ui/menu.json`) and returns an empty reference whose calls are no-ops.
- **Bindings:** `ui().onClick(id, fn)`, `toggle.onChange`, `slider.onChange` / `onCommit`,
  `popup.onClose`. Bindings are stored **by id** and re-applied after reload or re-load of the
  same file. Callbacks are stored once at bind time (allocation allowed then); invoking them never
  allocates.
- **Setters:** `setText`, `setSprite`, `setValue`, `setOn`, `setVisible`, `setEnabled`, `setStyle`,
  `setOpacity`, `setOffset`, `setScale`. Values set through the API are also remembered by id and
  re-applied after reload.
- **Animations:** `el.animate()` builder — `.scale(to, seconds)`, `.fade(to, seconds)`,
  `.move(offset, seconds)`, `.ease(Ease)`, `.delay(s)`, `.yoyo()`, `.loop()`, `.onDone(fn)`;
  uses the engine's existing `Ease` curves. A fixed pool of animation slots per canvas.
- **Code-built trees:** `ui().root().add<Panel>({...})` / `.add<Button>({...})` with designated-
  initializer option structs mirroring the JSON fields; file-built and code-built elements mix.
- **Gameplay input:** `Pointer` gains `bool consumedByUi`. `Input::tapped()` and
  `Input::primary()` ignore consumed pointers; `pointer(i)` still returns all of them.

## 7. Layout files and live reload

- **Location and format:** JSON under the game's `assets/ui/`. Fields mirror §4–§5:
  `type`, `id`, `style`, size/layout fields, widget fields, `children`, plus
  `"include": "<file>"` (insert another file's tree; other keys on the include element override the
  included root's) and root-level `enter`/`exit` transitions.
- **Parser:** yyjson (MIT, C), added as a pinned git submodule `third_party/yyjson` and built as a
  static library like the other third-party code. Parsing happens only at load/reload.
- **Validation:** unknown keys, wrong value types, unknown styles, missing sprites/fonts, unknown
  element types and duplicate ids each give one message naming file, element path and key
  (`ui/menu.json: children[2] (button "play"): unknown key "heigth"`); JSON syntax errors give line
  and column. Dev builds log them; release builds skip messages and use defaults.
- **Live reload (desktop dev builds only):** dev builds receive the game's source `assets/`
  directory as a compile-time path (`NOVA_DEV_ASSET_SOURCE_DIR`, defined only when `NOVA_DEV_TOOLS`
  and not mobile). Every 0.5 s the canvas compares the modification times of every file it loaded
  (layouts, includes, theme). On change: re-parse from the source directory, rebuild the tree,
  re-apply bindings and API-set values, keep player-changed toggle/slider values (by id), reload
  open popups too. On a parse or validation error, the previous UI stays and a red banner at the top
  of the screen shows the first error until a later save fixes it. Release and mobile builds never
  poll.

## 8. Popups and transitions

- **Popups:** `ui().push(file, PopupOptions{.dim = 0.6f, .closeOnBackdrop = false, .enter, .exit})`
  returns a `PopupRef` (`close()`, `onClose(fn)`, element lookups scoped to it); `ui().pop()` closes
  the top one. Each popup is a UI layer with a full-screen backdrop that dims and blocks all touches
  below it.
- **Back button:** Android back / Escape closes the top popup first; the scene's
  `input().backPressed()` is true only when no popup consumed it.
- **Transitions:** presets `none`, `fade`, `pop` (scale 0.85 → 1 with `OutBack`, fade in),
  `slide-up|down|left|right`; default duration 0.2 s; a layer ignores touches while it transitions.
- **Popup vs scene:** popups are for dialogs inside a scene; full pause/game-over scenes remain
  available when gameplay must stop. Brick Breaker uses both.

## 9. Testing

Headless doctest cases in `engine/tests` (software renderer, dummy audio):

- **Layout:** each anchor; row/column with gap, padding, every justify/align value; fill weights,
  fit, min/max; safe-area insets; a different aspect ratio; pixel snapping; dirty-only relayout
  (a frame with no changes does no layout work).
- **Touch:** topmost wins; ownership until release; drag-off cancel at 16 px; multitouch on two
  buttons; disabled ignores; `consumedByUi` hides UI taps from `tapped()`; non-interactive
  pass-through; popup backdrop blocks; back closes the top popup before the scene sees it; touches
  ignored during transitions.
- **Widgets:** button click on release only; toggle; slider drag, step, `onCommit`; progress
  animation.
- **Files:** every field parses; every validation message; include with overrides; missing id
  returns an empty ref without crashing; broken JSON on reload keeps the old UI and sets the banner;
  reload re-applies bindings, API values and player-changed values.
- **Performance:** a HUD with the score label changing every frame allocates zero times in steady
  state; Brick Breaker's menu draws within a fixed draw-call budget (asserted).
- **Devices:** screenshots of every Brick Breaker screen on macOS, the iOS Simulator (a notched
  iPhone) and the Android emulator.

## 10. Rollout

- Brick Breaker migrates to the UI system: menu (play, settings, best score), HUD (score, lives,
  pause button), pause popup (resume, settings, quit), game over, settings popup (volume slider,
  sound toggle, both saved with `SaveStore`).
- `templates/game` gains `assets/ui/menu.json` and `assets/ui/theme.json`, and its menu scene uses
  them.
- `docs/NEW_GAME.md` gains a "Building UI" section; `README.md` a short mention.
