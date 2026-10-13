# cleanmodels-qt

Qt6/C++17 desktop GUI for [cleanmodels](https://github.com/plenarius/cleanmodels), a tool to fix and clean NWN:EE MDL model files. Launches the Go CLI as a subprocess and consumes its `--json-lines` output.

## Architecture
- **mainwindow.cpp** — UI construction (programmatic, not Designer-driven), settings, directory management
- **mainwindow_clean.cpp** — QProcess lifecycle, JSON-lines parsing, file table + debug log updates
- **modelviewport.cpp** — QOpenGLWidget subclass for 3D model preview (OpenGL 3.3 Core)
- **renderer.cpp** — Scene graph traversal, shader compilation, mesh/texture upload
- **mdlscene.cpp** — Lightweight ASCII MDL parser (preview-only, not the authoritative parser)
- **gpumesh.cpp / gputexture.cpp** — GPU resource wrappers
- **camera.cpp** — Orbit camera with mouse interaction
- **fsmodel.cpp** — QFileSystemModel wrapper for directory completion

## Design Context

### Users
NWN community modders who build, fix, and maintain 3D model files (.mdl) for Neverwinter Nights: Enhanced Edition. These users range from experienced 3D artists to module builders with limited technical background. They work in a pipeline that includes 3D editors (NWMax/Blender), the game's toolset, and cleanmodels as the quality gate before assets ship.

### Brand Personality
**Fast, precise, flexible.** The tool should feel like a sharp instrument — quick to produce correct results, trustworthy in its output, and accommodating of the wide variety of models found in the NWN community (old, malformed, hand-edited, tool-exported). It never corrupts data and always explains what it changed.

### Aesthetic Direction
- **Qt GUI**: Native OS look — blend in with the platform rather than imposing a custom theme. The interface should feel like a well-made professional utility, not a game launcher or a toy. Polished and intentional, but never flashy.
- **3D Viewport**: Critical to the workflow. Users rely on it to visually verify fixes. It should be clear, responsive, and show geometry accurately. Grid, wireframe, and reference model overlay are first-class features.
- **Anti-references**: Not Blender-dark, not game-fantasy-themed, not web-app-styled. Think: native file manager meets engineering tool.

### Design Principles
1. **Speed is a feature** — The legacy Prolog tool was too slow. Every operation should feel instant on single models, and batch processing should saturate I/O, not CPU. Never block the UI.
2. **Correctness over convenience** — Binary output must match the game's own compiler. Checks should catch real issues, not generate noise. When in doubt, preserve the original data.
3. **Approachable complexity** — The tool has deep options (pivot repair, tilefade slicing, walkmesh material remapping) but non-technical modders should be able to hit "All Fixes" and get a good result. Progressive disclosure: simple defaults, advanced options collapsed.
4. **Explain what changed** — Every fix, warning, and repair should produce a human-readable message. The debug log and per-file fix counts are the user's audit trail.
5. **Stay focused** — NWN MDL files only. No scope creep into other formats, game engines, or unrelated tooling.
