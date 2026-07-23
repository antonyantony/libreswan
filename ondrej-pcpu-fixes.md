# RFC 9611 per-CPU Child SA: responder never assigns/installs a CPU binding

## Issue

Testing `clones=4` between west (initiator) and east (responder):

- On west, `ip x s` shows the 4 Additional Child SAs each carrying a
  `pcpu-num` line (two entries per SA, one per direction):

  ```
  pcpu-num 3
  pcpu-num 3
  pcpu-num 2
  pcpu-num 2
  pcpu-num 1
  pcpu-num 1
  pcpu-num 0
  pcpu-num 0
  ```

- On east, `ip x s` shows the same 4 Additional Child SAs installed
  (traffic works, negotiation logs look clean), but **none of them
  have a `pcpu-num` line at all** — they're installed as ordinary,
  unpinned states.

So the responder is creating the right *number* of Child SAs, but
never binding any of them to a CPU.

## Root cause

`cpu_id` assignment for a larval Additional Child SA only happens in
two places in `programs/pluto/ikev2_create_child_sa.c`:

1. The initiator side, when *west* creates its own Additional Child
   SAs (`submit_v2_CREATE_CHILD_SA_additional_child()`, called from
   `ikev2_child.c`'s IKE_AUTH-response handling) — `cpu_id` is set
   directly to the loop index.
2. `process_v2_CREATE_CHILD_SA_rekey_child_request()` — the
   **REKEY** responder handler. It checks
   `md->pd[PD_v2N_SA_RESOURCE_INFO]`, and if present calls
   `assign_least_loaded_cpu()` to pick a CPU and set
   `larval_child->sa.st_v2_resource_info.cpu_id`.

But an Additional Child SA request from west is **not** a rekey — it
is sent via `queue_v2_CREATE_CHILD_SA_new_child_request()` (see
`submit_v2_CREATE_CHILD_SA_additional_child()`), with no `REKEY_SA`
notification. On the wire it's indistinguishable from an ordinary new
Child SA request with duplicate Traffic Selectors (which RFC 7296
2.8 explicitly allows).

That means east processes it through
`process_v2_CREATE_CHILD_SA_new_child_request()` instead —
the plain "new child" responder path. That function never looks at
`PD_v2N_SA_RESOURCE_INFO` and never calls `assign_least_loaded_cpu()`.
`larval_child->sa.st_v2_resource_info.cpu_id` is left at its default,
`CPU_ID_NONE` (`state.c:1356`), so when `kernel.c` builds the
`kernel_state` to install the SA (`kernel.c:1269`), `cpu_id` comes out
as `CPU_ID_NONE` and `XFRMA_SA_PCPU` is never attached
(`kernel_xfrm.c:1748`).

In short: Ondrej wired the CPU-assignment logic into the REKEY
responder path, but Additional Child SA creation actually flows
through the *new-child* responder path, which was never updated to
match.

## Design: CPU distribution as a hint, not a wire requirement

RFC 9611 §5.1 defines an optional "Resource Identifier" field in the
notify payload:

```
 |  Protocol ID  |   SPI Size    |      Notify Message Type      |
 +---------------+---------------+-------------------------------+
 ~               Resource Identifier (optional)                  ~
```

> "Resource Identifier (optional) — This opaque data may be set to
> convey the local identity of the resource."
> "The SA_RESOURCE_INFO notify payload MAY be empty or MAY contain
> some identifying data."

The field is explicitly restricted to informational use — §5.1 says a
receiver "MUST only use it for debugging purposes," and §7 (Security
Considerations) discourages putting hardware-identifying data in it.
So the design below treats it as **advisory only, never
authoritative**:

- **Initiator → responder (request)**: west includes its own chosen
  `cpu_id` as a 4-byte network-order integer in the Resource
  Identifier field for each Additional Child SA request — an
  opportunistic hint, not a directive.
- **Responder (east)**: tries to honor the hint — uses it only if it's
  in range for east's own CPU count and not already claimed by
  another Additional Child SA on that connection. Otherwise it falls
  back to its own local `assign_least_loaded_cpu()` selection
  (first-unused-CPU, then round-robin), exactly as if no hint had been
  sent. East never trusts the hint's format or range without
  validating it first — a non-libreswan RFC 9611 peer could put
  arbitrary opaque bytes there.
- **Responder → initiator (response)**: east echoes back whichever
  CPU it actually installed the SA on, again via the Resource
  Identifier field. West logs this for diagnostics ("asked for CPU 2,
  peer installed CPU 2" / "... installed CPU 1 instead") — it never
  feeds back into west's own already-installed kernel state.

This keeps CPU distribution what it fundamentally is on each host — a
*local* resource-scheduling decision tied to that host's own NIC
RX-queue/core layout — while opportunistically aligning the two sides'
numbering when possible, purely as a usability/debuggability nicety.
It degrades gracefully (same behavior as before) whenever the hint is
absent, malformed, out of range, or already taken, so a mismatched
CPU count or a non-libreswan peer never breaks the negotiation.

## Solution

Wire the same detection-and-assignment logic that already exists in
`process_v2_CREATE_CHILD_SA_rekey_child_request()` into
`process_v2_CREATE_CHILD_SA_new_child_request()` — the path Additional
Child SA requests actually take — and extend it with the hint/fallback
behavior above:

- `assign_least_loaded_cpu()` (`ikev2_create_child_sa.c`) gained an
  optional `(have_preferred_cpu_id, preferred_cpu_id)` pair: if the
  preferred CPU is in range and unused, it's returned directly;
  otherwise the existing first-unused/round-robin logic runs
  unchanged.
- A new `cpu_hint_from_v2N_SA_RESOURCE_INFO()` helper extracts a
  4-byte peer hint from a `SA_RESOURCE_INFO` notification, returning
  `false` (no hint) for anything else — empty, wrong size, or absent.
- `process_v2_CREATE_CHILD_SA_new_child_request()` now checks
  `md->pd[PD_v2N_SA_RESOURCE_INFO]`, enforces the existing
  `MAX_ADDITIONAL_SAS` limit (previously unchecked on this path),
  parses any peer hint, and calls `assign_least_loaded_cpu()` with it.
- `emit_v2_child_request_payloads()` (`ikev2_child.c`, initiator side)
  now sends its own `cpu_id` as the Resource Identifier data instead
  of an empty notification.
- `emit_v2_child_response_payloads()` (`ikev2_child.c`, responder
  side) now echoes the actually-installed `cpu_id` back in the
  `CREATE_CHILD_SA` response.
- `process_v2_CREATE_CHILD_SA_child_response()` (`ikev2_create_child_sa.c`,
  initiator side) logs a comparison between the CPU it requested and
  the CPU the responder echoed back — log-only, no behavioral effect.

With this in place, east independently distributes each incoming
Additional Child SA across its own CPUs, opportunistically matching
west's own numbering when the hint lines up — so `ip x s` on east
should show `pcpu-num` entries the same way west does, and both ends
genuinely spread the SAs across CPUs instead of only the initiator
doing so.

## Future work: CPU distribution is still config-driven, not traffic-driven

Libreswan's current CPU distribution has a real weakness: `clones=N`
eagerly creates all N Additional Child SAs right after the Initial SA
establishes, independent of whether traffic ever actually lands on
all N CPUs. It's a static guess, not a response to real load.

The way forward is reacting to `XFRM_MSG_ACQUIRE` from the kernel:
create an Additional Child SA for a given CPU only when the kernel
actually signals it needs one for that CPU, rather than pre-creating
a fixed count upfront. That both avoids wasted SAs for CPUs that see
no traffic and gets the CPU id directly from the kernel event that
needs it, instead of us guessing via sequential/round-robin
assignment. `kernel_xfrm.c` already handles `XFRM_MSG_ACQUIRE`
generally; extending it to recognize a per-CPU acquire and route it
into `submit_v2_CREATE_CHILD_SA_additional_child()` is future work,
not something this fix does.
