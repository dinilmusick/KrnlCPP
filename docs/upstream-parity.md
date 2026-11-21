# KrnlTS → KrnlCPP pinned parity mapping

Authority: `KrnlTS@9fdd0eb792361dd72b57cb678b933ba0c8fcae3e` (`IMPLEMENTATION-SPEC.md`, `packages/core`). Native foundation evaluated against `GutCPP@443ad9397c081dd596e784c50cff14c1ecad95c6`. Distinguish a functional portable slice from full API, async and validation parity.

| Canonical component | Native implementation | Verification / remaining difference |
|---|---|---|
| `lifecycle/LifecycleManager.ts` | Preserved `include/krnl/lifecycle.hpp`, `Service::{initialize,start,stop,restart,dispose}` | Original lifecycle matrix tests remain; hook failure rollback, concurrent disposal serialization, in-flight cleanup and idempotent disposal tested; no async hook cancellation |
| `services/ServiceContract.ts` | `ServiceInfo`, `OperationInfo`, `InvocationContext`, typed `Result<T>`, `validate` | public consumer; identifier/capability/protocol validation rejects duplicates/unknown protocol strings; no JSON-schema compiler or full TS dependency metadata |
| `functionalities/FunctionalityContract.ts` | `Service::add_operation<In,Out>`, `Service::invoke`, input/output validators | success/failure/exception tests; synchronous C++ functions only, not TS Promise semantics |
| `runtime/ServiceRuntime.ts` | `ServiceRuntime::{register_service,get,unregister_service,discover,start_all,stop_all,dispose_all}` | duplicate and capability tests; rollback in `start_all`; no dynamic module resolver |
| `registry/ServiceRegistry.ts` | service and operation metadata, service capability discovery | no separately stored endpoint registry/version replacement support |
| `state/ServiceStateStore.ts` | `StateStore::set/get/subscribe`, RAII lifecycle | mutable typed local values; no JSON-schema or TS immutable snapshot semantics |
| `events/ServiceEventBus.ts`, `functionalities/*` | `EventBus`, `Subscription`, per-functionality `:completed` / `:failed` | exceptions isolated; no service-owned application event definitions (matches canonical boundary); richer TS event envelopes not yet ported |
| `orchestration/FunctionalityOrchestration.ts`, `canonical/dagValidation.ts` | single-service `execute_flow` and dependency graph validation | cycle/missing dependency/ordered result tests; no parallel DAG engine or nested orchestration tree |
| `orchestration/canonical/KrnlQ.ts` | `InvocationQueue` (existing), `JobQueue`, `JobHandle`, `JobPolicy`, `JobStatus` in `job_queue.hpp` | independent host-driven tests cover priority/FIFO, bounded pending and active, cancellation, retryable fixed/exponential backoff, deterministic pending deadline and typed result handles; no automatic worker pool / Promise event loop or forced active termination |
| `services/ServiceProxy.ts`, `protocols/ProtocolAdapter.ts` | `ServiceProxy` + injectable `HostTransport`, `Endpoint` | protocol-neutral adapter test, typed response/failure; transport framing, remote lifecycle/state cache not yet ported |
| `telemetry/TelemetryController.ts` | `TraceBuffer`, correlation-aware bounded `InvocationTrace` | capped retention and error/success tests; no nested spans or timestamps/clock adapter yet |
| `packages/validate/src` | `validate(ServiceInfo)` and runtime input/output predicates | service identity checks only; AST, purity, directory, line-limit and tooling rules **not yet ported** |
| `packages/cli/src` | CMake package export, installed consumer | no `generate`/`validate` CLI parity yet |
| `GutCPP/include/gut/result.hpp` | opt-in `krnl/gutcpp_adapter.hpp` | error code string mapped to structured `Error`; core remains independent |

**Architectural exclusions:** no Skipo cross-service orchestration, no Kloks catalog policy, no React, no JavaScript runtime. Native protocol and host threading/filesystem boundaries are explicit.

**Release gate not satisfied until:** original lifecycle tests + new tests pass GCC and Clang strict modes, AddressSanitizer/UndefinedBehaviorSanitizer, a separately built installed consumer, and exact PR-head then merged-main GitHub CI. This is a substantial functional advance, not an unsupported parity-certification claim.

## Wave 2 closure delta (issue #4)

- Native `JobQueue` is a **caller-driven** host boundary rather than a fake background worker; its `pump_one(now_ms)` contract makes backoff and queue deadlines deterministic without OS timing. `JobHandle` supplies a shared terminal future, cancellation and typed `Result<T>` retrieval. Retries apply only to explicitly retryable failures. Shutdown cancels active job tokens and the destructor waits for active pumps.
- `Service::dispose()` is now terminal and serialized across concurrent callers, waits for transitional lifecycle hooks and in-flight executions, refuses new operations during disposal and calls `on_dispose` once. `Service::call()` owns an RAII in-flight reservation, so a telemetry failure cannot strand a pending disposal.
- Upstream `packages/validate` still needs an implementation of full JSON schemas / AST-based source rules / CLI. Host-driven pending deadlines intentionally do not imply forcibly aborting active native execution. There is no linked JavaScript or React runtime.
