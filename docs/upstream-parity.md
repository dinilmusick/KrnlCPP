# KrnlTS → KrnlCPP pinned parity mapping

Authority: `KrnlTS@9fdd0eb792361dd72b57cb678b933ba0c8fcae3e` (`IMPLEMENTATION-SPEC.md`, `packages/core`). Native foundation evaluated against `GutCPP@443ad9397c081dd596e784c50cff14c1ecad95c6`. Distinguish a functional portable slice from full API, async and validation parity.

| Canonical component | Native implementation | Verification / remaining difference |
|---|---|---|
| `lifecycle/LifecycleManager.ts` | Preserved `include/krnl/lifecycle.hpp`, `Service::{initialize,start,stop,restart,dispose}` | Original lifecycle matrix tests remain; hook failure rollback, idempotent disposal tested; no async hook cancellation |
| `services/ServiceContract.ts` | `ServiceInfo`, `OperationInfo`, `InvocationContext`, typed `Result<T>`, `validate` | typed public consumer; no schema compiler or all TS dependency contract metadata |
| `functionalities/FunctionalityContract.ts` | `Service::add_operation<In,Out>`, `Service::invoke`, input/output validators | success/failure/exception tests; synchronous C++ functions only, not TS Promise semantics |
| `runtime/ServiceRuntime.ts` | `ServiceRuntime::{register_service,get,unregister_service,discover,start_all,stop_all,dispose_all}` | duplicate and capability tests; rollback in `start_all`; no dynamic module resolver |
| `registry/ServiceRegistry.ts` | service and operation metadata, service capability discovery | no separately stored endpoint registry/version replacement support |
| `state/ServiceStateStore.ts` | `StateStore::set/get/subscribe`, RAII lifecycle | mutable typed local values; no JSON-schema or TS immutable snapshot semantics |
| `events/ServiceEventBus.ts`, `functionalities/*` | `EventBus`, `Subscription`, per-functionality `:completed` / `:failed` | exceptions isolated; no service-owned application event definitions (matches canonical boundary); richer TS event envelopes not yet ported |
| `orchestration/FunctionalityOrchestration.ts`, `canonical/dagValidation.ts` | single-service `execute_flow` and dependency graph validation | cycle/missing dependency/ordered result tests; no parallel DAG engine or nested orchestration tree |
| `orchestration/canonical/KrnlQ.ts` | `InvocationQueue`, cooperative `Cancellation`, bounded pending/active and retries | deterministic capacity/retry/cancel tests; no PQueue promise handles, priorities, timeout/backoff, worker pool or `WorkerExecutionBoundary` parity |
| `services/ServiceProxy.ts`, `protocols/ProtocolAdapter.ts` | `ServiceProxy` + injectable `HostTransport`, `Endpoint` | protocol-neutral adapter test, typed response/failure; transport framing, remote lifecycle/state cache not yet ported |
| `telemetry/TelemetryController.ts` | `TraceBuffer`, correlation-aware bounded `InvocationTrace` | capped retention and error/success tests; no nested spans or timestamps/clock adapter yet |
| `packages/validate/src` | `validate(ServiceInfo)` and runtime input/output predicates | service identity checks only; AST, purity, directory, line-limit and tooling rules **not yet ported** |
| `packages/cli/src` | CMake package export, installed consumer | no `generate`/`validate` CLI parity yet |
| `GutCPP/include/gut/result.hpp` | opt-in `krnl/gutcpp_adapter.hpp` | error code string mapped to structured `Error`; core remains independent |

**Architectural exclusions:** no Skipo cross-service orchestration, no Kloks catalog policy, no React, no JavaScript runtime. Native protocol and host threading/filesystem boundaries are explicit.

**Release gate not satisfied until:** original lifecycle tests + new tests pass GCC and Clang strict modes, AddressSanitizer/UndefinedBehaviorSanitizer, a separately built installed consumer, and exact PR-head then merged-main GitHub CI. This is a substantial functional advance, not an unsupported parity-certification claim.
