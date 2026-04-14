# Design Brief: cleanmodels-qt — Holistic App Redesign

## 1. Feature Summary

cleanmodels-qt is a desktop GUI for batch-cleaning NWN:EE 3D model files. It's used across all session types — quick batch cleans, one-off decompiles, and extended iterative sessions alongside a 3D editor. The typical user changes 0-3 options from defaults and hits Clean. The redesign should make that fast path effortless while keeping power-user options accessible without overwhelming.

## 2. Primary User Action

**Pick a directory, click Clean.** Everything else is secondary. The app should be usable in under 3 seconds for someone who's done it before: open, verify the directory, press F5 or click Clean, watch results flow in.

## 3. Design Direction

**Native utility feel.** The design context says "native file manager meets engineering tool" — this should feel like a macOS system app that happens to process 3D models. No custom chrome, no theming. The personality is **fast, precise, flexible** — the UI should reflect that by being lean, responsive, and never in the way.

The current UI's biggest problem is **option overload**: ~40 controls presented upfront when 95% of sessions use 0-3 of them. The redesign should dramatically reduce the visible surface area on first glance while keeping everything accessible.

The user explicitly said "too complex" is the wrong direction. This means: **subtract, don't add.** Every element visible by default needs to justify its presence for the common workflow.

## 4. Layout Strategy

**Three-zone layout:**

```
+-------------------+--------------------------------------------+
|                   |  Stats: Detected: 12  Cleaned: 0  Failed: 0|
|  [A] Options      +--------------------------------------------+
|  Sidebar          |                                             |
|                   |  [B] File Table                             |
|  In: /path...  [] |  File    | Size   | Status  | Fixes | Time |
|  All Fixes [x]    |  foo.mdl | 12 KB  | Pending |       |      |
|                   |  bar.mdl | 8 KB   | Pending |       |      |
|  > Advanced       +--------------------------------------------+
|  > Tile Options   |  [B2] Detail Panel (shows when row clicked) |
|  > Pivot Options  |  fix 1: Stripped 2 degenerate faces         |
|                   |  fix 2: Rebuilt AABB tree                   |
|  ─────────────── +--------------------------------------------+
|  [CLEAN]   (F5)  |                                             |
|                   |  [C] 3D Viewport                            |
|  [Hide Sidebar]   |  (wireframe / grid / reference toolbar)     |
|                   |                                             |
+-------------------+--------------------------------------------+
```

### Zone A — Options sidebar (collapsible)
- Directory path + browse button at the top
- "All Fixes (recommended)" checkbox — the only option visible for the fast path
- Collapsible groups (Advanced, Tile, Pivot) — collapsed by default
- Clean button pinned at the bottom, always visible, bold, prominent
- **Hide Sidebar toggle** at the very bottom — collapses the sidebar to give the workspace full width. Sidebar state persists via QSettings.
- The sidebar is 320-480px, does not stretch with window resize

### Zone B — File table + detail panel (top of workspace)
- File table with columns: File, Size, Status, Fixes, Time
- Clicking a row opens a **detail panel** below the table (like Mail.app preview pane)
- The detail panel shows all fixes and findings for the selected file, color-coded by severity:
  - Green checkmark for applied fixes
  - Orange for warnings
  - Red for errors
  - Gray for info
- The detail panel replaces the old HTML debug log for per-file inspection
- Double-clicking a row also previews the model in the viewport

### Zone C — 3D Viewport (bottom of workspace)
- Takes the remaining vertical space below the table+detail area
- Wireframe / Grid / Reference toolbar strip above it
- The viewport is the visual verification surface — it should be generous
- When no model is selected, shows the grid only

### Raw log (hidden by default)
- The old debug log is preserved as a collapsible panel, accessible from View menu or a keyboard shortcut
- Useful for debugging CLI issues or seeing the raw streaming output
- Not visible by default — the detail panel replaces it for normal use

### Visual hierarchy (squint test):
1. Clean button — largest, boldest element, always visible
2. File table — the data surface users scan during/after processing
3. Detail panel — per-file results when a row is selected
4. 3D viewport — the verification tool
5. Input directory — the "where"
6. Options — background noise until needed

## 5. Key States

| State | What the user sees | What they feel |
|-------|-------------------|----------------|
| **First launch (no directory)** | Empty table with "Select an input directory to begin." Detail panel empty. Viewport shows grid. Clean button disabled. | Guided |
| **Directory selected** | Table fills with MDL files. Clean button enabled. Detail panel shows "Click a file to see details." | Ready |
| **Clean running** | Table rows update live with status icons. Progress in status bar. Clean button becomes red "Abort". | Confident |
| **Clean complete** | All rows show status. Stats update. Clicking any row shows its fixes in the detail panel. | Satisfied |
| **File selected** | Detail panel shows fixes/findings. Viewport loads model preview. | Connected |
| **No fixes needed** | Detail panel shows "No issues found." for that file. | Reassured |
| **Error on file** | Row shows Failed icon. Detail panel shows error message in red. | Informed |
| **Sidebar hidden** | Full-width workspace. Small toggle button in the workspace area to restore. | Focused |

## 6. Interaction Model

- **Directory**: Type, browse button, or drag-and-drop folder onto window
- **Clean**: Click button or F5. Same button becomes Abort (red) while running
- **File selection**: Single-click shows detail panel + scrolls to that file's results. Double-click also previews in viewport
- **Detail panel**: Always visible below table when a file is selected. Shows fixes (green), warnings (orange), errors (red). Scrollable if many results
- **Viewport**: Middle-click orbit, right-click pan, scroll zoom. Toolbar for wireframe/grid/reference
- **Sidebar toggle**: Click "Hide Sidebar" to collapse. Click the restore button in the workspace to bring it back. Persists across sessions
- **Raw log**: View > Show Raw Log (or Cmd+L) to toggle the old text log as a collapsible panel
- **Presets**: File > Save/Load Preset

## 7. Content Requirements

**Labels** (sentence case):
- Stats: "Detected: N", "Cleaned: N", "Failed: N"
- Table: "File", "Size", "Status", "Fixes", "Time"
- Buttons: "Clean" / "Decompile" / "Abort"
- Status bar: "Idle" / "Processing filename.mdl"

**Empty states**:
- Table: "Select an input directory to begin"
- Detail panel (no selection): "Click a file to see details"
- Detail panel (no issues): "No issues found"
- Viewport: Grid only
- Raw log: "Output will appear here when you run Clean"

**Detail panel content per file**:
- Each fix: green checkmark + description (e.g., "Stripped 2 degenerate faces")
- Each finding: severity icon + "WARN: message" or "ERROR: message"
- Summary line at top: "3 fixes applied, 1 warning"

## 8. Recommended References

- `spatial-design.md` — sidebar/workspace proportions, detail panel sizing
- `interaction-design.md` — table selection, detail panel reveal, button states
- `ux-writing.md` — label consistency, empty states, error messages
- `craft.md` — overall quality bar

## 9. Resolved Decisions

- **Per-file results**: Detail panel below the table (Mail.app style), not tree-view or inline expand
- **Log panel**: Kept but collapsed by default, accessible via View menu. Detail panel is the primary results view.
- **Sidebar hideable**: Yes, with a toggle. State persists in QSettings.
- **Must preserve**: "All Fixes" one-click default is the killer feature. Never bury it.

## 10. Resolved Open Questions

1. **Detail panel height**: Resizable via QSplitter between the file table and the detail panel. User drags the divider.
2. **Batch summary**: Yes — when no specific file is selected after a clean run, the detail panel shows a batch summary: "12 files cleaned, 47 fixes applied, 2 warnings". Clicking a file replaces the summary with that file's details.
3. **Raw log position**: Bottom drawer — slides up from the bottom of the window, overlapping the viewport. Toggled via View > Raw Log (Cmd+L). Not a permanent panel.
