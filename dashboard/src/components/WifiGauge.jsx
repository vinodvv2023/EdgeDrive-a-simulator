import React from 'react';

export default function WifiGauge({ type, value, max, unit }) {
  const numBars = 6;
  const fillLevel = Math.max(0, Math.min(numBars, Math.ceil((value / max) * numBars)));
  
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%' }}>
      <h2 style={{ marginBottom: '30px' }}>{type.toUpperCase()}</h2>
      
      <div style={{ position: 'relative', width: '200px', height: '100px', display: 'flex', justifyContent: 'center', alignItems: 'flex-end', overflow: 'hidden' }}>
        {[...Array(numBars)].map((_, i) => {
          const size = 30 + (i * 30);
          const isActive = (i < fillLevel);
          const color = isActive ? 'var(--accent)' : 'rgba(255,255,255,0.1)';
          
          return (
            <div key={i} style={{
              position: 'absolute',
              bottom: 0,
              width: `${size}px`,
              height: `${size}px`,
              borderRadius: '50%',
              border: `8px solid transparent`,
              borderTopColor: color,
              boxShadow: `0 -5px 15px ${isActive ? 'var(--accent-glow)' : 'transparent'}`,
              transition: 'all 0.2s ease'
            }} />
          );
        })}
        {/* Center dot */}
        <div style={{
          position: 'absolute',
          bottom: '0px',
          width: '14px',
          height: '14px',
          borderRadius: '50%',
          backgroundColor: fillLevel > 0 ? 'var(--accent)' : 'rgba(255,255,255,0.1)'
        }} />
      </div>
      
      <div style={{ marginTop: '20px', display: 'flex', alignItems: 'baseline' }}>
        <div className="digital-value" style={{ fontSize: '2.5rem' }}>{Math.round(value)}</div>
        <div className="unit">{unit}</div>
      </div>
    </div>
  );
}
