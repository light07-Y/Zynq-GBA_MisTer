# PS App Conformance Rules

## Scope
This document applies to `src/ps/app/src` project-owned code (exclude `ThirdParty`).

## Architecture Alignment
- Reference architecture follows `ISO/IEC/IEEE 42010:2022` viewpoints:
  - Composition root: `main.c` and `Common/` context wiring.
  - Orchestration layer: `App/`.
  - Domain layer: `Video/`, `Diagnostics/`, `Rom/`, `UsbHost/`, `Input/`, `Hdmi/`, `Save/`.
  - Reusable infrastructure: `Audio/`, `Storage/`, `Vdma/`, `Gba/` wrappers.
- `ISO 26262-6` and `IEC TR 61508-3-3` intent is enforced by deterministic control flow, explicit state ownership, and compile-verified module boundaries.

## Layering and Decoupling Rules
- Public APIs must stay in `Inc/` headers; implementation-only symbols must stay in `Src/` and use internal headers only.
- For large modules, use explicit interface/implementation split:
  - `ps_app_usbhost.c` is interface layer.
  - `ps_app_usbhost_impl.c` is implementation layer.
- Use forward declarations in context headers whenever complete type layout is not required.
- Domain code must not depend on application super-context directly.

## Naming Rules
- File names: `ps_<module>_<role>.c/.h` (lowercase, underscore separated).
- Public functions: `Ps<App/Module><Action>()`.
- Internal implementation functions: append `Impl` only for adapter-split interfaces.
- Static/internal module variables: prefix `s_ps_<layer>_<module>_...` (for app-domain modules this becomes `s_ps_app_<module>_...`; for reusable infrastructure modules use `s_ps_<module>_...`).
- Macros/constants: `PS_APP_<MODULE>_<NAME>`.
- Boolean-like flags use `_enable`, `_ready`, `_pending`, `_active`, `_connected` suffixes.

### External ABI Exceptions
- Keep externally-required integration symbols unchanged; use wrapper adapters so internal logic remains convention-compliant:
  - C entry and RTOS hooks: `main`, `vApplicationStackOverflowHook`, `vApplicationMallocFailedHook`, `vApplicationAssert`.
  - CherryUSB low-level and class hooks: `usb_hc_low_level_init`, `usb_hc_low_level2_init`, `usb_hc_low_level_deinit`, `usbh_get_port_speed`, `usbh_xbox_run`, `usbh_xbox_stop`.
  - CherryUSB shared bus object: `g_usbhost_bus` (third-party owned symbol; project code may reference but must not rename).

## C Safety and Verifiability
- Keep side effects explicit and localized; avoid hidden transitive includes.
- Avoid implicit declarations; every cross-unit function must have a visible prototype.
- Keep pointer ownership explicit; null-check external inputs at API boundaries.
- Prefer fixed-width or platform-defined explicit integer types (`u8/u16/u32`).
- Prefer shared compatibility adapters for optional platform headers (for example `Common/Inc/ps_xgpiops_compat.h`) instead of duplicating fallback typedef logic in multiple modules.

## Integration and Verification Loop
- Every structural refactor must pass full build (`cmake --build .`) before merge.
- Behavior preservation is mandatory for refactor-only changes.
- Runtime command and diagnostics interfaces are backward compatible unless explicitly versioned.

## Standard Mapping Intent
- AUTOSAR layered architecture: application-facing API separated from implementation internals.
- CMSIS-Driver style: abstraction layer exposes responsibility-focused API; policy remains outside driver internals.
- TS 17961 + MISRA Addendum 2/3 + CERT C: analyzable naming, explicit interfaces, reduced hidden coupling.
- NIST SSDF: component isolation, limited blast radius, and verifiable build gate.
