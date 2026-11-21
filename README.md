# KrnlCPP

Portable, header-only **C++17** foundation of the KrnlTS backend-service kernel. Target: `dinilmusick/KrnlTS@9fdd0eb792361dd72b57cb678b933ba0c8fcae3e`; optional native foundation: `dinilmusick/GutCPP@443ad9397c081dd596e784c50cff14c1ecad95c6`.

```cpp
#include <krnl/kernel.hpp>
krnl::Service service({"math", "Math", "1.0", {"arithmetic"}, {"memory"}});
service.add_operation<int,int>({"double","",{},{}}, [](int in, const krnl::InvocationContext&, krnl::Cancellation) {
  return krnl::Result<int>(in*2);
});
service.initialize(); service.start();
auto value = service.invoke<int,int>("double",21); // value.value() == 42
service.dispose();
```

Use `Result<T>::ok()` before accessing `value()` or `error()`. `OperationInfo` carries explicit input/output RTTI, capability and protocol metadata. Handlers return typed `Result<Out>`; runtime converts exceptions and validation failures into deterministic `Error` codes. `std::any` is confined to erased execution/protocol boundaries. `Cancellation` is cooperative. Subscriptions own their removal and must be kept alive for callbacks to fire.

Lifecycle: `initialize` → `start` → `stop` → `start` (restart) → `dispose`. Failed hooks roll back to the canonical initial/ready/running state; disposal is terminal and idempotent. In-flight synchronous invocations finish before `stop()` or `dispose()` returns. **Do not call `stop()` or `dispose()` from the same service's running operation** (it would wait on its own invocation). Hooks should not re-enter transitions while a transition is ongoing.

`InvocationQueue` is a capacity-bounded **caller-driven executor**. Host workers invoke `pump_one`; `max_active` caps concurrent pumps without mandating a specific thread runtime. Retry only applies to errors explicitly marked retryable. No automatically sleeping/backoff host dependency is introduced. A `FlowStep` references only an operation in the already-bound service; cross-service orchestration belongs to Skipo, catalogs to Kloks. No JavaScript or React linkage is used.

Build and test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake --install build --prefix /tmp/krnlcpp-install
cmake -S consumer -B consumer/build -DCMAKE_PREFIX_PATH=/tmp/krnlcpp-install
cmake --build consumer/build && consumer/build/consumer
```

`docs/upstream-parity.md` identifies implemented behavior and documented remaining gaps; **this is not yet full KrnlTS parity**. The base header does not require GutCPP; `include/krnl/gutcpp_adapter.hpp` is optional and targets the pinned `gut/result.hpp` contract. The portable core does not own threads, networking, filesystem or clock integrations; it depends on C++17 `std::mutex`, `std::any`, and exceptions, so freestanding/bare-metal without those features requires an adapter or reduced profile.
