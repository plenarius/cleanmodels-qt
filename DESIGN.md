---
name: cleanmodels-qt
description: Native desktop GUI for the cleanmodels NWN MDL toolchain
colors:
  log-info: '#3498db'
  log-success: '#27ae60'
  log-error: '#e74c3c'
  log-warning: '#e67e22'
  log-command: '#95a5a6'
  log-fix-applied: '#2ecc71'
  log-sev-error: '#e74c3c'
  log-sev-warning: '#f39c12'
  log-sev-info: '#7f8c8d'
  log-invalid-path: '#FF0000'
  viewport-bg: '#2E3340'
  viewport-grid: '#595966'
  viewport-reference: '#6699E6'
typography:
  body:
    fontFamily: 'system-ui'
    fontWeight: 400
  label:
    fontFamily: 'system-ui'
    fontWeight: 700
  log:
    fontFamily: 'ui-monospace, SF Mono, Menlo, Consolas, Liberation Mono, monospace'
    fontSize: '0.9em'
  drawer:
    fontFamily: 'system-ui'
    fontSize: '0.85em'
    fontWeight: 700
spacing:
  root-margin: '8px'
  default-spacing: '6px'
  compact-spacing: '4px'
  section-gap: '14px'
  indent-left: '16px'
  group-margin-h: '8px'
  group-margin-top: '6px'
  group-margin-bottom: '6px'
  sidebar-top-pad: '8px'
components:
  clean-button:
    height: '36px'
  clean-button-running:
    textColor: '{colors.log-error}'
    typography: '{typography.label}'
  table-row:
    height: '24px'
  sidebar:
    width: '380px'
  collapse-header:
    typography: '{typography.label}'
    padding: '4px 0'
  invalid-path-input:
    textColor: '{colors.log-invalid-path}'
---

# Design System: cleanmodels-qt

## 1. Overview

**Creative North Star: "The Engineering Utility"**

cleanmodels-qt is a Qt6 desktop application that looks and feels native on Linux, macOS, and Windows. It is built from standard Qt widgets with minimal custom styling — a model viewer plus a CLI dashboard plus a structured options panel, fused into a single resizable window. The visual language is the platform's, not the app's.

The reference points are professional engineering and content tools that live alongside an IDE on the same desktop: a native file manager, a CMake GUI, a glTF inspector, the OS's own image viewer. The anti-references are everything in PRODUCT.md: Blender-dark themes, fantasy launchers, web-app card grids, gradient buttons, glass surfaces.

The window has three primary surfaces. The **viewport** (OpenGL, ~50% of the window when expanded) is where the user sees the loaded model. The **sidebar** (320–480px, default 380px) is where the user configures the run. The **findings + log area** (bottom split) is where results stream in. None of these is decorative; all three reflect the same CLI run.

**Key Characteristics:**

- Native Qt widgets and native palette. Custom QSS is the exception, never the default.
- Viewport-first layout: the 3D view is the largest surface when the user has a model loaded.
- Color is reserved for severity in the log, the viewport, and the invalid-path indicator. Nothing else gets tinted.
- Spacing is small and dense (root margin 8px, default spacing 6px) so the window stays compact at common laptop resolutions.
- Tokens are pixels. Qt's logical-pixel scaling handles HiDPI; we don't redefine sizes per density.

## 2. Colors

The palette is defensive. The OS provides the chrome (window frame, scrollbars, buttons, menus, splitters); the app provides severity colors for the log, three colors for the OpenGL viewport, and one red for invalid input paths. Everything else is the system palette.

### Primary

The application has no "primary" accent color. The system theme provides whatever accent the user has configured at the OS level. Trying to add a brand colour on top of that would fight the OS — and PRODUCT.md is explicit that we don't.

### Log Severity (HTML-coloured spans in the QTextEdit log)

These are the colours emitted into the log via inline HTML. They map cleanmodels' severity model onto the log surface. Hex values are normative; the log uses them literally.

- **Info** (`#3498db`): General informational messages from the GUI ("Selected directory: ...", "Found N models").
- **Success** (`#27ae60`): Successful operations and completion summaries.
- **Error** (`#e74c3c`): Errors that the user must read. Also used for `SevError` from the CLI.
- **Warning** (`#e67e22`): Warnings worth noting but not blocking.
- **Command** (`#95a5a6`): The CLI invocation line itself, dimmed so it doesn't compete with results.
- **Fix Applied** (`#2ecc71`): A subtly brighter green that distinguishes "we changed something" from "the operation succeeded" in the log stream.
- **Severity: Info** (`#7f8c8d`): `SevInfo` items from the CLI. Lower contrast on purpose; surfaces only under verbose.
- **Severity: Warning** (`#f39c12`): `SevWarning` items from the CLI. A warmer yellow than the GUI's own Warning so the source is visible.

### Viewport

OpenGL clear/grid/overlay colors. RGB floats in code; hex equivalents documented here.

- **Background** (`#2E3340`, RGB(0.18, 0.20, 0.25)): Dark slate-blue clear color. Cool and neutral; lets light-coloured models sit comfortably without glare.
- **Grid** (`#595966`, RGB(0.35, 0.35, 0.40)): Mid-gray grid lines. Visible against the background but not so bright they compete with the model wireframe.
- **Reference Model Overlay** (`#6699E6`, RGB(0.4, 0.6, 0.9)): Light blue. Distinct from any common diffuse colour so the user can tell at a glance what's the loaded model and what's the reference.

### Invalid State

- **Invalid Path** (`#FF0000`): Pure saturated red, applied as a `color:` style on the input directory's `QLineEdit`. The only place in the app where pure-saturated red is used. Its rarity is the point.

### Named Rules

**The Native-Palette Rule.** The application does not define its own primary, secondary, or accent colour. The OS provides those. Any time custom colour is used, it is severity (in the log), spatial (in the viewport), or validation (the invalid-path red).

**The Severity-Colour Rule.** Colours in the log are reserved for severity. Don't tint filenames, paths, or counts; they live in the system foreground colour. The severity tag is the only coloured token on a log line.

## 3. Typography

The application does not ship its own fonts. Qt resolves the system UI font; the app applies bold and size deltas via `QFont` properties (`setBold`, `setPointSizeF`).

**UI Font:** System UI font (Qt resolves: Segoe UI on Windows, San Francisco on macOS, the configured GTK/Plasma font on Linux).

**Log Font:** System monospace, sized at 0.9× the body. Used in the raw log pane and any pre-formatted CLI output.

### Hierarchy

- **Headers** (bold, system size): Section labels in the sidebar; collapsible-section header buttons. Bold is the only weight contrast inside the sidebar.
- **Body** (regular, system size): All other labels, controls, list rows, table cells.
- **Drawer** (bold, 0.85× system size): Compact header text for collapsed/condensed UI strips.
- **Log** (monospace, 0.9× system size): The raw log `QTextEdit`. Smaller and monospaced so dense streaming output doesn't dominate the window.
- **Clean Button (running)** (bold, system size, color `{colors.log-error}`): The Clean action button while a run is in flight. Bold red text signals "operation in progress, this is the cancel target".

### Named Rules

**The System-Font Rule.** Don't bundle fonts. Don't override the family. Bold and size deltas are the only typography knobs. The app reads correctly in whatever font the user has configured.

**The Sparing-Bold Rule.** Bold is reserved for: section headers, collapsible-section toggles, and one transient state (Clean-while-running). Body text is never bold. Buttons get their weight from the OS theme.

### Font-relative chrome

Anything that frames text — buttons, table cells, drawer headers, progress bars, status sliver — is sized in *em* multiples of `fontMetrics().height()` (vertical) or `horizontalAdvance("0")` (horizontal). The multipliers live in `constants.h` under `Layout::*Em` / `Layout::*Chars`, the conversion to pixels happens via `Layout::emH(fm, em)` / `Layout::emW(fm, chars)` in `metrics.h`.

| Token | Multiplier | Used for |
|---|---|---|
| `CleanButtonHeightEm` | 2.0 × line-height | Bold "Clean" button |
| `TableRowHeightEm` | 1.4 × line-height | File-table rows |
| `SidebarToggleHeightEm` | 1.2 × line-height | "Hide Sidebar" flat button |
| `DrawerTitleHeightEm` | 1.5 × line-height | Raw-log drawer title bar |
| `DrawerCloseSizeEm` | 1.2 × line-height | Drawer close button (square) |
| `DetailPanelMinHeightEm` | 5.0 × line-height | Detail panel min height (~5 body lines) |
| `StatusProgressHeightEm` | 0.7 × line-height | Status-bar progress sliver |
| `TableColumnSizeChars` | 9 × digit-width | "Size" column ("1234.56 KB") |
| `TableColumnStatusChars` | 11 × digit-width | "Status" column |
| `TableColumnFixesChars` | 9 × digit-width | "Fixes" column |
| `TableColumnTimeChars` | 9 × digit-width | "Time" column |
| `RefModelComboWidthChars` | 14 × digit-width | Reference-model combo min width |
| `StatusProgressWidthChars` | 11 × digit-width | Status-bar progress sliver width |

The point: a user who bumps the system font from 12pt to 16pt, or runs at 200% scale, gets buttons / table cells / drawers that grow proportionally instead of clipping at 96-DPI defaults. Each call site reads the *widget's own* `font()`, so when a child uses a smaller / monospace / bold variant the chrome adapts to that font, not to the application default.

The two non-em pixel sizes that remain are `SidebarMinWidth`/`SidebarMaxWidth`. Those are gross structural breakpoints — they decide when the sidebar layout collapses or stretches — not text frames, so they stay in pixels by design.

## 4. Elevation

Flat by default. Qt's native widgets carry whatever shadow vocabulary the OS provides — window drop-shadow, popup shadow on menus, focus ring on inputs. The application does not add shadow tokens of its own.

### Spatial cues that aren't shadows

- **Splitter handles** between viewport / sidebar / log: thin, OS-native, draggable.
- **`QGroupBox` framing** for sidebar option groups: native border, optional title.
- **Collapsible sections**: header is a `QPushButton` (text-aligned left, transparent in collapsed state), content area shows/hides under it.

### Named Rules

**The Flat-By-Default Rule.** No custom shadows, no glow tokens, no glass. Depth comes from native widget chrome only. If a panel or card needs a shadow, the layout is wrong.

## 5. Components

### Window Layout

A single `QMainWindow` with three resizable regions wired through `QSplitter`:

- **Viewport** (top-left): OpenGL widget with model preview. Min height ~280px; user-resizable.
- **Sidebar** (right): Options panel. Min 320, max 480, default 380px (`Layout::SidebarMinWidth/MaxWidth/DefaultWidth`).
- **Findings + Log area** (bottom): Findings tree on the left, raw log `QTextEdit` on the right (also splittable). Either side can collapse.

Persisted: window geometry, splitter positions, sidebar width, log/findings visibility, sidebar visibility.

### Sidebar Sections

The sidebar is a vertical stack of `QGroupBox`-framed sections, each containing a `QFormLayout`. Sections include:

- Directories (input / output / pattern)
- Classification (combo)
- Fix toggles (validate, strip degenerate, animations, pivots, tilefade, AABB, reparent, wrap-root, split-multiedge)
- Rescaling (X/Y/Z)
- Snap controls (vertex / TVert)
- Render / shadow overrides
- Tile-specific operations (water, foliage, splotches, ground, chamfer, raise/lower, walkmesh remap)
- Texture/EE cleanup
- Camera sensitivities

Layout tokens (`Layout::*`):

- Root margin: 8px
- Default spacing: 6px
- Compact spacing: 4px
- Section gap: 14px
- Indent left: 16px
- GroupBox margins: 8px horizontal, 6px top, 6px bottom

### Collapsible Sections

A `QPushButton` styled to look like a section header (`text-align: left; padding: 4px 0; font-weight: bold;`) toggles visibility of a `QWidget` content panel. Used for "Advanced" sub-groups inside larger sections so the default sidebar stays scannable.

### Clean Button

The primary action. Triggers a `cleanmodels` subprocess with the current options.

- **Resting**: `QPushButton`, min-height = `Layout::CleanButtonHeightEm` × line-height (~36px at 12pt), default Qt style. Label: "Clean".
- **Starting**: button is disabled briefly between `QProcess::start()` and `QProcess::started`. No restyle — just disabled. There is nothing to abort yet.
- **Running**: same button, restyled via QSS to `color: <LogColor::Error>; font-weight: bold;`. The colour is read from `LogColor::Error` (not hard-coded), so the running state stays in lockstep with the rest of the severity palette. Label changes to a cancel-style cue. Bold red signals "this is the in-flight target". Applied in `MainWindow::onCleanStarted`.
- **Failed to start**: button re-enabled with original "Clean" label and a critical message in the detail panel. Applied in `MainWindow::onCleanProcessError` when `err == QProcess::FailedToStart`.
- **Done**: stylesheet cleared back to default (`m_cleanButton->setStyleSheet("")`).

### Subprocess Lifecycle (Async)

Every CLI invocation in the Qt app is signal-driven; no slot ever calls `QProcess::waitForStarted` or `waitForFinished` on a path the user can take. The UI thread stays responsive even when the CLI binary is slow to launch (cold cache, antivirus interception, network filesystem).

Two patterns appear in the code:

- **Long-running batches (`MainWindow::doClean`)**: `m_pCleanProcess` is a member, parented to the window. Persistent connections in the constructor wire `&QProcess::started` → `onCleanStarted`, `&QProcess::errorOccurred` → `onCleanProcessError`, and `&QProcess::finished` → `onCleanFinished`. `doClean` disables the button, calls `start()`, and returns. The state machine fires the appropriate slot when the OS resolves the start.
- **One-shot decompiles (`ModelViewport::decompileAsync`)**: a transient `QProcess` parented to the viewport with a `QTimer` watchdog (`CliDefaults::ProcessTimeoutMs`, 10s). On `finished`, the helper invokes a success or error callback and `deleteLater`s the process. The watchdog kills runaway CLI processes so a wedged decompile never leaks. Callers (`previewFile`, `loadReferenceFile`) capture a `QPointer<ModelViewport>` so the callback is a no-op if the widget is destroyed mid-operation.

### Input Directory Field

A `QLineEdit` plus a "Browse" `QPushButton`. The line edit gets one piece of custom styling: when the path doesn't exist or isn't readable, `setStyleSheet("color: <LogColor::InvalidPath>")` paints the text pure red. Cleared back to default once the path is valid. The colour is read from `LogColor::InvalidPath`, never hard-coded inline.

### Findings Tree

A `QTreeView` listing per-file results streamed from the CLI's JSON-lines events. Row height fixed at 24px (`Layout::TableRowHeight`) for density. Columns: file, status, fixes, warnings, errors. Severity icons or coloured indicators per row.

### Raw Log Pane

A read-only `QTextEdit` rendered in the system monospace font at 0.9× body size. Streams every CLI line plus the GUI's own info/success/error messages, each wrapped in HTML span with the matching `LogColor::*` token.

The HTML-fragment templates live in `loghtml.h` (`LogHtml::span`, `logLine`, `detailLine`, `listItem`, `preBlock`). Call sites pass the colour token and the inner HTML; the helper handles the escape sequence. This is the only place those literal `<span style="color:..."` strings are written, which means a tweak to the log markup (e.g. dropping `<br>` for native paragraph spacing, or switching to CSS classes) is one diff instead of fourteen. Callers remain responsible for `.toHtmlEscaped()` on any user-derived string before it reaches a helper — the helpers are template assemblers, not sanitisers.

### 3D Viewport

A `QOpenGLWidget`-backed scene. OpenGL context: 3.3 core, 24-bit depth, 4× MSAA (`GLDefaults::*`).

- **Clear color**: `#2E3340` (`ViewportColor::Bg*`).
- **Grid**: ground plane at `#595966` (`ViewportColor::Grid*`).
- **Reference model overlay**: `#6699E6` (`ViewportColor::Ref*`) when a reference is loaded for comparison.
- **Camera**: orbit, pan, zoom. Sensitivities persisted (`Setting::CameraRotSens`, `CameraPanScale`, `CameraZoomFactor`).

### Combo Options

Every dropdown uses `Options::*`'s `{label, cliValue}` pairs from `constants.h` so the display string and the CLI flag value stay coupled at compile time. Empty `cliValue` = "no selection / default" and the flag is omitted from the subprocess argv.

### Keyboard Surface

Every primary action has a keyboard binding so the app is usable from a screen reader, a kiosk without a mouse, or a developer who lives in shortcuts. The bindings are split between menu-bound `QAction`s (declared in `mainwindow.ui` so they appear inline in the menus) and free-standing `QShortcut`s registered in code.

| Shortcut | Action | Source |
|---|---|---|
| `F5` | Run cleanmodels (also the Clean button's primary shortcut) | `m_cleanButton->setShortcut` |
| `Ctrl+Return` / `Ctrl+Enter` | Run cleanmodels (alternate; F5 is unreachable on FN-locked laptops) | `QShortcut` |
| `Esc` | Abort an in-flight run (no-op when idle) | `QShortcut` |
| `Ctrl+O` | Load preset | `actionLoadPreset` |
| `Ctrl+S` | Save preset | `actionSavePreset` |
| `Ctrl+Q` | Quit | `actionQuit` |
| `Ctrl+L` | Toggle raw log drawer | `actionToggleRawLog` |
| `Ctrl+Shift+S` | Toggle sidebar | `actionToggleSidebar` |
| `F1` | Help | `actionHelp` |

`Esc` and the `Ctrl+Return` family use `Qt::WindowShortcut` context so modal dialogs above this window (file pickers, message boxes) handle their own `Esc` first. The Clean and Abort shortcuts share a single physical control: the shortcut callbacks both call `doClean()`, which switches between start and abort based on `m_bCleanRunning`.

### Accessible Names

Every primary widget gets an `accessibleName` (and where useful, `accessibleDescription`) so VoiceOver / Narrator / Orca can announce it without falling back to the class name. Tooltips on the same widgets repeat the action plus the keyboard binding, so mouse users get the same information visually.

Set on: `m_cleanButton`, `m_indirButton`, `m_outdirButton`, `m_inDirectory`, `m_outDirectory`, `m_filePattern`, `m_classificationCombo`, `m_filesTable`, `m_detailPanel`, `m_debugTextBrowser`, `m_viewport`, `m_refBrowseBtn`, `m_refModelCombo`, `m_refModelCheck`, `m_wireframeCheck`, `m_gridCheck`, `m_sidebarToggleBtn`. Severity icons and statistics labels rely on their text content for accessibility (the announcement is the same).

## 6. Do's and Don'ts

### Do

- **Do** use native Qt widgets and the OS palette. Custom QSS is the exception, never the default.
- **Do** route every operation through the `cleanmodels` CLI subprocess. JSON-lines events are the contract; the human-readable log is for the user, not for parsing.
- **Do** persist every option, geometry, splitter, and visibility toggle in `QSettings`. Users don't re-set the tool.
- **Do** centralise tokens in `constants.h` (`Layout`, `LogColor`, `ViewportColor`, `Options`). Hard-coded magic numbers in `mainwindow.cpp` are a bug.
- **Do** colour log entries by severity using `LogColor::*`. Wrap inline in HTML spans on the way to the `QTextEdit`.
- **Do** keep the 3D viewport accurate and responsive. It's the visual verification surface; lag or wrong geometry erodes trust.

### Don't

- **Don't** ship a custom dark theme, neon palette, or custom widget style. PRODUCT.md is explicit: not Blender-dark, not game-fantasy-themed, not web-app-styled.
- **Don't** add card grids with rounded shadows, hero metrics, gradient buttons, or glass surfaces. The app is not a SaaS dashboard.
- **Don't** reimplement validation or repair logic in C++. If the GUI needs a result the CLI doesn't surface, extend the CLI's JSON output and consume it here.
- **Don't** parse the human-readable log to detect outcomes. The JSON-lines schema is the contract.
- **Don't** add custom shadow tokens or a glow vocabulary. Native widget chrome is the only depth in the app.
- **Don't** introduce app-defined primary / accent colours that compete with the OS theme. Severity, viewport spatial cues, and the invalid-path red are the only sanctioned custom colours.
- **Don't** colour anything in the log other than severity. Filenames, counts, and paths use the system foreground.
- **Don't** add wizards, modal-on-modal flows, or multi-step setup screens. The main window does the job.
- **Don't** hard-code `setStyleSheet` strings outside the small set of sanctioned states (Clean-running, invalid-path) and the collapsible-section header. New styled states need a token in `constants.h` or a discussion first. Borderless / transparent buttons should reach for `setFlat(true)` before they reach for QSS.
- **Don't** bundle fonts or override the system UI font family. Bold and size deltas are the only typography knobs.
