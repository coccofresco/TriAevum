# Runtime Memory Owner Provenance

- Read-only source repository: `I:\oot3decomp`
- Source commit at capture: `0dd01be396392611d60b776ca72a4a39fc43361f`
- Source file: `src/runtime/memory.c`
- Source SHA-256: `2BAE5DAC22AB9F7E9959D27B45D5920CB4536407903D19E322D280594A345420`
- Capture policy: exact semantic snapshot; the source repository had unrelated
  uncommitted work and was not modified or normalized.

This owner is compiled through the source-overlay guest-memory lowering. Its
source must never directly dereference a numeric 32-bit guest pointer on the
64-bit host.
