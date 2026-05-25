import React from 'react';

export default function Pedals({ pedals, gear }) {
  const getStyle = (isPressed) => ({
    padding: '15px 30px',
    borderRadius: '8px',
    border: `2px solid ${isPressed ? 'var(--accent)' : 'rgba(255,255,255,0.1)'}`,
    backgroundColor: isPressed ? 'rgba(0, 210, 255, 0.2)' : 'transparent',
    color: isPressed ? '#fff' : 'var(--text-dim)',
    textShadow: isPressed ? '0 0 10px var(--accent)' : 'none',
    boxShadow: isPressed ? '0 0 15px var(--accent-glow)' : 'none',
    transition: 'all 0.1s',
    fontWeight: 'bold',
    fontFamily: 'Orbitron',
    fontSize: '1.2rem',
    textAlign: 'center',
    width: '150px'
  });

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', justifyContent: 'space-between' }}>
      <h2 style={{ textAlign: 'center' }}>CONTROLS</h2>
      
      <div style={{ display: 'flex', justifyContent: 'center', gap: '30px', flex: 1, alignItems: 'center' }}>
        <div style={getStyle(pedals?.brake)}>BRAKE<br/><span style={{fontSize:'0.8rem', color:'var(--text-dim)'}}>(Down Arrow)</span></div>
        <div style={getStyle(pedals?.gas)}>GAS<br/><span style={{fontSize:'0.8rem', color:'var(--text-dim)'}}>(Up Arrow)</span></div>
      </div>
      
      <div style={{ textAlign: 'center', fontSize: '2rem', fontFamily: 'Orbitron', color: '#fff', textShadow: '0 0 10px var(--accent)' }}>
        GEAR {gear} <span style={{fontSize: '1rem', color: 'var(--text-dim)'}}>(AUTO)</span>
      </div>
    </div>
  );
}
