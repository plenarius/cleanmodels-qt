# Extraction Findings — cleanmodels-qt

## All Extractions Complete

### Phase 1: Initial extractions

1. **`constants.h`** — Centralized design tokens: log colors, GL defaults, CLI defaults, viewport colors
2. **`ModelViewport::readMdlToAscii()`** — Deduplicated binary-detect + CLI-decompile (previewFile + loadReferenceFile)
3. **`GLDefaults::*`** — Unified QSurfaceFormat setup (main.cpp + modelviewport.cpp)
4. **`MainWindow::populateRefModelCombo()`** — Deduplicated ref combo population, fixed double-connect bug
5. **`Renderer::destroyRenderNodes()`** — Collapsed 4 identical GPU cleanup loops

### Phase 2: Future opportunities (now completed)

6. **`Setting::*` namespace** — All ~50 QSettings keys are now named constants. Load and save reference the same symbols, eliminating silent typo drift.
7. **`CliFlag::*` namespace** — All ~40 CLI flag strings are named constants. Both `buildCliArgs()` and `readMdlToAscii()` reference the same source of truth.
8. **`Layout::*` namespace** — Repeated margin/spacing values (`6`, `4`, `2`, `12`, `20`, `8/4/8/4`) are now semantic tokens: `RootMargin`, `DefaultSpacing`, `CompactSpacing`, `SectionGap`, `IndentLeft`, `GroupMarginH/Top/Bottom`.
9. **`Options::*` + `ComboOption` struct** — Combo items are defined as `{label, cliValue}` arrays in `constants.h`. Combos are populated via `fillCombo()` with CLI values stored as UserRole data. `buildCliArgs()` reads values via `currentData().toString()` instead of fragile index lookups.
10. **Camera configurable** — Rotation sensitivity, pan scale, and zoom factor are now `Camera` member variables with getters/setters, persisted in QSettings. Defaults are `static constexpr` on the Camera class.
