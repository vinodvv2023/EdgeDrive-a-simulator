import asyncio
import websockets
import json
import os
import paho.mqtt.client as mqtt

# ─── Load Simulation Config ───────────────────────────────────────────────────
def _load_sim_cfg():
    cfg_path = os.path.join(os.path.dirname(__file__), "simulation_config.json")
    try:
        with open(cfg_path, "r") as f:
            cfg = json.load(f)
        print(f"[SimCfg] Loaded simulation_config.json")
        return cfg
    except Exception as e:
        print(f"[SimCfg] WARNING: Could not load simulation_config.json: {e}")
        return {}

_sim_cfg = _load_sim_cfg()
_sensors = _sim_cfg.get("sensors", {})

# Pull tyre config as list
_tyre_cfg = _sensors.get("tyre_pressures", [
    {"start": 32.0, "rate_per_tick": -0.05, "clamp_min": 10.0, "only_when_running": True},
    {"start": 32.0, "rate_per_tick":  0.0,  "clamp_min": 10.0, "only_when_running": False},
    {"start": 32.0, "rate_per_tick":  0.0,  "clamp_min": 10.0, "only_when_running": False},
    {"start": 32.0, "rate_per_tick":  0.0,  "clamp_min": 10.0, "only_when_running": False},
])

_fuel_cfg     = _sensors.get("fuel_level",  {"start": 100.0, "rate_per_tick": -0.1417, "clamp_min": 0.0,   "only_when_running": True})
_coolant_cfg  = _sensors.get("coolant_temp", {"start": 40.0,  "rate_per_tick":  0.05,   "clamp_max": 110.0, "fan_trigger_temp": 95.0, "fan_cooling_rate": -0.03, "only_when_running": True})

# Initialize MQTT Client
try:
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
except AttributeError:
    mqtt_client = mqtt.Client()

# Physics state
speed = 0.0
rpm = 0
gear = 1
lat, lon = 40.7128, -74.0060

is_started = False
gas_pressed = False
brake_pressed = False

# Sensor states — initialised from simulation_config.json
fuel_level     = float(_fuel_cfg.get("start", 100.0))
coolant_temp   = float(_coolant_cfg.get("start", 40.0))
cooling_fan    = False
tyre_pressures = [float(t.get("start", 32.0)) for t in _tyre_cfg]

async def handle_client(websocket):
    global is_started, gas_pressed, brake_pressed, gear
    try:
        async for message in websocket:
            try:
                cmd = json.loads(message)
                action = cmd.get("action")
                val = cmd.get("value")
                
                if action == "start":
                    is_started = True
                elif action == "gas":
                    gas_pressed = val
                elif action == "brake":
                    brake_pressed = val
            except:
                pass
    except websockets.exceptions.ConnectionClosed:
        pass

async def simulate(websocket):
    global speed, rpm, gear, lat, lon, is_started, gas_pressed, brake_pressed
    global fuel_level, coolant_temp, cooling_fan, tyre_pressures
    
    # Run the handler task
    handler_task = asyncio.create_task(handle_client(websocket))
    
    print("Dashboard Connected to Simulator!")
    try:
        while True:
            if is_started:
                # Interactive Physics
                if gas_pressed:
                    speed += 4.0 * 0.1 # Acceleration
                elif brake_pressed:
                    speed -= 10.0 * 0.1 # Braking
                else:
                    speed -= 1.0 * 0.1 # Friction
                
                speed = max(0, min(240, speed))
                
                # Auto Shift Logic
                if speed < 20: gear = 1
                elif speed < 50: gear = 2
                elif speed < 80: gear = 3
                elif speed < 120: gear = 4
                elif speed < 160: gear = 5
                else: gear = 6
                
                # Simple RPM calculation
                max_speed_gear = gear * 40
                min_speed_gear = max(0, (gear - 1) * 40 - 20)
                
                if speed < min_speed_gear:
                    rpm = 800 # Idle
                else:
                    ratio = (speed - min_speed_gear) / (max_speed_gear - min_speed_gear)
                    rpm = 1000 + (ratio * 7000)
                    rpm = max(800, min(9000, rpm))
                    
                # GPS movement (rough line approximation)
                lat += 0.00001 * (speed / 100.0)
                lon += 0.00001 * (speed / 100.0)
                
                # ── Fuel (config-driven rate) ─────────────────────────────
                f_rate  = float(_fuel_cfg.get("rate_per_tick", -0.1417))
                f_clamp = float(_fuel_cfg.get("clamp_min", 0.0))
                fuel_level = max(f_clamp, fuel_level + f_rate)

                # ── Tyre pressures (per-tyre config-driven rate) ──────────
                for i, tcfg in enumerate(_tyre_cfg):
                    rate  = float(tcfg.get("rate_per_tick", 0.0))
                    clamp = float(tcfg.get("clamp_min", 10.0))
                    tyre_pressures[i] = max(clamp, tyre_pressures[i] + rate)

                # ── Coolant temperature (config-driven rate + fan logic) ──
                c_rate    = float(_coolant_cfg.get("rate_per_tick", 0.05))
                c_clamp   = float(_coolant_cfg.get("clamp_max", 110.0))
                c_fan_thr = float(_coolant_cfg.get("fan_trigger_temp", 95.0))
                c_fan_cool= float(_coolant_cfg.get("fan_cooling_rate", -0.03))

                if cooling_fan:
                    # Fan active: apply cooling rate
                    coolant_temp = min(c_clamp, coolant_temp + c_rate + c_fan_cool)
                else:
                    coolant_temp = min(c_clamp, coolant_temp + c_rate)
                    if coolant_temp >= c_fan_thr:
                        cooling_fan = True
            else:
                # Reset all sensors to config start values when engine stops
                fuel_level     = float(_fuel_cfg.get("start", 100.0))
                coolant_temp   = float(_coolant_cfg.get("start", 40.0))
                cooling_fan    = False
                tyre_pressures = [float(t.get("start", 32.0)) for t in _tyre_cfg]
                
            data = {
                "started": is_started,
                "speed": round(speed, 1),
                "rpm": round(rpm),
                "gear": gear,
                "lat": lat,
                "lon": lon,
                "pedals": {"gas": gas_pressed, "brake": brake_pressed},
                "fuel_level": round(fuel_level, 2),
                "coolant_temp": round(coolant_temp, 2),
                "cooling_fan": cooling_fan,
                "tyre_pressures": [round(p, 2) for p in tyre_pressures]
            }
            
            print(f"CAN Sim Output: Speed: {data['speed']} KMPH | RPM: {data['rpm']} | Gear: {data['gear']} | Gas: {gas_pressed}")
            await websocket.send(json.dumps(data))
            
            # Publish via MQTT
            try:
                mqtt_client.publish("vehicle/simulator/telemetry", json.dumps(data))
            except Exception as e:
                print(f"MQTT Publish Error: {e}")
                
            await asyncio.sleep(0.1) # 10Hz
    except websockets.exceptions.ConnectionClosed:
        print("Client disconnected")
    finally:
        handler_task.cancel()

async def main():
    print("Connecting to MQTT Broker on localhost:1883...")
    try:
        mqtt_client.connect("127.0.0.1", 1883, 60)
        mqtt_client.loop_start()
        print("Connected to MQTT Broker!")
    except Exception as e:
        print(f"Warning: Could not connect to MQTT Broker: {e}")
        
    print("Starting CAN Simulator on ws://0.0.0.0:8083")
    async with websockets.serve(simulate, "0.0.0.0", 8083):
        await asyncio.Future()  # run forever

if __name__ == "__main__":
    asyncio.run(main())
