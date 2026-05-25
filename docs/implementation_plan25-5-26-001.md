# Implementation Plan - Simulating CAN Data Feed to Kuksa Databroker

This plan describes how to download/run the **Kuksa Databroker** locally using Docker, and how to create a **Feeder Bridge** script (`kuksa_feeder.py`) that translates telemetry from the CAN bus simulator and pushes it to the databroker.

---

## 1. Running the Kuksa Databroker

We will run the official Kuksa Databroker docker container. Since the databroker requires a VSS definition schema, we will mount our local `vss_rel_4.1_noexpand.json` from the `Kuksa-vss-data` folder.

### Docker Run Command (for Windows PowerShell)
```powershell
docker run -it --rm --name kuksa-databroker `
  -p 55555:55555 `
  -v "c:\Users\xtrem\Downloads\CPlusPlus\CAN CTRL\Kuksa-vss-data\vss_rel_4.1_noexpand.json:/data/vss.json" `
  ghcr.io/eclipse-kuksa/kuksa-databroker:latest `
  --insecure `
  --vss /data/vss.json
```

---

## 2. Proposed Changes

We will create a new Python script inside the `simulator` directory that acts as the bridge.

### [Component: Simulator Bridge]

#### [NEW] [kuksa_feeder.py](file:///c:/Users/xtrem/Downloads/CPlusPlus/CAN%20CTRL/simulator/kuksa_feeder.py)
This script will:
1. Connect to the CAN Simulator WebSocket at `ws://127.0.0.1:8083`.
2. Connect to the Kuksa Databroker gRPC server at `127.0.0.1:55555`.
3. Periodically receive telemetry updates from the simulator, translate them to VSS syntax, and publish them to Kuksa Databroker.

### Attribute Mapping Specification

| Simulator Key | VSS v4.1 Target Path | VSS DataType | Mapping Rule |
| :--- | :--- | :--- | :--- |
| `started` | `Vehicle.Powertrain.CombustionEngine.IsRunning` | `boolean` | Direct boolean assignment |
| `speed` | `Vehicle.Speed` | `float` | Direct float assignment |
| `rpm` | `Vehicle.Powertrain.CombustionEngine.Speed` | `uint16` | Cast to integer |
| `gear` | `Vehicle.Powertrain.Transmission.CurrentGear` | `int8` | Cast to integer |
| `lat` | `Vehicle.CurrentLocation.Latitude` | `double` | Direct double assignment |
| `lon` | `Vehicle.CurrentLocation.Longitude` | `double` | Direct double assignment |
| `pedals.gas` | `Vehicle.Chassis.Accelerator.PedalPosition` | `uint8` | `100` if `True` else `0` |
| `pedals.brake` | `Vehicle.Chassis.Brake.PedalPosition` | `uint8` | `100` if `True` else `0` |

---

## 3. Verification Plan

### Automated / Manual Verification
1. **Start CAN Simulator:** Start `simulator/main.py`.
2. **Start Databroker:** Run the Docker command above and verify the databroker starts and successfully loads the VSS file.
3. **Start Feeder Bridge:** Run the new feeder script `python simulator/kuksa_feeder.py` using the activated virtual environment.
4. **Inspect Values in Kuksa:** Run the `kuksa-client` CLI tool or a verification script to read the active paths (e.g. `Vehicle.Speed`) and confirm they match the simulator's speed.
