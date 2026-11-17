# ADR 0159: Serve 989snd SL_COMMAND_BATCH (fno 77)

- **Status:** Accepted.
- **Date:** 2026-11-17.
- **Scope:** 989snd main server (`0x00123456`) handler `serve_ee_snd_call` in `runtime/include/reps2/runtime/ee_hle.hpp`.

## Context

After clearing the `unsupported_instruction` at `pc = 0x0025432C`, the sound-initialisation and command-flushing routines in the title invoke `sceSifCallRpc` targeting the main sound server with function number 77 (`SL_COMMAND_BATCH`).

- **Observed:** `snd_SendIOPCommandAndWait` and the batch flush pathway at `0x00202478` issue RPC function 77 to dispatch accumulated sound commands with a request packet containing at least 12 bytes. The EE client waits on the completion handshake via `snd_GotReturns` (`0x00201640`).
- **Derived:** The IOP 989snd server executes the batched commands and signals completion using the standard return buffer format `{-1, 0, -1}` (`kEeSndReturnDoneWord`).
- **Unknown:** Internal sound synthesis state or hardware sound register writes.

## Decision

1. Define `kEeSndCmdSlCommandBatch = 77U` and `kEeSndBatchRequestBytes = 12U`.
2. Update `serve_ee_snd_call` to accept `kEeSndCmdSlCommandBatch` when `send_size >= 12` and `recv_size >= 12`.
3. Write the standard return handshake `{-1, 0, -1}` into the reply buffer and emit `RPC_END` to unblock the caller.

## Consequences

Sound command batch flushes succeed without stalling the calling EE thread on unmodelled function 77.
