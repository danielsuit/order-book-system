import React, { useMemo, useRef, useEffect } from 'react';

export default function DepthChart({ book }) {
  const canvasRef = useRef(null);

  const cumulative = useMemo(() => {
    const bidsCum = [];
    let sum = 0;
    for (const level of book.bids) {
      sum += level.quantity;
      bidsCum.push({ price: level.price, cum: sum });
    }

    const asksCum = [];
    sum = 0;
    for (const level of book.asks) {
      sum += level.quantity;
      asksCum.push({ price: level.price, cum: sum });
    }

    return { bids: bidsCum, asks: asksCum };
  }, [book]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);
    const W = rect.width;
    const H = rect.height;

    ctx.clearRect(0, 0, W, H);

    const { bids, asks } = cumulative;
    if (!bids.length && !asks.length) {
      ctx.fillStyle = '#555';
      ctx.font = '13px Inter, sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('No depth data', W / 2, H / 2);
      return;
    }

    const allPrices = [...bids, ...asks].map((p) => p.price);
    const allCum = [...bids, ...asks].map((p) => p.cum);
    const minP = Math.min(...allPrices);
    const maxP = Math.max(...allPrices);
    const maxC = Math.max(...allCum);
    const priceRange = maxP - minP || 1;
    const pad = 4;

    function x(price) {
      return pad + ((price - minP) / priceRange) * (W - pad * 2);
    }
    function y(cum) {
      return H - pad - (cum / maxC) * (H - pad * 2);
    }

    // Bids area (green)
    if (bids.length) {
      ctx.beginPath();
      ctx.moveTo(x(bids[0].price), y(0));
      for (const pt of bids) {
        ctx.lineTo(x(pt.price), y(pt.cum));
      }
      ctx.lineTo(x(bids[bids.length - 1].price), y(0));
      ctx.closePath();
      ctx.fillStyle = 'rgba(0, 200, 120, 0.15)';
      ctx.fill();
      ctx.strokeStyle = '#00c878';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(x(bids[0].price), y(bids[0].cum));
      for (const pt of bids) ctx.lineTo(x(pt.price), y(pt.cum));
      ctx.stroke();
    }

    // Asks area (red)
    if (asks.length) {
      ctx.beginPath();
      ctx.moveTo(x(asks[0].price), y(0));
      for (const pt of asks) {
        ctx.lineTo(x(pt.price), y(pt.cum));
      }
      ctx.lineTo(x(asks[asks.length - 1].price), y(0));
      ctx.closePath();
      ctx.fillStyle = 'rgba(240, 60, 60, 0.15)';
      ctx.fill();
      ctx.strokeStyle = '#f03c3c';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(x(asks[0].price), y(asks[0].cum));
      for (const pt of asks) ctx.lineTo(x(pt.price), y(pt.cum));
      ctx.stroke();
    }
  }, [cumulative]);

  return (
    <div className="depth-chart">
      <div className="panel-header">
        <span className="panel-title">Depth Chart</span>
      </div>
      <canvas ref={canvasRef} className="depth-canvas" />
    </div>
  );
}
