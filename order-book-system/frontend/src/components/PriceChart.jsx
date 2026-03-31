import React, { useRef, useEffect, useMemo } from 'react';

export default function PriceChart({ priceHistory, ticker = 'AAPL' }) {
  const canvasRef = useRef(null);

  const data = useMemo(() => {
    if (!priceHistory.length) return null;
    const closes = priceHistory.map((b) => b.close);
    const highs = priceHistory.map((b) => b.high);
    const lows = priceHistory.map((b) => b.low);
    const opens = priceHistory.map((b) => b.open);
    const volumes = priceHistory.map((b) => b.volume);
    const min = Math.min(...lows);
    const max = Math.max(...highs);
    const maxVol = Math.max(...volumes, 1);
    return { closes, highs, lows, opens, volumes, min, max, maxVol };
  }, [priceHistory]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !data) return;
    const ctx = canvas.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);
    const W = rect.width;
    const H = rect.height;

    ctx.clearRect(0, 0, W, H);

    const n = data.closes.length;
    if (n === 0) return;

    const padT = 20;
    const padB = 40; // room for volume bars
    const padL = 2;
    const padR = 2;
    const priceH = H - padT - padB;
    const priceRange = data.max - data.min || 1;

    function xPos(i) {
      return padL + (i / Math.max(n - 1, 1)) * (W - padL - padR);
    }
    function yPrice(p) {
      return padT + (1 - (p - data.min) / priceRange) * priceH;
    }

    // Grid lines
    ctx.strokeStyle = 'rgba(255,255,255,0.04)';
    ctx.lineWidth = 1;
    const gridLines = 5;
    for (let i = 0; i <= gridLines; i++) {
      const y = padT + (i / gridLines) * priceH;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(W, y);
      ctx.stroke();

      // Price labels
      const p = data.max - (i / gridLines) * priceRange;
      ctx.fillStyle = '#5c6a7e';
      ctx.font = '10px JetBrains Mono, monospace';
      ctx.textAlign = 'right';
      ctx.fillText(`$${p.toFixed(2)}`, W - 4, y - 3);
    }

    // Volume bars (bottom area)
    const volH = padB - 8;
    const barW = Math.max((W - padL - padR) / n - 1, 1);
    for (let i = 0; i < n; i++) {
      const x = xPos(i) - barW / 2;
      const h = (data.volumes[i] / data.maxVol) * volH;
      const bullish = data.closes[i] >= data.opens[i];
      ctx.fillStyle = bullish ? 'rgba(0,200,120,0.2)' : 'rgba(240,60,60,0.2)';
      ctx.fillRect(x, H - h, barW, h);
    }

    // Candlesticks
    for (let i = 0; i < n; i++) {
      const x = xPos(i);
      const bullish = data.closes[i] >= data.opens[i];
      const color = bullish ? '#00c878' : '#f03c3c';

      // Wick
      ctx.strokeStyle = color;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(x, yPrice(data.highs[i]));
      ctx.lineTo(x, yPrice(data.lows[i]));
      ctx.stroke();

      // Body
      const bodyTop = yPrice(Math.max(data.opens[i], data.closes[i]));
      const bodyBot = yPrice(Math.min(data.opens[i], data.closes[i]));
      const bodyH = Math.max(bodyBot - bodyTop, 1);
      ctx.fillStyle = color;
      ctx.fillRect(x - barW / 2, bodyTop, barW, bodyH);
    }

    // Current price label
    const lastClose = data.closes[n - 1];
    const lastY = yPrice(lastClose);
    ctx.fillStyle = data.closes[n - 1] >= data.opens[n - 1] ? '#00c878' : '#f03c3c';
    ctx.font = 'bold 12px JetBrains Mono, monospace';
    ctx.textAlign = 'left';
    ctx.fillText(`$${lastClose.toFixed(2)}`, 8, lastY - 6);

    // Dashed line at current price
    ctx.setLineDash([4, 4]);
    ctx.strokeStyle = ctx.fillStyle;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, lastY);
    ctx.lineTo(W, lastY);
    ctx.stroke();
    ctx.setLineDash([]);
  }, [data]);

  return (
    <div className="price-chart">
      <div className="panel-header">
        <span className="panel-title">{ticker} Price</span>
        {data && (
          <span className="chart-last-price">
            ${data.closes[data.closes.length - 1]?.toFixed(2)}
          </span>
        )}
      </div>
      <canvas ref={canvasRef} className="price-canvas" />
      {!data && (
        <div className="chart-empty">Start replay to see price data</div>
      )}
    </div>
  );
}
