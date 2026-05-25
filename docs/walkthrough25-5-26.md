# Walkthrough - CAN Simulator to Kuksa Databroker Integration

We have successfully implemented and verified the Python bridge script that captures WebSockets data from the CAN simulator and feeds it to the Kuksa Databroker as standardized COVESA VSS attributes.

---

## 1. Accomplishments

### Created the Feeder Bridge Script
We created the [kuksa_feeder.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/simulator/kuksa_feeder.py) script in the `simulator/` directory. It uses `asyncio` and `websockets` to receive CAN simulator broadcasts, translates them, and publishes them using the `kuksa-client` gRPC protocol.

### Mapping Implementation Details
- **Simulator `started` (bool)** $\rightarrow$ mapped to `Vehicle.Powertrain.CombustionEngine.IsRunning` (boolean).
- **Simulator `speed` (float)** $\rightarrow$ mapped to `Vehicle.Speed` (float).
- **Simulator `rpm` (int)** $\rightarrow$ mapped to `Vehicle.Powertrain.CombustionEngine.Speed` (uint16).
- **Simulator `gear` (int)** $\rightarrow$ mapped to `Vehicle.Powertrain.Transmission.CurrentGear` (int8).
- **Simulator `lat` / `lon` (float)** $\rightarrow$ mapped to `Vehicle.CurrentLocation.Latitude` / `Longitude` (double).
- **Simulator `pedals.gas` / `pedals.brake` (bool)** $\rightarrow$ mapped to `Vehicle.Chassis.Accelerator/Brake.PedalPosition` (uint8: `100` if pressed, `0` if released).

---

## 2. Verification Results

We verified the integration end-to-end:
1. **Initial Uninitialized Check:** Connecting to Kuksa Databroker before starting the feeder returned empty/`None` values.
2. **Telemetry Propagation:** When the CAN simulator was running, the feeder successfully updated Kuksa Databroker.
3. **Dynamic Value Verification:** We simulated starting the engine and pressing the gas pedal:
   - `Vehicle.Powertrain.CombustionEngine.IsRunning` updated to **`True`**
   - `Vehicle.Chassis.Accelerator.PedalPosition` updated to **`100`**
   - `Vehicle.Speed` increased dynamically to **`52.8 km/h`**
   - `Vehicle.Powertrain.Transmission.CurrentGear` auto-shifted to **`3`**
   - GPS Coordinates (`Vehicle.CurrentLocation.Latitude`/`Longitude`) changed according to simulated movement.

---

## 3. How to Run Locally

To run the simulator and the feeder bridge:

1. **Start the Kuksa Databroker (Docker):**
   Ensure Docker is running and execute:
   ```powershell
   docker run -it --rm --name kuksa-databroker -p 55555:55555 -v "c:\Users\xtrem\Downloads\CPlusPlus\CAN CTRL\Kuksa-vss-data\vss_rel_4.1_noexpand.json:/data/vss.json" ghcr.io/eclipse-kuksa/kuksa-databroker:latest --insecure --vss /data/vss.json
   ```

2. **Start the CAN Simulator:**
   In your workspace, run:
   ```powershell
   .\venv\Scripts\python simulator\main.py
   ```

3. **Start the Kuksa Feeder:**
   In a separate terminal, run:
   ```powershell
   .\venv\Scripts\python simulator\kuksa_feeder.py
   ```
