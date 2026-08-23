# ESPressio Persistence dependency position

ESPressio Persistence 0.2.0 keeps its **core storage layer dependency-free** while adding an opt-in integration with ESPressio Serializable.

```text
Persistence core 0.2.0
    -> none

Persistence Serializable integration
    - - -> Serializable >= 0.10.3 < 1.0.0
```

The optional edge exists only for consumers that include:

```cpp
#include <ESPressio_Persistence_Serializable.hpp>
```

Core consumers using `ESPressio_Persistence.hpp` or `ESPressio_ESP32Persistence.hpp` do not acquire Serializable as a required dependency.

Dependency direction is intentionally one-way:

```text
Serializable
    ^
    : optional
    :
Persistence
```

Serializable remains foundational and has no knowledge of Persistence or storage media.

Future optional integration direction:

```text
Persistence
    - - -> Security       encryption/integrity policy (future)

Web
    - - -> Persistence    static assets/configuration (future)
```
