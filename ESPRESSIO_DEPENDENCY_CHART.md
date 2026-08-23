# ESPressio Persistence dependency position — 0.3.0

ESPressio Persistence keeps its **core storage layer dependency-free** while exposing opt-in typed and protected-typed integrations.

```text
Persistence core 0.3.0
    -> none

Persistence Serializable integration
    - - -> Serializable >= 0.11.0 < 1.0.0

Persistence protected Serializable integration
    - - -> Serializable >= 0.11.0 < 1.0.0
            - - -> Security >= 0.4.0 < 1.0.0
```

Headers make the distinction explicit:

```cpp
#include <ESPressio_Persistence.hpp>
#include <ESPressio_Persistence_Serializable.hpp>
#include <ESPressio_Persistence_Serializable_Security.hpp>
```

Persistence does **not** depend directly on Security. It consumes Serializable's protection configuration and protected archive API; Serializable delegates authenticated encryption to Security.

Dependency direction remains one-way:

```text
Security
   ^ optional through Serializable
Serializable
   ^ optional
Persistence
```

Neither Security nor Serializable knows about storage media or depends back on Persistence.

This positioning is intended to support downstream consumers such as ESPressio WiFi and ESPressio Web without coupling those libraries to LittleFS/NVS/SD implementations or to cryptographic primitives.
