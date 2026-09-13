# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESPHome component library for controlling Sunster and protocol-compatible diesel heaters (Chinese-style VEVOR/XMZ-D2) via UART. Provides three control modes (Manual, Automatic with PI controller, Antifreeze), fuel consumption tracking, and native Home Assistant climate entity integration.

Protocol based on reverse-engineering from [vevor_heater_control](https://github.com/zatakon/vevor_heater_control). UART @ 4800 baud, binary frames starting with 0xAA.

**Status**: Highly beta, active development, breaking changes expected.

## Build & Development Commands

This is an ESPHome external component — use ESPHome CLI:

```bash
esphome config esphome.yaml      # Validate YAML configuration
esphome compile esphome.yaml     # Compile firmware
esphome run esphome.yaml         # Compile and upload to device
esphome upload esphome.yaml      # Upload pre-built firmware
esphome logs esphome.yaml        # Monitor serial logs
```

For local development, use `example_develop.yaml` (references component via local path `source: components`). Production configs reference the GitHub repo.

No unit tests or linting tools configured.

## Architecture

### Core Component (6 files, ~3100 lines)

```
components/sunster_heater/
├── __init__.py              # ESPHome config schema, validation, code generation (to_code)
├── climate.py               # HA climate entity registration
├── sunster_heater.h         # Main class declaration, enums, structs
├── sunster_heater.cpp       # Core: UART protocol, state machine, PI controller, fuel tracking
├── sunster_climate.h        # Climate entity header
└── sunster_climate.cpp      # Climate entity (thermostat mode/temp forwarding to HA)
```

### Key Class: `SunsterHeater`

Inherits `PollingComponent` + `UARTDevice`. Central orchestrator that:
- Sends/receives UART frames to the heater hardware
- Manages heater state machine (OFF → STARTING → PREHEATING → STABLE_COMBUSTION → STOPPING)
- Runs the active control mode each update cycle
- Tracks fuel consumption with persistent NVS storage
- Auto-creates ~15 sensors (temperature, voltage, fan, pump, fuel, PI output, etc.)

### Control Modes

- **Manual**: Direct power level (10–100%), no temperature regulation
- **Automatic**: PI controller with temperature prediction (`T_pred = T_measured + slope × t_lookahead`). Uses on/off thresholds (hysteresis). See `PI_CONTROLLER_GUIDE.md`.
- **Antifreeze**: Zone-based power levels at temperature thresholds (2/6/8/9°C) with hysteresis

### Data Flow

1. `__init__.py` defines the YAML schema and generates C++ setup code via `to_code()`
2. `SunsterHeater::setup()` loads persistent config from NVS, registers sensor callbacks
3. `SunsterHeater::update()` (polling loop): `check_uart_data()` → parse response → run active mode handler → send command frame
4. `SunsterClimate` bridges heater state to Home Assistant's climate platform

### Persistent Storage

Uses ESPHome preferences (NVS flash). Saves PI parameters, target temperature, fuel totals. Config writes are debounced (2s) to avoid blocking control callbacks.

### Home Assistant Integration

`home-assistant/` contains supplementary YAML for HA dashboards:
- `template-sensor.yaml` — UI mode detection (temperature vs power)
- `input-number.yaml` — power level slider
- `automation.yaml` — syncs power slider ↔ climate entity
- `sunster-climate-dashboard.yaml` — conditional Lovelace cards per mode

## Conventions

- **Language**: Communicate with the developer in German. All code, comments, commit messages, and documentation in English.
- **Commit style**: Conventional commits with scope: `feat(scope):`, `fix(scope):`, `refactor(scope):`, `perf:`. Single line, English, no Co-Authored-By.
- **Testing workflow**: Always commit and push so the developer can test on real hardware.
- **Sensor auto-creation**: Sensors are created automatically in `__init__.py` `to_code()` unless explicitly disabled — don't duplicate sensor definitions in YAML
- **Configuration keys**: snake_case per ESPHome conventions (e.g., `pi_kp`, `t_lookahead`, `slope_window`)
- **Version**: Embedded in YAML configs under `esphome.project.version`, currently `1.2.0`
