import React, { useMemo } from 'react';

export default function OrderBook({ book, nodeId }) {
  const maxQty = useMemo(() => {
    const all = [...book.bids, ...book.asks].map((l) => l.quantity);
    return Math.max(...all, 1);
  }, [book]);

  const spread = useMemo(() => {
    if (book.asks.length && book.bids.length) {
      return (book.asks[0].price - book.bids[0].price).toFixed(2);
    }
    return '--';
  }, [book]);

  const midPrice = useMemo(() => {
    if (book.asks.length && book.bids.length) {
      return ((book.asks[0].price + book.bids[0].price) / 2).toFixed(2);
    }
    return '--';
  }, [book]);

  return (
    <div className="orderbook">
      <div className="orderbook-header">
        <span className="orderbook-title">{nodeId}</span>
        <span className="orderbook-mid">Mid {midPrice}</span>
        <span className="orderbook-spread">Spread {spread}</span>
      </div>

      <div className="orderbook-columns">
        <span>Price</span>
        <span>Quantity</span>
      </div>

      {/* Asks — reversed so lowest ask is at bottom */}
      <div className="orderbook-asks">
        {[...book.asks].reverse().map((level, i) => (
          <div className="orderbook-row ask-row" key={`a-${i}`}>
            <div
              className="depth-bar ask-bar"
              style={{ width: `${(level.quantity / maxQty) * 100}%` }}
            />
            <span className="price ask-price">{level.price.toFixed(2)}</span>
            <span className="qty">{level.quantity.toLocaleString()}</span>
          </div>
        ))}
      </div>

      {/* Spread divider */}
      <div className="orderbook-spread-row">
        <span>{spread !== '--' ? `$${spread} spread` : 'No spread'}</span>
      </div>

      {/* Bids */}
      <div className="orderbook-bids">
        {book.bids.map((level, i) => (
          <div className="orderbook-row bid-row" key={`b-${i}`}>
            <div
              className="depth-bar bid-bar"
              style={{ width: `${(level.quantity / maxQty) * 100}%` }}
            />
            <span className="price bid-price">{level.price.toFixed(2)}</span>
            <span className="qty">{level.quantity.toLocaleString()}</span>
          </div>
        ))}
      </div>

      {book.bids.length === 0 && book.asks.length === 0 && (
        <div className="orderbook-empty">No orders</div>
      )}
    </div>
  );
}
