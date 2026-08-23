# ESPressio Persistence dependency position

ESPressio Persistence 0.1.0 deliberately has **no required dependency on another ESPressio library**.

```text
Persistence 0.1.0
    -> none
```

The core abstraction is intended to sit beneath higher-level facilities such as Serializable document persistence and ESPressio-Web static-asset storage.

Planned optional integration direction:

```text
Persistence
    - - -> Serializable   typed object/document persistence
    - - -> Security       encryption/integrity policy (future)

Web
    - - -> Persistence    static assets/configuration (future)
```

These optional edges are architectural direction only; they are not part of Persistence 0.1.0.
