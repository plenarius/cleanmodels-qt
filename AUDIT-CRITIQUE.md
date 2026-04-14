# cleanmodels-qt — Design Audit & Critique

## Anti-Patterns Verdict

**Pass.** This does not look AI-generated. It looks like a competent developer built a functional tool and stopped before the "polish" phase. No gradient text, no glassmorphism, no hero metrics, no card grids. The aesthetic is honest — it's a Qt utility app that uses native widgets. The problem isn't that it's trying too hard; it's that it isn't trying at all in places where it should.

---

## Overall Impression

The tool is functionally complete and architecturally sound. The 3D viewport, file table, debug log, and option panels all work. But the layout feels like a **settings dialog that grew into a main window** — every option has equal visual weight, there's no clear narrative flow from "pick a directory" to "review results," and the most important element (the 3D viewport where users verify their work) is crammed into half of the bottom quarter of the screen.

**Single biggest opportunity**: Restructure the layout so the viewport and results are the heroes, and the options recede into a sidebar or panel.

---

## What's Working

1. **Progressive disclosure via collapsible groups** — The Advanced, Tile, and Pivot sections default to collapsed. This is the right instinct — most users should never need to open them. The "All Fixes (recommended)" master checkbox is a genuinely good UX pattern.

2. **File table with status feedback** — Icons for ASCII/binary, per-file status/fixes/time, and the colored debug log provide a clear audit trail. The tooltip on the fixes column showing individual fixes is thoughtful.

3. **Reference model overlay** — Being able to visually compare against a reference model in the 3D viewport is a standout feature for this domain.

---

## Priority Issues

### 1. The viewport is buried — the most critical element gets the least space

**What**: The 3D viewport is the user's only way to verify fixes visually. It's "critical to the workflow." But it's allocated to roughly 1/4 of the window — the bottom-right quadrant of a bottom half-panel, hidden behind a horizontal splitter.

**Why it matters**: Users process a directory of models, then want to spot-check results. They have to squint at a tiny viewport or manually drag splitters every session. The options panel (which they configure once and forget) takes more space than the thing they stare at continuously.

**Fix**: Consider a left sidebar layout: narrow options panel (250-300px) on the left, file table + viewport stacked on the right taking 70%+ of the window. Or a tabbed approach where options are a collapsible panel and the main area is table + viewport. The viewport should be at *least* 50% of the visible area when working.

**Command**: `/i-arrange`

### 2. Flat visual hierarchy — everything screams at the same volume

**What**: The In/Out path fields, mode radio buttons, fix checkboxes, advanced options, tile options, pivot options, clean button, file table, debug log, and viewport all sit in a single vertical stack with uniform 4px spacing. There's no visual grouping, no breathing room, no sense of priority.

**Why it matters**: A new user opening this for the first time can't tell at a glance: "What do I do first? What's the primary action? What can I ignore?" The eye has nowhere to land. The Clean button — the single most important action — is the same visual weight as the "Pattern:" label.

**Fix**: 
- Give the Clean button significantly more prominence — larger, colored, or in a fixed toolbar position
- Separate the "configure" phase (top: paths + options) from the "results" phase (bottom: table + viewport + log) with a clear visual divider
- Use typography weight/size to create hierarchy: section headers should be visually distinct from field labels

**Command**: `/i-arrange`, `/i-typeset`

### 3. The debug log is an HTML soup with no structure

**What**: `appendDebugHtml` inserts raw HTML strings with inline `color:` styles into a `QTextBrowser`. The log output is a wall of colored text with `<b>`, `<ul>`, and `<li>` tags. There's no filtering, no search, no severity-based collapse, no way to copy a structured report.

**Why it matters**: When batch-cleaning 200 models, the debug log becomes unusable. Users can't find the one model that had a warning, can't filter to show only errors, can't export a clean summary. Double-clicking a table row scrolls the log to that model's entry, but if there are hundreds of entries, this is still hunting through a wall of text.

**Fix**:
- Consider a structured log model (QTreeView or QTableView) with columns for severity/file/message, filterable by severity
- At minimum, add a severity filter (show all / errors only / warnings+errors) and a search box
- The current HTML approach is fragile for large volumes — it will also degrade performance as QTextBrowser re-renders the entire document on every insert

**Command**: `/i-harden`, `/i-arrange`

### 4. No empty state or onboarding guidance

**What**: When the app launches with no prior directory configured, the user sees an empty file table, an empty debug log, an empty viewport, and a blank directory field. There's a "Welcome to Clean Models:EE QT!" message in the log, but no guidance on what to do next.

**Why it matters**: Design principle says "Approachable complexity — non-technical modders should be able to hit 'All Fixes' and get a good result." But the current first-run experience offers zero guidance. A module builder who downloads this tool has to figure out: Where do I point it? What are reasonable defaults? What does "Clean" even do?

**Fix**:
- Empty viewport: show a message like "Select a directory and double-click a file to preview" or a simple graphic
- Empty table: show "No MDL files found — select an input directory above"
- Consider a "Quick Start" banner that appears on first launch and can be dismissed

**Command**: `/i-onboard`, `/i-clarify`

### 5. Labels are terse to the point of cryptic

**What**: Labels like "Snap:", "TVert Snap:", "Chamfers:", "Retile ground:", "Smoothing groups:", "Split first:" are domain jargon with no explanation. The "Slice height (x10cm):" label tries to explain the unit but the parenthetical is confusing — why not just show the actual unit?

**Why it matters**: Even experienced NWN modders may not know what "TVert Snap" means or why they'd want to remap walkmesh materials. The tool has ~40 options and almost none of them have descriptions or tooltips.

**Fix**:
- Add `setWhatsThis()` or `setToolTip()` on every control with a one-sentence explanation
- For obscure options, add a `(?)` link or info icon that expands an explanation
- Consider renaming labels for clarity: "Vertex Snap Grid" instead of "Snap:", "Texture Coordinate Snap" instead of "TVert Snap:"

**Command**: `/i-clarify`

---

## Medium-Severity Issues

### 6. Invalid path feedback is poor
**Location**: `mainwindow.cpp:548` — sets text color to red on invalid path but provides no message. User sees red text but doesn't know why.
**Fix**: Add a tooltip or inline label: "Directory does not exist"

### 7. Clean button doesn't visually change state when running
**Location**: Text changes to "Abort" but the button style is identical. During a long batch, users may not notice the mode change.
**Fix**: Change the button's background color or style when in "running" state.

### 8. File sizes displayed as raw bytes
**Location**: `updateFileListing()` — `QString::number(inputFile.size())`. Displays "142376" instead of "139 KB".
**Fix**: Format with human-readable sizes.

### 9. No keyboard shortcut for Clean
**Location**: The most important action has no accelerator. Ctrl+Enter or F5 would be natural.

### 10. Status bar elements are reversed
**Location**: Lines 637-639 — progress bar appears *before* the "Status:" label, reading right-to-left as "... [progress] Status: Idle". Should be "Status: Idle [progress]".

### 11. No drag-and-drop for input directory
Users can't drag a folder onto the window to set the input directory. This is a missed native-feel opportunity.

### 12. Table row height (20px) is very tight
**Location**: `setDefaultSectionSize(20)` — may cause text truncation on HiDPI displays and makes click targets small.

---

## Low-Severity Issues

### 13. Context menu only offers "Copy path" — consider adding "Open in explorer", "Preview", "Show in log"
### 14. The `indir` and `outdir` icon resources are swapped in `icons.qrc` (line 17-18: indir points to outdir.png and vice versa)
### 15. `presetRow->addSpacing(40)` is a magic number not using `Layout::` tokens
### 16. Scale preset buttons (0.5x, 0.75x, 1.0x, 1.5x, 2.0x) are small (48px) and feel disconnected from the scale spinboxes — consider a dropdown or integrated control
### 17. No visual indicator of which model is currently displayed in the viewport

---

## Positive Findings

- **Collapsible sections** are a solid progressive disclosure pattern — maintain this
- **JSON-lines protocol** between CLI and GUI is well-architected and makes the debug log reliable
- **File system watcher** with debounce timer provides responsive directory monitoring without polling overhead
- **QSettings persistence** is thorough — the app remembers everything between sessions
- **Reference model overlay** is a domain-specific feature that shows real understanding of the modding workflow
- **Icon differentiation** between ASCII and binary MDLs in the table is a nice touch

---

## Recommendations by Priority

### Immediate
1. Add tooltips to every control (low effort, high impact for new users)
2. Fix the swapped icon resources in `icons.qrc`
3. Add a keyboard shortcut for Clean (F5 or Ctrl+Enter)

### Short-term
4. Restructure the layout so the viewport gets more screen real estate
5. Add empty states for table, viewport, and debug log
6. Make the Clean button visually prominent and show running state clearly
7. Format file sizes as human-readable

### Medium-term
8. Replace the HTML debug log with a structured, filterable log view
9. Add drag-and-drop for input directory
10. Improve label clarity and add inline help for domain-specific options

### Long-term
11. Consider a sidebar layout instead of the current vertical stack
12. Add a structured export of the cleaning report (CSV, summary text)
13. Investigate performance of QTextBrowser for very large batch runs

---

## Suggested Commands

- **`/i-arrange`** — Restructure the layout: viewport prominence, sidebar options, visual hierarchy (addresses issues 1, 2, 3)
- **`/i-clarify`** — Improve labels, add tooltips, rename cryptic options (addresses issues 5, 6)
- **`/i-onboard`** — Add empty states, first-run guidance, progressive onboarding (addresses issue 4)
- **`/i-harden`** — Structured log view, error handling, edge cases (addresses issue 3, 7, 8)
- **`/i-polish`** — Clean button prominence, state feedback, file size formatting, keyboard shortcuts (addresses issues 7, 8, 9, 10, 12)
