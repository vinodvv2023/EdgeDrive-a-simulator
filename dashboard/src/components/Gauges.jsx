import React from 'react';

export default function Gauges({ type, value, gear }) {
  const isSpeed = type === 'speed';
  
  // Speed alerts
  const isAlert = isSpeed && value >= 120;
  const isWarning = isSpeed && value >= 80 && value < 120;
  
  let valClass = "digital-value";
  if (isAlert || isWarning) valClass += " alert";
  
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%' }}>
      <h2>{isSpeed ? 'Speed' : 'Tachometer'}</h2>
      
      <div className={valClass} style={{ color: isWarning ? '#ffaa00' : (isAlert ? 'var(--alert)' : '#fff') }}>
        {Math.round(value)}
      </div>
      <div className="unit">{isSpeed ? 'KMPH' : 'RPM'}</div>
      
      {!isSpeed && (
        <div style={{ marginTop: '20px', fontSize: '2rem', fontFamily: 'Orbitron', color: 'var(--accent)' }}>
          GEAR {gear}
        </div>
      )}
    </div>
  );
}
