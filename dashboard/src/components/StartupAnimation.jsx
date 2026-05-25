import React, { useState, useEffect } from 'react';

export default function StartupAnimation({ onComplete }) {
  const [isPlaying, setIsPlaying] = useState(true);
  const [visible, setVisible] = useState(true);

  useEffect(() => {
    // Simulate a 4 second startup animation
    const timer = setTimeout(() => {
      setIsPlaying(false);
      setTimeout(() => {
        setVisible(false);
        onComplete();
      }, 1500); // 1.5s fade out
    }, 4000);
    
    return () => clearTimeout(timer);
  }, []);

  if (!visible) return null;

  return (
    <div className={`startup-overlay ${!isPlaying ? 'fade-out' : ''}`}>
      {/* 
        You can replace this placeholder with an actual HTML5 video:
        <video className="startup-video" autoPlay muted playsInline>
          <source src="/startup.mp4" type="video/mp4" />
        </video>
      */}
      <div className="placeholder-animation">
        <h1>CAN CTRL</h1>
        <p style={{ marginTop: '20px', letterSpacing: '5px', color: 'var(--text-dim)' }}>SYSTEM INITIALIZING...</p>
      </div>
    </div>
  );
}
