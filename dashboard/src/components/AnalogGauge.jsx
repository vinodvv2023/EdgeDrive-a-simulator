import React from 'react';

export default function AnalogGauge({ 
  type, value, max, unit, legendUnit, 
  tickCount = 11, majorTickInterval = 1, renderTick, 
  size = 200, accentColor = "var(--accent)" 
}) {
  const radius = 80;
  const circumference = 2 * Math.PI * radius; // 502.65
  const arcLength = circumference * (270 / 360); // 376.99
  
  // Angle for needle
  const clampedValue = Math.max(0, Math.min(value, max));
  const angle = -135 + (clampedValue / max) * 270;
  
  // Dash offset for the filled progress bar
  const progressOffset = circumference - (arcLength * (clampedValue / max));

  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', width: '100%' }}>
      <h2 style={{ marginBottom: '10px', fontSize: size > 250 ? '1.5rem' : '1rem', color: accentColor, textShadow: `0 0 10px ${accentColor}80` }}>{type.toUpperCase()}</h2>
      
      <div style={{ position: 'relative', width: `${size}px`, height: `${size}px`, display: 'flex', justifyContent: 'center', alignItems: 'center' }}>
        
        {/* SVG Background and Progress Arc */}
        <svg width="100%" height="100%" viewBox="0 0 200 200" style={{ position: 'absolute', overflow: 'visible' }}>
          
          <defs>
            <linearGradient id={`grad-${type}`} x1="0%" y1="100%" x2="100%" y2="0%">
              <stop offset="0%" stopColor={accentColor} stopOpacity="0.1" />
              <stop offset="50%" stopColor={accentColor} stopOpacity="0.5" />
              <stop offset="100%" stopColor={accentColor} />
            </linearGradient>
            <filter id={`glow-${type}`}>
              <feGaussianBlur stdDeviation="4" result="coloredBlur"/>
              <feMerge>
                <feMergeNode in="coloredBlur"/>
                <feMergeNode in="SourceGraphic"/>
              </feMerge>
            </filter>
          </defs>

          {/* Background Arc */}
          <path d="M 43.43 156.57 A 80 80 0 1 1 156.57 156.57" fill="none" stroke="rgba(255,255,255,0.05)" strokeWidth="14" strokeLinecap="round" />
          
          {/* Filled Progress Arc */}
          <circle 
            cx="100" cy="100" r="80" 
            fill="none" 
            stroke={`url(#grad-${type})`} 
            strokeWidth="14" 
            strokeLinecap="round" 
            strokeDasharray={circumference} 
            strokeDashoffset={progressOffset} 
            filter={`url(#glow-${type})`}
            style={{ 
              transformOrigin: 'center', 
              transform: 'rotate(135deg)', 
              transition: 'stroke-dashoffset 0.2s ease-out' 
            }} 
          />
          
          {/* Tick marks & Labels */}
          {[...Array(tickCount)].map((_, i) => {
            const tickAngle = -135 + (i * (270 / (tickCount - 1)));
            const isMajor = i % majorTickInterval === 0;
            const tickValue = (max / (tickCount - 1)) * i;
            
            // Calculate label position
            const angleRad = (tickAngle - 90) * (Math.PI / 180);
            const labelRadius = 56;
            const lx = 100 + labelRadius * Math.cos(angleRad);
            const ly = 100 + labelRadius * Math.sin(angleRad);
            
            return (
              <g key={i}>
                <line 
                  x1="100" y1={isMajor ? "20" : "24"} 
                  x2="100" y2="30" 
                  stroke={isMajor ? "rgba(255,255,255,0.9)" : "rgba(255,255,255,0.4)"} 
                  strokeWidth={isMajor ? "3" : "1.5"} 
                  style={{ transformOrigin: '100px 100px', transform: `rotate(${tickAngle}deg)` }} 
                />
                {isMajor && renderTick && (
                  <text x={lx} y={ly} fill="var(--text-main)" fontSize="11" fontWeight="600" textAnchor="middle" alignmentBaseline="middle" style={{ fontFamily: 'Orbitron', filter: 'drop-shadow(0px 0px 2px rgba(0,0,0,0.8))' }}>
                    {renderTick(tickValue, i)}
                  </text>
                )}
              </g>
            );
          })}

          {legendUnit && (
            <text x="100" y="145" fill="var(--text-dim)" fontSize="10" fontWeight="bold" textAnchor="middle" style={{ fontFamily: 'Orbitron', opacity: 0.8 }}>
              {legendUnit}
            </text>
          )}
        </svg>

        {/* Needle */}
        <div style={{
          position: 'absolute',
          width: '4px',
          height: '46%',
          backgroundColor: '#fff',
          bottom: '50%',
          transformOrigin: 'bottom center',
          transform: `rotate(${angle}deg)`,
          transition: 'transform 0.2s cubic-bezier(0.4, 0, 0.2, 1)',
          borderRadius: '2px',
          boxShadow: `0 0 12px ${accentColor}, 0 0 4px #fff`,
          zIndex: 5
        }} />

        {/* Center Hub */}
        <div style={{
          position: 'absolute',
          width: '14%',
          height: '14%',
          backgroundColor: '#0a0a0a',
          border: `3px solid ${accentColor}`,
          boxShadow: `0 0 10px ${accentColor} inset, 0 0 10px rgba(0,0,0,0.5)`,
          borderRadius: '50%',
          zIndex: 10
        }} />
        
        {/* Digital Value Display in the center/bottom */}
        <div style={{ position: 'absolute', bottom: '2%', display: 'flex', flexDirection: 'column', alignItems: 'center', zIndex: 11 }}>
          <div className="digital-value" style={{ 
            fontSize: size > 250 ? '3.5rem' : '2.2rem', 
            textShadow: `0 0 15px ${accentColor}`, 
            color: '#fff', 
            lineHeight: 1 
          }}>
            {Math.round(value)}
          </div>
          <div className="unit" style={{ 
            fontSize: size > 250 ? '1.2rem' : '0.9rem', 
            color: accentColor, 
            fontWeight: 'bold', 
            marginTop: '2px',
            textShadow: `0 0 5px ${accentColor}80`
          }}>
            {unit}
          </div>
        </div>
      </div>
    </div>
  );
}
