-- RiskDB Schema Initialization for PostgreSQL
CREATE TABLE IF NOT EXISTS contracts (
    contract_id BIGINT PRIMARY KEY,
    underlying_id INT NOT NULL,
    portfolio_id INT NOT NULL,
    option_type SMALLINT NOT NULL, -- 0 for CALL, 1 for PUT
    spot_price REAL NOT NULL,
    strike_price REAL NOT NULL,
    time_to_expiry REAL NOT NULL,  -- in years (tau > 0)
    volatility REAL NOT NULL,     -- sigma > 0
    risk_free_rate REAL NOT NULL, -- r >= 0
    quantity REAL NOT NULL        -- long > 0, short < 0
);

CREATE INDEX IF NOT EXISTS idx_contracts_portfolio ON contracts(portfolio_id);
CREATE INDEX IF NOT EXISTS idx_contracts_underlying ON contracts(underlying_id);
