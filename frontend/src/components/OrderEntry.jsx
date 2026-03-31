import React, { useState, useCallback } from 'react';

export default function OrderEntry({ sendCommand, nodes, tickerMap = {} }) {
  const [node, setNode] = useState('node-a');
  const [side, setSide] = useState('BUY');
  const [orderType, setOrderType] = useState('LIMIT');
  const [price, setPrice] = useState('');
  const [quantity, setQuantity] = useState('');
  const [lastResult, setLastResult] = useState(null);

  const submit = useCallback(
    (e) => {
      e.preventDefault();
      const qty = parseInt(quantity, 10);
      if (!qty || qty <= 0) return;

      let cmd;
      if (orderType === 'LIMIT') {
        const px = parseFloat(price);
        if (!px || px <= 0) return;
        cmd = `ADD ${side} ${px.toFixed(2)} ${qty}`;
      } else {
        cmd = `MARKET ${side} ${qty}`;
      }

      sendCommand(node, cmd);
      setLastResult({ cmd, node, ts: Date.now() });
      setPrice('');
      setQuantity('');
    },
    [node, side, orderType, price, quantity, sendCommand],
  );

  return (
    <form className="order-entry" onSubmit={submit}>
      <div className="order-entry-title">New Order</div>

      <div className="order-entry-row">
        <label>Node</label>
        <select value={node} onChange={(e) => setNode(e.target.value)}>
          {nodes.map((n) => (
            <option key={n} value={n}>{tickerMap[n] || n} ({n})</option>
          ))}
        </select>
      </div>

      <div className="order-entry-sides">
        <button
          type="button"
          className={`side-btn buy ${side === 'BUY' ? 'active' : ''}`}
          onClick={() => setSide('BUY')}
        >
          Buy
        </button>
        <button
          type="button"
          className={`side-btn sell ${side === 'SELL' ? 'active' : ''}`}
          onClick={() => setSide('SELL')}
        >
          Sell
        </button>
      </div>

      <div className="order-entry-types">
        <button
          type="button"
          className={`type-btn ${orderType === 'LIMIT' ? 'active' : ''}`}
          onClick={() => setOrderType('LIMIT')}
        >
          Limit
        </button>
        <button
          type="button"
          className={`type-btn ${orderType === 'MARKET' ? 'active' : ''}`}
          onClick={() => setOrderType('MARKET')}
        >
          Market
        </button>
      </div>

      {orderType === 'LIMIT' && (
        <div className="order-entry-row">
          <label>Price</label>
          <input
            type="number"
            step="0.01"
            min="0.01"
            placeholder="0.00"
            value={price}
            onChange={(e) => setPrice(e.target.value)}
          />
        </div>
      )}

      <div className="order-entry-row">
        <label>Quantity</label>
        <input
          type="number"
          step="1"
          min="1"
          placeholder="0"
          value={quantity}
          onChange={(e) => setQuantity(e.target.value)}
        />
      </div>

      <button
        type="submit"
        className={`submit-btn ${side === 'BUY' ? 'buy' : 'sell'}`}
      >
        {orderType === 'LIMIT' ? 'Place' : 'Execute'} {side === 'BUY' ? 'Buy' : 'Sell'}
      </button>

      {lastResult && (
        <div className="order-result">
          <span className="mono">{lastResult.cmd}</span>
          <span className="dim"> to {lastResult.node}</span>
        </div>
      )}
    </form>
  );
}
