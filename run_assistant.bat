@echo off
echo ========================================================
echo       Starting Voice Assistant Microservices...
echo ========================================================
echo.

:: Add CUDA v13.2 bin directory to PATH so whisper.cpp can find cublas64_13.dll and other CUDA DLLs
set PATH=%PATH%;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2\bin

echo Preparing custom VSS signal definitions...
".\venv\Scripts\python.exe" "Kuksa-vss-data\inject_custom_vss.py"

echo [1/10] Starting Text-to-Speech Service (Port 8081)...
start "TTS Service (Port 8081)" cmd /k ".\build\bin\Release\tts_service.exe"

echo [2/10] Starting Speech-to-Text Service (Port 8080)...
echo       (Note: Whisper may take a few seconds to load the ggml-base.bin model)
start "STT Service (Port 8080)" cmd /k ".\build\bin\Release\stt_service.exe"

echo [3/10] Starting Ollama API Backend...
start "Ollama API (Port 11434)" cmd /c "ollama serve"

echo [4/10] Starting Mosquitto MQTT Broker (Port 1883)...
docker rm -f eclipse-mosquitto >nul 2>&1
start "MQTT Broker" cmd /k "docker run -it --rm --name eclipse-mosquitto -p 1883:1883 eclipse-mosquitto"

echo [5/10] Starting Kuksa Databroker (Port 55555)...
docker rm -f kuksa-databroker >nul 2>&1
start "Kuksa Databroker" cmd /k "docker run -it --rm --name kuksa-databroker -p 55555:55555 -v "c:\Users\xtrem\Downloads\CPlusPlus\CAN CTRL\Kuksa-vss-data\custom_vss.json:/data/vss.json" ghcr.io/eclipse-kuksa/kuksa-databroker:latest --insecure --vss /data/vss.json"

echo.
echo Waiting 5 seconds for all backend services to initialize...
timeout /t 5 /nobreak >nul

echo [6/10] Starting Assistant GUI Orchestrator...
start "Voice Assistant Orchestrator" cmd /c ".\build\bin\Release\assistant_gui.exe"

echo [7/10] Starting CAN Simulator...
start "CAN Simulator" cmd /k "cd simulator && ..\venv\Scripts\python main.py"

echo [8/10] Starting CAN Orchestrator...
start "CAN Orchestrator" cmd /k "cd orchestrator && ..\venv\Scripts\python server.py"

echo [9/10] Starting Kuksa Feeder Bridge...
start "Kuksa Feeder" cmd /k "cd simulator && ..\venv\Scripts\python kuksa_feeder.py"

echo [10/10] Starting Dashboard UI...
start "Dashboard UI" cmd /k "cd dashboard && npm run dev"


echo.
echo All services launched!
echo The Assistant GUI and Dashboard should now be open.
