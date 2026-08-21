# Phenix LogView — Project Context

## 1. Project Identity

**Project:** Phenix LogView  
**Current CMake target:** `logviewer`

Phenix LogView is a C++ desktop application for viewing and analyzing automotive ECU/logging data. It is intended to become a polished, modern automotive log viewer and telemetry-analysis application, with a more refined workflow and user experience than older/popular tools such as MegaLogViewerHD.

The application currently works with:
- MegaSquirt/TunerStudio/MegaLogViewer-style tab-delimited log exports.
- Haltech NSP / Elite `.dl` text logs.
- A custom binary `.plog` format implemented by this project, compressed with Zstandard.

The product direction is not merely "display CSV data": it is an automotive telemetry/log-analysis application with plotting, calculated channels, table analysis, drive-regime detection, VE analysis, and virtual-dyno functionality.

## 2. Technology Stack

- C++20.
- CMake 3.16 or newer.
- Dear ImGui, using the `docking` branch.
- ImPlot 1.0.
- GLFW 3.4.
- OpenGL.
- nlohmann/json for JSON serialization.
- ExprTk for calculated/custom channel formulas.
- Zstandard (`zstd`) for the custom `.plog` format.
- `portable-file-dialogs` for native file dialogs.
- Font Awesome 7 icons are used by the UI.
- MSVC receives `/bigobj`.

CMake fetches several third-party dependencies with `FetchContent`. ImGui and ImPlot are built as local static library targets because their repositories do not provide the required CMake targets directly.

## 3. High-Level Architecture

The code is organized into four principal layers/namespaces:

### `core`
Domain/data-model layer. Contains the representation of a loaded log session and reusable analytical data structures.

Important types include:
- `core::LogSession`
- `core::Channel`
- `core::Table2D`
- `core::Curve1D`
- `core::RegimeDef` / `core::RegimeSummary`
- `core::ChannelMapping`
- custom-channel/formula definitions

`LogSession` is the central in-memory representation of loaded log data. Channels are stored as vectors of doubles and are aligned 1:1 by row index.

### `io`
Input/output and format-specific code.

The parser abstraction is `io::LogParser`. Each supported log format has its own implementation so format-specific quirks remain isolated from `core` and `ui`.

Current parsers:
- `MegasquirtCsvParser`
- `HaltechDlParser`
- `PlogParser`

There is also a CSV exporter.

### `engine`
Analysis and computation that should generally remain independent of the ImGui presentation layer.

Current systems include:
- `Decimator`
- `VeAnalyzer`
- `RegimeAnalyzer`
- `VirtualDyno`

### `ui`
Dear ImGui/ImPlot presentation and interaction layer.

The main `ui::App` owns the dockspace and active panels. Panels derive from `ui::PlotPanel` and share a synchronized `PlotCursor`.

## 4. Directory Structure

```text
src/
├── core/
│   ├── channel.*
│   ├── curve1d.*
│   ├── formula_evaluator.*
│   ├── logsession.*
│   ├── regime.*
│   └── table2d.*
│
├── engine/
│   ├── decimator.*
│   ├── regime_analyzer.*
│   ├── ve_analyzer.*
│   └── virtualdyno.*
│
├── io/
│   ├── logparser.h
│   ├── megasquirtcsvparser.*
│   ├── haltechdlparser.*
│   ├── plogparser.*
│   └── csvexporter.*
│
├── ui/
│   ├── app.*
│   ├── plotpanel.*
│   ├── timeseriespanel.*
│   ├── scatterpanel.*
│   ├── statuspanel.*
│   ├── tableeditorpanel.*
│   ├── tableoverlaypanel.*
│   ├── veanalysispanel.*
│   ├── driveregimepanel.*
│   ├── curve2dpanel.*
│   ├── virtualdynopanel.*
│   └── ui_helpers.h
│
├── utils/
│   └── utils.*
│
└── main.cpp
```

The CMake file explicitly lists the project sources and currently builds a single executable target named `logviewer`.

## 5. Core Data Model

### `LogSession`

`LogSession` is the central object passed between parsing, analysis, and UI.

It contains:
- channels and row count
- the selected time channel
- source path
- format information
- capture date
- semantic channel mapping
- crop range
- regime summaries
- stitch points
- annotations/bookmarks
- a revision counter
- detected data discontinuities

A channel is a named series with a unit and a vector of `double` values. Channels in a session are row-aligned.

The session exposes operations for:
- finding channels
- selecting/accessing the time series
- defining a crop range
- stitching/concatenating sessions
- adding/removing annotations
- scanning the log for timing/data discontinuities

### Channel semantics

The application does not assume that every ECU/log format uses the same channel names. `ChannelMapping` maps semantic roles such as:
- RPM
- Load/MAP
- AFR/Lambda
- TPS
- TPSdot
- CLT
- EGT1/EGT2
- Pulse Width
- Ignition Timing
- MAT
- duty-cycle channels
- fan duty
- fuel warmup correction

`ChannelMapping::autoDetect()` looks for known names from MegaSquirt and Haltech conventions and binds available channels to these semantic roles.

When implementing analysis features, prefer semantic channel mapping where appropriate instead of hard-coding one ECU's raw channel name.

## 6. Supported Log Formats

### MegaSquirt / TunerStudio-style export

`MegasquirtCsvParser` parses the CSV-like/tab-delimited export format used by TunerStudio/MegaLogViewer workflows.

Important distinction:
- This is the MSL/CSV-like exported format.
- It is **not** the MegaSquirt `.mlg` binary DataLogger format.
- It is **not** the raw MS3 ECU log format.

The parser handles format-specific details including metadata, units, boolean-ish values, malformed/mismatched rows, and a known first-row time anomaly.

### Haltech

`HaltechDlParser` parses Haltech NSP / Elite `.dl` text logs.

The format contains channel metadata and a `Log :` data block. The parser creates a normalized `Time` channel in seconds and converts Haltech invalid/sentinel values to NaN.

### Custom `.plog`

`PlogParser` handles the project's custom binary `.plog` format.

The current implementation:
- uses a `PLOG` magic value
- supports format versions through version 2
- uses Zstandard compression
- stores numeric data in a compact scaled representation
- has explicit representations for NaN/boolean values

Do not casually change the `.plog` binary layout; treat it as a format with compatibility implications.

## 7. Data Integrity and Time Handling

Time is a first-class part of the data model.

`LogSession::scanIntegrityAndBuildDiscontinuities()` analyzes the time channel and identifies:
- intentional stitch seams
- time inversions/non-monotonic timestamps
- dropped frames / serial dropouts
- duplicate timestamps are recognized separately

The nominal sample interval is estimated from the median of early positive sample deltas. Large gaps are treated as likely dropped frames using a threshold based on the nominal interval.

The UI can visualize these discontinuities in time-series plots.

When adding time-series features, preserve the distinction between:
1. normal sampling,
2. actual data discontinuities,
3. intentional seams created by log stitching.

## 8. Log Stitching / Concatenation

`LogSession::stitchSession()` can prepend or append another log session.

The operation:
- aligns channels using explicit merge resolutions,
- handles missing incoming channels with NaN values,
- offsets time so sessions become one continuous timeline,
- inserts a deliberate gap between stitched logs,
- records stitch points,
- clears the crop range,
- rescans integrity/discontinuities.

The stitch seam is intentionally represented as a data discontinuity rather than being treated like an ordinary sample gap.

## 9. Calculated / Custom Channels

The application supports custom calculated channels.

`FormulaEvaluator` uses ExprTk and allows formulas to reference channels using bracket syntax such as:

```text
[MAP] - 101.3
([RPM] * [PW]) / 12000
```

Channel references are resolved against the current `LogSession`, then the expression is evaluated row-by-row.

Custom channels retain:
- name
- unit
- formula text
- custom-channel status

There is also a collection of predefined automotive math presets, including pressure, AFR/lambda, temperature, injector duty-cycle, and estimated airflow calculations.

When adding calculated-channel functionality, use the existing formula/evaluator infrastructure rather than introducing another expression system.

## 10. Curves and Tables

### `Table2D`

`Table2D` represents editable two-dimensional data grids with X/Y breakpoints and cell values. It is used heavily by analysis/UI features such as VE analysis and table overlays.

### `Curve1D`

`Curve1D` represents a one-dimensional curve with sorted `(x,y)` points.

It supports:
- point insertion/removal/update
- piecewise-linear interpolation
- smooth Catmull-Rom-style spline interpolation
- JSON serialization

### Persistence

Several core/UI structures expose JSON serialization/deserialization using nlohmann/json. Preserve existing JSON structures when extending features unless a deliberate migration is required.

## 11. Drive Regimes

The project has a concept of **drive regimes**: meaningful operating conditions/events detected from logged data.

A `RegimeDef` contains:
- ID
- display name
- color
- shading visibility
- built-in/user status
- channel-bound rules
- configured metrics
- warning thresholds
- optional custom formula

`RegimeSummary` contains:
- the regime definition
- detected time intervals
- sample count
- dwell time
- percentage of the log
- calculated metrics
- warning messages

Built-in examples include concepts such as freeway cruise and overrun fuel cut.

Drive regimes are used by other features, including the Virtual Dyno.

## 12. Engine Analysis

### Decimator

The time-series UI does not necessarily plot every raw sample. `engine::Decimator` produces a reduced series suitable for the current plot range and pixel width.

The time-series panel caches decimated data based on:
- session
- session revision
- X-axis range
- plot width

When modifying plotting performance, preserve this separation between raw log data and display-resolution data.

### VE Analyzer

The VE analysis system is a substantial feature area.

It works with concepts including:
- observed AFR
- target AFR
- baseline VE
- AFR delta
- suggested VE
- sample density
- smoothing
- correction gain
- maximum allowed VE change
- unvisited cells
- drive-regime filtering

The UI provides editable tables and analysis operations. Manual table edits and calculated results are deliberately distinct.

### Regime Analyzer

`RegimeAnalyzer` detects configured drive regimes from log data and produces `RegimeSummary` results stored on the session.

### Virtual Dyno

`VirtualDyno` estimates wheel horsepower/torque from log data using a configurable vehicle profile.

The profile includes parameters such as:
- vehicle weight
- drag coefficient
- frontal area
- rolling resistance
- tire dimensions
- gear ratio
- final drive ratio
- SAE correction settings
- atmospheric conditions
- smoothing window

The Virtual Dyno can operate on a manual time range or detected drive-regime intervals, including averaging across multiple events.

Treat this as an analysis feature, not merely a plotting feature.

## 13. UI Architecture

`ui::App` owns the main application's dockspace and panel collection. It does not own the GLFW/OpenGL/ImGui lifecycle; that responsibility belongs to `main.cpp`.

`PlotPanel` is the common base for dockable panels.

Panels can:
- render each frame
- receive the current `LogSession`
- respond to data/regime changes
- expose a panel type ID
- save/load panel-specific JSON state

The shared `PlotCursor` synchronizes the time cursor between panels.

### Current panel types

- Time Series
- Scatter
- Status
- Table Editor
- Table Overlay
- VE Analyzer
- Drive Regime
- Curve 2D
- Virtual Dyno

The Time Series panel is a central visualization surface. It supports linked ImPlot subplots, channel selection, statistics, minimap, CSV export, cursor interaction, crop markers, annotations/bookmarks, and discontinuity visualization.

## 14. UI Interaction Patterns

The project has a centralized `ui::UI` helper layer for common controls and button styles.

Button styles include concepts such as:
- Primary
- Secondary
- Success
- Danger

Follow existing UI helper conventions when adding controls instead of creating ad-hoc styling.

The UI uses Font Awesome icons extensively.

The application uses ImGui docking and is designed around multiple dockable analysis panels rather than one monolithic view.

## 15. Timeline / Log Interaction

The time-series UI provides direct timeline-oriented operations such as:
- moving/centering the view
- crop start/end selection
- zooming to crop range
- clearing crop
- adding bookmarks/annotations
- following a shared cursor
- displaying data discontinuities
- exporting visible/session data to CSV

Keyboard shortcuts and timeline actions are part of the application's interaction model. New timeline features should fit into this existing interaction style.

## 16. Export

`CsvExporter` exports a `LogSession` to CSV and supports options such as:
- delimiter selection
- units row
- progress reporting
- cancellation

Do not assume that importing and exporting are exact inverses: parsers normalize different source formats into the common `LogSession` representation.

## 17. Build System

The CMake project currently:
- requires CMake 3.16+
- requires C++20
- uses `FetchContent`
- fetches ImGui's `docking` branch
- fetches ImPlot 1.0
- fetches GLFW 3.4
- finds system OpenGL
- builds ImGui and ImPlot as local static targets
- builds one executable target named `logviewer`
- copies `assets` after build to both the build directory and executable output directory
- adds `/bigobj` under MSVC

The executable currently includes the project's source hierarchy under `src/`, bundled Zstandard C source, and `app.rc`.

## 18. Architectural Guidelines for AI Coding Assistants

When modifying this project:

1. **Preserve the existing layer boundaries.**
   - `core` should remain primarily about data/domain structures.
   - `io` should contain format-specific parsing/export concerns.
   - `engine` should contain reusable analysis/computation.
   - `ui` should contain ImGui/ImPlot presentation and interaction.

2. **Do not move domain/analysis logic into UI code merely because the feature is initiated by a button.**

3. **Do not duplicate existing infrastructure.**
   Before creating a new mechanism, look for existing:
   - `LogSession` functionality
   - channel mapping
   - formula evaluation
   - `Table2D`
   - `Curve1D`
   - `PlotPanel`
   - shared cursor
   - revision/change notification
   - JSON state persistence
   - UI helpers

4. **Prefer extending existing abstractions over creating parallel ones.**

5. **Keep format-specific quirks inside their parser.**
   Do not make `core` or `ui` aware of MegaSquirt/Haltech file-format parsing details unless there is a genuine domain-level reason.

6. **Do not introduce new third-party dependencies casually.**
   Prefer the libraries already present.

7. **Avoid unrelated refactoring during feature work.**
   Keep changes focused on the requested behavior unless a structural change is genuinely necessary.

8. **Preserve existing user-visible behavior unless the task explicitly requests a change.**

9. **Be careful with large logs.**
   Avoid unnecessary full-channel copies, per-frame allocations, or O(N) work inside rendering loops. Existing decimation and caching mechanisms exist for performance reasons.

10. **Treat NaN and boolean/status channels deliberately.**
    Invalid samples are represented as NaN. Boolean channels are represented numerically but also carry semantic boolean metadata.

11. **Respect session revision/invalidation mechanisms.**
    Cached analysis/display data may depend on `LogSession::revision()`.

12. **Preserve JSON persistence compatibility where practical.**

13. **When adding a new panel, use `PlotPanel` and its state/panel-type mechanisms.**

14. **When adding analysis, prefer `engine` code that can be tested/reused independently of ImGui.**

15. **When adding a parser, implement the `io::LogParser` interface and keep format quirks isolated to that parser.**

## 19. Important Product Direction

The long-term goal is a **polished, refined, modern automotive telemetry/log-analysis application**, not simply a clone of an existing CSV log viewer.

Feature decisions should therefore favor:
- clear workflows
- good visual presentation
- useful automotive-specific analysis
- fast interaction with large logs
- discoverability
- sensible defaults
- cohesive behavior across panels
- professional presentation
- extensibility for additional ECU/log formats and analysis tools

At the same time, do not invent product requirements that have not been specified. When a feature request is ambiguous, preserve existing behavior and ask/identify the important product-level decision rather than making a large architectural assumption.

## 20. Known Scope / Boundaries

Current source code explicitly supports the MegaSquirt/TunerStudio-style exported text format, Haltech NSP/Elite `.dl`, and the project's custom `.plog` format.

The MegaSquirt parser explicitly does **not** currently target:
- `.mlg` binary DataLogger files
- raw MS3 ECU log format

Do not describe those formats as supported unless the implementation is changed.

## 21. Useful Terminology

- **LogSession:** normalized in-memory representation of one or more loaded/stiched logs.
- **Channel:** one row-aligned named data series.
- **Semantic channel mapping:** mapping raw channel names to automotive roles such as RPM, MAP, AFR, CLT, etc.
- **Stitch:** prepend/append another log into the current session.
- **Stitch seam:** intentional discontinuity introduced between stitched logs.
- **Regime:** detected/configured operating condition represented by one or more time intervals.
- **Table2D:** editable X/Y breakpoint grid with cell values.
- **Custom channel:** calculated channel generated from a formula.
- **Revision:** session mutation counter used by dependent caches.
- **Discontinuity:** detected timing/data anomaly or intentional stitch seam.
- **Virtual Dyno:** estimated power/torque calculation from logged vehicle data.

## 22. Guidance for Future Feature Requests

For future AI-assisted implementation requests, assume this file is the persistent project background.

A good task prompt should then specify:
- the desired user-visible behavior
- what should trigger it
- important edge cases
- whether existing behavior should change
- any performance expectations
- any UI/UX expectations
- whether persistence/backward compatibility matters

The AI should first identify the likely affected layers/files, then implement the smallest coherent change that fits the existing architecture.

## 23. Source-of-Truth Note

This context document was generated from the supplied `CMakeLists.txt`, the supplied combined source snapshot, and the project description provided by the developer.

Where this document describes current implementation details, the source code remains the ultimate authority. If this document and the current source diverge, prefer the current source and update this context document when the architectural change is intentional.
