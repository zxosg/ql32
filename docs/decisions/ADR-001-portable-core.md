# ADR-001: Keep the QL/32 core independent of hardware libraries

**Status:** Accepted

The language frontend, VM, scheduler, window semantics, and public service
interfaces are portable C++. Platform adapters implement those interfaces.
FabGL is a VGA backend, not the public graphics or window-management API;
similarly, ESP-IDF LCD and LVGL are optional TFT implementation details.

This makes Linux testing authoritative for portable behavior and permits
separate VGA and TFT firmware images without language forks.

