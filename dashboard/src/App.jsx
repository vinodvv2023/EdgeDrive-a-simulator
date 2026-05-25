import { useState, useEffect, useRef } from 'react';
import './index.css';
import AnalogGauge from './components/AnalogGauge';
import CameraView from './components/CameraView';
import MapView from './components/MapView';
import StartupAnimation from './components/StartupAnimation';
import Pedals from './components/Pedals';

function App() {
  const [telemetry, setTelemetry] = useState({
    speed: 0,
    rpm: 0,
    gear: 1,
    lat: 40.7128,
    lon: -74.0060,
    pedals: { gas: false, brake: false }
  });
  
  const [hasTriggeredStart, setHasTriggeredStart] = useState(false);
  const [isStarted, setIsStarted] = useState(false);
  const wsRef = useRef(null);

  useEffect(() => {
    const ws = new WebSocket('ws://127.0.0.1:8082/ws/dashboard');
    wsRef.current = ws;
    
    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        if (data.started && !hasTriggeredStart) {
            setHasTriggeredStart(true);
        }
        setTelemetry(data);
      } catch(e) {}
    };
    
    return () => ws.close();
  }, [hasTriggeredStart]);

  // Keyboard Listeners
  useEffect(() => {
    const handleKeyDown = (e) => {
      const ws = wsRef.current;
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      
      if ((e.key === 's' || e.key === 'S') && !hasTriggeredStart) {
        setHasTriggeredStart(true);
        ws.send(JSON.stringify({action: 'start'}));
      }
      if (e.key === 'ArrowUp') ws.send(JSON.stringify({action: 'gas', value: true}));
      if (e.key === 'ArrowDown') ws.send(JSON.stringify({action: 'brake', value: true}));
    };
    
    const handleKeyUp = (e) => {
      const ws = wsRef.current;
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      
      if (e.key === 'ArrowUp') ws.send(JSON.stringify({action: 'gas', value: false}));
      if (e.key === 'ArrowDown') ws.send(JSON.stringify({action: 'brake', value: false}));
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
    };
  }, [hasTriggeredStart]);

  return (
    <>
      {hasTriggeredStart && !isStarted && <StartupAnimation onComplete={() => setIsStarted(true)} />}
      
      {!hasTriggeredStart && (
        <div style={{ position: 'fixed', top: '50%', left: '0', width: '100vw', textAlign: 'center', color: '#fff', zIndex: 10000, fontFamily: 'Orbitron', fontSize: '3rem', animation: 'pulse-alert 2s infinite' }}>
          PRESS 'S' TO START SYSTEM
        </div>
      )}

      <div className="dashboard-container" style={{ opacity: isStarted ? 1 : 0, transition: 'opacity 1s ease' }}>
        
        {/* Top Left: Dummy Rear Cam */}
        <div className="glass-panel" style={{ gridColumn: 1, gridRow: 1 }}>
          <h2>Rear Cam</h2>
          <div style={{ flex: 1, backgroundColor: '#111', borderRadius: '8px', display: 'flex', alignItems: 'center', justifyContent: 'center', color: 'var(--text-dim)' }}>
            NO SIGNAL
          </div>
        </div>
        
        {/* Top Center: Pedals */}
        <div className="glass-panel" style={{ gridColumn: 2, gridRow: 1 }}>
          <Pedals pedals={telemetry.pedals} gear={telemetry.gear} />
        </div>

        {/* Top Right: Live Web Cam */}
        <div className="glass-panel" style={{ gridColumn: 3, gridRow: 1 }}>
          <h2>Cabin Cam</h2>
          <CameraView />
        </div>

        {/* Bottom Left: Tachometer */}
        <div className="glass-panel" style={{ gridColumn: 1, gridRow: 2, display: 'flex', justifyContent: 'center', alignItems: 'center' }}>
          <AnalogGauge 
            type="Tachometer" 
            value={telemetry.rpm} 
            max={9000} 
            unit="RPM" 
            legendUnit="x1000 RPM" 
            tickCount={19} 
            majorTickInterval={2} 
            renderTick={(v) => Math.round(v / 1000)} 
            size={220} 
            accentColor="#ff2a2a" 
          />
        </div>

        {/* Bottom Center: Speedometer */}
        <div className="glass-panel" style={{ gridColumn: 2, gridRow: 2, display: 'flex', justifyContent: 'center', alignItems: 'center' }}>
          <AnalogGauge 
            type="Speedometer" 
            value={telemetry.speed} 
            max={240} 
            unit="KM/H" 
            tickCount={25} 
            majorTickInterval={2} 
            renderTick={(v) => Math.round(v)} 
            size={360} 
            accentColor="#00d2ff" 
          />
        </div>

        {/* Bottom Right: Map */}
        <div className="glass-panel" style={{ gridColumn: 3, gridRow: 2 }}>
          <h2>Navigation</h2>
          <MapView lat={telemetry.lat} lon={telemetry.lon} />
        </div>
        
      </div>
    </>
  );
}

export default App;
