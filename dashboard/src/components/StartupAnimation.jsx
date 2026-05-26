import React, { useState, useEffect, useRef } from 'react';

export default function StartupAnimation({ onComplete }) {
  const [isPlaying, setIsPlaying] = useState(true);
  const [visible, setVisible] = useState(true);
  const videoRef = useRef(null);

  const handleVideoEnd = () => {
    setIsPlaying(false);
    setTimeout(() => {
      setVisible(false);
      onComplete();
    }, 1500); // 1.5s fade out
  };

  useEffect(() => {
    // Try to auto-play
    if (videoRef.current) {
      videoRef.current.play().catch(() => {
        // Auto-play was prevented (e.g. browser policy), fallback to timer
        console.log("Autoplay blocked, falling back to timer.");
      });
    }

    // Safety fallback timer of 12 seconds in case video doesn't trigger onEnded or load
    const timer = setTimeout(() => {
      handleVideoEnd();
    }, 12000);
    
    return () => clearTimeout(timer);
  }, []);

  if (!visible) return null;

  return (
    <div className={`startup-overlay ${!isPlaying ? 'fade-out' : ''}`}>
      <video 
        ref={videoRef}
        className="startup-video" 
        autoPlay 
        muted 
        playsInline
        onEnded={handleVideoEnd}
      >
        <source src="/covesa.mp4" type="video/mp4" />
        Your browser does not support the video tag.
      </video>
    </div>
  );
}
