# PI Controller with Temperature Prediction for Diesel Air Heaters

## Problem Statement

### System Overview

We control a diesel air heater (e.g. Vevor/Chinese Diesel Heater) in a camper via ESPHome. The heater has an adjustable power range of **10–100%**. The goal is stable room temperature without overshoot.

### Why a Standard PID Fails

The system has several properties that cause problems for a classic PID controller:

**1. Slow temperature sensor (dead time 1–3 minutes)**  
The sensor reports temperature with significant delay. When the controller reacts to the measured value, the real temperature has already moved. The controller brakes too late and temperature overshoots the setpoint.

**2. Thermal inertia of the heat exchanger**  
Even after reducing power, the hot heat exchanger continues to release heat for minutes. This increases overshoot.

**3. Minimum power of 10%**  
Below 10% the heater switches off and runs a cooldown cycle, followed by a startup cycle (glow plug, ramp-up). That takes several minutes and is inefficient. A PID trying to regulate below 10% causes constant on/off cycling.

**4. Asymmetric behaviour**  
The heater can heat actively (fast) but only cool passively (slow, depending on insulation and outside temperature). A symmetric PID does not account for this.

### Result with Standard PID

- With a small temperature difference (e.g. 2°C below setpoint): PID runs at full power → strong overshoot  
- After overshoot: power is reduced to minimum or heater turns off → cooldown → temperature drops → restart → oscillation  
- The I-term keeps integrating during the dead time → increases overshoot (windup)

---

## Approach: PI Controller with Predictive Input

### Core Idea

Instead of feeding the PI controller the **current (delayed) measurement** as the process value, we compute a **predicted temperature** and use that as input:

```
T_pred = T_measured + slope × t_lookahead
```

- `T_measured` = current measured temperature  
- `slope` = filtered temperature rate (°C/s)  
- `t_lookahead` = lookahead time in seconds (≈ estimated system delay)

The PI controller then acts on the **error between setpoint and predicted value**:

```
error = T_target − T_pred
```

### Why This Works

| Situation              | slope    | T_pred vs T_measured | Effect                                                                 |
|------------------------|----------|----------------------|------------------------------------------------------------------------|
| Heating, temp rising   | positive | T_pred >> T_measured | Controller “sees” setpoint approaching → brakes early                 |
| Approaching setpoint  | small    | T_pred ≈ T_measured | Controller behaves like a normal PI                                    |
| Stable at setpoint    | ≈ 0      | T_pred ≈ T_measured | Controller holds power (I-term has found holding power)                |
| Cooling down          | negative | T_pred < T_measured | Controller reacts earlier and increases power before the sensor shows it |

**Important:** In steady state (slope ≈ 0) the controller behaves like a normal PI. Prediction only matters in dynamic phases—exactly where a standard PI fails.

---

## Controller Structure

### Block Diagram

```
  T_measured ──────► │  Compute slope   │
                     │  (filtered)      ├──► slope
                     └─────────────────┘
                            │
                            ▼
                     ┌─────────────────┐
  T_measured ──────► │  Prediction     │
  t_lookahead ─────► │                 ├──► T_pred
                     │ T_meas + slope  │
                     │  × t_lookahead  │
                     └─────────────────┘
                            │
                            ▼
               ┌────────────────────────┐
  T_target ──► │  error =               │
               │  T_target − T_pred     ├──► error
               └────────────────────────┘
                            │
                            ▼
               ┌────────────────────────┐
               │  PI controller        │
               │  output = Kp×err + I   │
               │  output ∈ [-100, +100]%├──► output_raw
               └────────────────────────┘
                            │
                            ▼
               ┌────────────────────────┐
               │  On/off by thresholds  │
               │  off if out < off_thr  │
               │  on  if out > on_thr   │
               │  Power 10–100% when on │
               └────────────────────────┘
```

### Parameters (this component)

| Parameter              | Description                          | Typical      |
|------------------------|--------------------------------------|-------------|
| `Kp`                   | Proportional gain                    | e.g. 10–20  |
| `Ki`                   | Integral gain                        | e.g. 0.1–0.2|
| `t_lookahead`          | Lookahead time [s]                   | 60–120 s    |
| `slope_window`         | EMA window for slope [s]             | 30–60 s     |
| `output_off_threshold` | Heater off when output < this [%]    | e.g. -10    |
| `output_on_threshold`  | Heater on when output > this [%]     | e.g. +10    |
| Min power when on      | 10%                                  | Do not go below or heater shuts off |

---

## Implementation in this component

- **Pure PI** (no D-term): `output_raw = Kp×error + I`, clamped to **±100%**.
- **On/off by thresholds:** Heater turns off when `output_raw < output_off_threshold` (e.g. -10%), turns on when `output_raw > output_on_threshold` (e.g. +10%). No separate on/off delay timers.
- **Prediction always active:** `T_pred = T_measured + slope_filtered × t_lookahead`; slope from EMA of (T_now - T_prev)/dt.
- **Temperature:** Sensor at 5 s, 12-bit; all math in float.
- **Min-on time:** After stable combustion, heater stays on for a configurable min time before threshold-based turn-off.

---

## Tuning

### Step 1: Choose t_lookahead

1. Run heater at fixed power (e.g. 50%).  
2. Wait until temperature is stable.  
3. Step power to 80%.  
4. Measure how long until the sensor shows a clear change.  
5. That time ≈ `t_lookahead` (often 60–120 s).

### Step 2: Set Kp roughly

1. Set Ki to 0 (P-only).  
2. Set target temperature.  
3. Set Kp so that at ΔT = 5°C the output is about 50–70% above expected holding power.  
4. Some overshoot (1–2°C) is acceptable; I will correct it later.

### Step 3: Increase Ki gradually

1. Increase Ki step by step.  
2. Watch how quickly temperature settles to setpoint.  
3. Aim: stable at setpoint within 10–15 minutes without persistent oscillation.  
4. If temperature starts to oscillate, reduce Ki.

### Step 4: Fine-tuning

- Overshoot when heating → increase `t_lookahead`.  
- Too slow reaction to disturbances (e.g. door open) → decrease `t_lookahead` or increase Kp.  
- Temperature oscillates around setpoint → reduce Ki.  
- Steady offset from setpoint → increase Ki.

---

## Summary

```
Standard PID:   error = T_target − T_measured        → reacts too late
This approach:  error = T_target − (T_measured + slope×t_lookahead)  → reacts in time
```

The controller is a **normal PI** with a smarter input. As a result:

- No overshoot when heating (prediction brakes in time).  
- Fast reaction to cooling (prediction sees falling temperature before the sensor).  
- Stable holding power (I-term converges when slope → 0).  
- No constant on/off (power stays above 10% when on).  
- One extra parameter (`t_lookahead`) compared to a standard PI, plus thresholds for on/off.
