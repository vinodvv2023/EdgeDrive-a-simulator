import React from 'react';

export default function CameraView() {
  return (
    <div style={{ width: '100%', height: '100%', borderRadius: '12px', overflow: 'hidden', backgroundColor: '#000', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
      <img 
        src="http://127.0.0.1:8082/video_feed" 
        alt="Live Camera Feed"
        style={{ width: '100%', height: '100%', objectFit: 'cover' }}
        onError={(e) => { e.target.style.display = 'none'; }}
      />
    </div>
  );
}
