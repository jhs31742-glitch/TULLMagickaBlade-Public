# [TULL] Magicka Blade

A Magicka-based weapon system for **The Elder Scrolls V: Skyrim Special Edition**.

The mod combines an ESP with a native SKSE/CommonLibSSE-NG plugin. Its main mechanic converts the player's **maximum Magicka** into weapon damage and resource cost, with separate melee and ranged rules.

## Runtime compatibility

**Tested by the author**

- Skyrim Special Edition **1.5.97**
- SKSE **2.0.20**

**Build targets**

- SE target: enabled
- AE target: enabled
- VR target: disabled

The DLL is built with CommonLibSSE-NG for SE/AE targets, but the author has only personally tested **SE 1.5.97**.

**AE / 1.6.x is unverified.**  
**VR is not supported.**

The AE melee path intentionally leaves physical-damage mutation disabled because that path has not been validated.

## Runtime requirements

For the complete mod package:

- SKSE64
- Address Library for SKSE Plugins
- `[TULL] Magicka Blade.esp`
- Backported Extended ESL Support (BEES) when required by the user's Skyrim runtime/plugin-header support
- Container Item Distributor (CID) for the spell-tome vendor distribution included with the Nexus release
- Any requirements of the above mods

The native DLL looks up forms from `[TULL] Magicka Blade.esp`, so the ESP is part of the required runtime package.

## Gameplay mechanics

### Magicka Conversion — melee

Eligible enchanted melee weapons use the player's Magicka as the authoritative conversion resource.

- Normal attack cost: **5% of Max Magicka**
- Power attack cost: **10% of Max Magicka**
- One-handed conversion multiplier: **0.80x**
- Two-handed conversion multiplier: **1.00x**
- Normal conversion multiplier: **1.00x**
- Power-attack conversion multiplier: **2.40x**
- If current Magicka is lower than the required cost, conversion damage is not applied.
- Poison attached to Magicka Conversion weapons is intentionally removed.

On the tested SE 1.5.97 path, the plugin zeros the physical melee HitData fields before applying the Magicka-based conversion damage.

### Bound scaling

Bound Magicka weapons scale their conversion rate with the player's Conjuration skill:

| Conjuration | Conversion rate |
| ---: | ---: |
| below 25 | 5% |
| 25+ | 6.25% |
| 50+ | 7.5% |
| 75+ | 8.75% |
| 100+ | 10% |

### Ranged conversion

Ranged conversion keeps normal projectile damage and adds Magicka-based damage on valid Actor impacts.

- Shot prepay: **12% of Max Magicka**
- First valid Actor hit refund: **8% of Max Magicka**
- Misses keep the full 12% cost.
- If the shot cannot pay the required Magicka cost, Magicka bonus damage and refund are not approved.
- Multi-projectile siblings from the same firing action share one cost/refund state.
- Standard bow shots use `TESPlayerBowShotEvent`.
- Compatible non-standard projectile weapons can fall back to projectile-authoritative shot creation.
- Projectile Power is intentionally not used as the grouping key.
- Projectile poison is removed locally.

## Source structure

```text
.
├─ src/
│  ├─ Plugin.cpp
│  ├─ PCH.h
│  └─ Utils.h
├─ cmake/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ vcpkg.json
├─ .gitmodules
├─ .gitignore
├─ LICENSE
└─ THIRD_PARTY_NOTICES.md
```

The source archive does **not** vendor the full CommonLibSSE-NG or vcpkg repositories.

## CommonLibSSE-NG version

This release was built against:

- CommonLibSSE-NG **v4.18.0**
- Commit: `8c4025b01fac2bea1bbe73a3a9da7b4fde338343`
- License at that commit: **MIT**

The repository submodule path is:

```text
lib/commonlibsse-ng
```

## Build requirements

The Windows build used for development was based on:

- Visual Studio / MSVC with Desktop development with C++
- CMake 3.21+
- Ninja
- vcpkg
- CommonLibSSE-NG v4.18.0

The project uses **C++23**.

### Clone from GitHub

If the repository is published with submodules:

```bash
git clone --recurse-submodules <repository-url>
cd TULL-Magicka-Blade
```

If you cloned without submodules:

```bash
git submodule update --init --recursive
```

### Building from the Nexus source ZIP

The Nexus source ZIP contains the project source, but not the full submodule contents.

Prepare:

```text
lib/commonlibsse-ng
lib/vcpkg
```

Use CommonLibSSE-NG v4.18.0 at:

```text
8c4025b01fac2bea1bbe73a3a9da7b4fde338343
```

Then bootstrap vcpkg as required by your environment.

### Windows build

Run from an x64 Visual Studio Developer PowerShell / Developer Command Prompt:

```powershell
cmake --preset release-windows
cmake --build --preset release-windows
```

The resulting DLL is placed under:

```text
build/msvc/tullmagickablade.dll
```

The CMake preset expects vcpkg at:

```text
lib/vcpkg
```

## Project dependencies

The project manifest directly references:

- CommonLibSSE-NG
- spdlog
- DirectXTK
- rapidcsv
- vcpkg as the package/build tool

See `THIRD_PARTY_NOTICES.md` for license information.

## Generative AI disclosure

Generative AI tools were used during development as an engineering assistant for tasks including:

- C++ code drafting and review
- debugging and crash-log analysis
- documentation and English-language editing
- promotional images used on the Nexus mod page

Gameplay design, implementation decisions, testing, balancing, release decisions, and final validation were directed by the author.

The released mod does not intentionally include AI-generated voice acting, dialogue, music, character models, weapon models, or other AI-generated in-game visual assets.

## License

The original source code of **[TULL] Magicka Blade** is released under the **MIT License**.

See `LICENSE`.

Third-party libraries remain under their respective licenses. See `THIRD_PARTY_NOTICES.md`.

## Author

**TULL**

## Disclaimer

This is an unofficial fan-made mod for Skyrim Special Edition and is not affiliated with or endorsed by Bethesda Game Studios.
