# Zynq-GBA_MiSTer Project Layout

This repository now uses a single canonical Vivado source layout:

- `src/bd/` stores project-owned block designs.
- `src/rtl/top/` stores the PL integration top-level modules.
- `src/rtl/common/` stores reusable AXI, DDR, frame pacing, and audio helper RTL.
- `src/rtl/core/gba_mister/rtl/` stores the upstream GBA core sources that are still part of the active design.
- `src/constraints/` stores board constraints that target the generated BD wrapper top.
- `scripts/build/` stores the canonical Vivado project maintenance and build scripts.

Generated Vivado directories such as `.Xil/`, `*.cache/`, `*.gen/`, `*.hw/`, `*.ip_user_files/`, `*.runs/`, `*.sim/`, and `*.srcs/` are treated as rebuildable artifacts. They are regenerated from `src/bd/`, `src/rtl/`, `src/constraints/`, and `scripts/build/rebuild_project.tcl`.

The current project flow is:

1. Run `vivado -mode batch -source scripts/build/rebuild_project.tcl`
2. Open the regenerated `Zynq-GBA_MisTer-Vivado.xpr`
3. Run synthesis and implementation from the clean project

For direct PL-only RTL elaboration of the hand-written integration top, use `scripts/build/check_zynq_top.tcl`.
