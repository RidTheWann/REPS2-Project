# ADR 0063: discover the continuation after a SYSCALL trap

- **Status:** Accepted
- **Date:** 2026-11-04
- **Scope:** reachability analysis (`analysis.rs`), and therefore every generated
  translation, `analyze-elf` edge reporting and the build manifest

## Context

ADR 0062 gave a guest `SYSCALL` a host dispatch path: when the HLE layer is
enabled and a service is answered, the generated code resumes the guest at
`resume_pc`. The analysis never learned about that address. It classifies a
`SYSCALL` as `Terminator::Unsupported`, which has no outgoing edge, because the
architectural successor of a trap is the COP0 exception vector and not the next
instruction. Translation therefore only contained a block after a trap if some
other path (a jump, a call return, or a discovered root) happened to lead there.

The M4 real-title run exposed the gap. On Downhill Domination (`SCUS_971.77`,
entry `0x0010A008`) the boot path clears about six megabytes of RAM and reaches
`li $v1, 0x3C; syscall` at `0x0010A178`. With `--hle-syscalls` the layer
answered `SetupThread` (`syscalls=1 hle_handled=1 hle_number=0x0000003C`) and
then stopped immediately:

```
status=4 stage=unresolved_control_flow pc=0x0010A17C budget=3000000 executed=2756973
hle=1 syscalls=1 hle_handled=1 hle_number=0x0000003C hle_name=SetupThread
```

`0x0010A17C` is `pc + 4` after the trap and had no generated block. The service
was answered but the guest could not continue, which makes the HLE layer useless
on real code regardless of how many services it implements.

## Decision

1. A `SYSCALL` terminator exposes its fall-through as an outgoing
   `EdgeKind::SyscallContinuation` edge, and the traversal queues that address
   for translation.
2. The edge is documented as *not* an architectural successor. Without a
   serviced call the trap vectors through COP0 and no guest code at that address
   runs; the edge exists so a serviced call has somewhere to resolve.
3. The continuation is a **guess about content, not about reachability**. Retail
   images place data and jump tables directly after traps, so an undecodable
   continuation is reported through the existing skipped-block channel (the same
   one heuristic roots use) instead of aborting the analysis with a hard error.
   `block_targets` keeps returning only architectural successors, and the new
   `lenient_block_targets` returns the continuations that must be queued
   leniently.
4. A `SYSCALL` in a branch delay slot needs no continuation edge: the owning
   branch already contributes its taken and fall-through targets as
   architectural edges, and those are exactly the two resume addresses ADR 0062
   passes to the dispatcher.
5. `BREAK` gets no continuation edge. No host service ever resumes it, so
   discovering code after it would only guess without a consumer.

## Validation

- Two unit tests in `analysis.rs`:
  `discovers_the_continuation_after_a_syscall_terminator` asserts the
  `SyscallContinuation` edge to `pc + 4` and that the continuation block is
  translated; `treats_an_undecodable_syscall_continuation_as_a_skipped_guess`
  asserts that an undecodable continuation leaves the block absent, lands in
  `skipped_blocks`, is not reported as a rejected root, and does not fail the
  analysis.
- All eight retail boot executables from the local library were re-extracted and
  recompiled with `--discover` before and after the change. Every title still
  analyses successfully; the change adds 77-102 blocks and exactly one
  trap-terminated block per title:

  | Title | blocks before | blocks after | trap stops |
  | --- | --- | --- | --- |
  | Black (USA) | 101,877 | 101,956 | 242 -> 243 |
  | Bully (USA) | 215,635 | 215,737 | 254 -> 255 |
  | Downhill Domination (USA) | 56,101 | 56,181 | 250 -> 251 |
  | God of War II (USA) | 69,400 | 69,477 | 284 -> 285 |
  | GTA San Andreas (Indonesian v6) | 217,276 | 217,376 | 235 -> 236 |
  | GTA San Andreas (USA v3.00) | 217,276 | 217,376 | 235 -> 236 |
  | Need for Speed Most Wanted (USA v2.00) | 182,440 | 182,527 | 681 -> 682 |
  | Resident Evil 4 (USA) | 78,264 | 78,348 | 389 -> 390 |

  The explicit-unsupported-stop counts are unchanged, so the new blocks did not
  pull undecodable data into the graphs of these titles.
- The real title then runs past the trap and stops on the next service instead:
  `status=14 stage=unhandled_syscall pc=0x0010A194`,
  `hle_number=0x0000003D hle_name=-`, one call seen and one handled.
- `cargo fmt`, `cargo clippy --workspace --all-targets -D warnings`,
  `cargo test --workspace` and the CTest suite pass; the existing synthetic
  fixtures, which supply their continuations as explicit CFG roots, are
  unaffected.

## Consequences

Block counts, `analyze-elf`'s static edge totals and the manifest's
`static_edge_count` increase for every image containing a trap, because
continuation edges are now reported. They are visible as continuation edges
rather than folded into the architectural counts, and rendering stays
deterministic.

The analysis can now translate code that only a serviced trap reaches, which is
a prerequisite for any real-title syscall work rather than a compatibility claim.
A title that reaches an unimplemented service still stops at the trapping
instruction with no fabricated result; on Downhill Domination that service is
`CreateSema` (0x40), which is deliberately not implemented here. No new guest
service, no scheduler, no heap model and no real-title boot claim follows from
this change.
