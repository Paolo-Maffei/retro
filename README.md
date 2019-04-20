See <https://jeelabs.org/projects/retro/>.

## Repository layout

```
.
├── blink
│   ├── arm
│   │   └── src
│   └── esp
│       └── src
├── tests
│   └── circle407-sdcard
│       └── src
└── z80doc
    ├── code-z80
    ├── common-z80
    ├── cpm2-arm
    │   ├── include -> ../common-z80
    │   └── src
    ├── cpm2-native
    │   ├── include -> ../common-z80
    │   └── src
    ├── cpm3-arm
    │   ├── include -> ../common-z80
    │   └── src
    ├── fuzix-arm
    │   ├── include -> ../common-z80
    │   └── src
    ├── fuzix-esp
    │   ├── data
    │   ├── include -> ../common-z80
    │   └── src
    ├── fuzix-native
    │   ├── include -> ../common-z80
    │   └── src
    ├── zexall-arm
    │   ├── include -> ../common-z80
    │   └── src
    └── zexall-native
        ├── include -> ../common-z80
        └── src
```
