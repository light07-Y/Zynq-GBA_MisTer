PS research validation runtime for Zynq-GBA.

Features:
- AXI-Lite runtime control for GBA core registers.
- HDMI VDMA triple-buffer bring-up, frame fill, and test pattern rendering.
- SSM2603 codec initialization with configurable sample rate and word length.
- UART command console for live validation.

Typical commands:
- status / diag
- audio status
- audio fmt 48000 16
- hdmi draw 0
- hdmi test on

Default ROM autoload:
- The runtime now autoloads `0:/games/PokemonSapphire-E.gba` during boot.
- Put a known-good, BIOS-compatible ROM at that path for repeatable bring-up.
- If no ROM is present there, boot continues and you can load one manually with `rom load <path>`.
- The previous hardcoded `ucity-advance-v1.0.3.gba` path is no longer used as the default autoload target.
