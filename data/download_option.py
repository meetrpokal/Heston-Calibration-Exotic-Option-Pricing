import math
from datetime import date, datetime
from typing import Optional

import pandas as pd
import yfinance as yf


def norm_cdf(x: float) -> float:
    return 0.5 * (1.0 + math.erf(x / math.sqrt(2.0)))


def black_scholes_call(spot: float, strike: float, time_to_expiry: float, rate: float, dividend: float, volatility: float) -> float:
    if time_to_expiry <= 0.0:
        return max(spot - strike, 0.0)
    if volatility <= 0.0:
        forward = spot * math.exp((rate - dividend) * time_to_expiry)
        return math.exp(-rate * time_to_expiry) * max(forward - strike, 0.0)

    sqrt_t = math.sqrt(time_to_expiry)
    variance = volatility * sqrt_t
    d1 = (math.log(spot / strike) + (rate - dividend + 0.5 * volatility * volatility) * time_to_expiry) / variance
    d2 = d1 - variance
    return spot * math.exp(-dividend * time_to_expiry) * norm_cdf(d1) - strike * math.exp(-rate * time_to_expiry) * norm_cdf(d2)


def implied_volatility_from_price(
    price: float,
    spot: float,
    strike: float,
    time_to_expiry: float,
    rate: float,
    dividend: float,
    lower: float = 1e-4,
    upper: float = 5.0,
    max_iter: int = 100,
) -> Optional[float]:
    intrinsic = max(spot * math.exp(-dividend * time_to_expiry) - strike * math.exp(-rate * time_to_expiry), 0.0)
    if price <= intrinsic + 1e-10:
        return None

    low = lower
    high = upper
    low_price = black_scholes_call(spot, strike, time_to_expiry, rate, dividend, low)
    high_price = black_scholes_call(spot, strike, time_to_expiry, rate, dividend, high)

    if price < low_price or price > high_price:
        return None

    for _ in range(max_iter):
        mid = 0.5 * (low + high)
        mid_price = black_scholes_call(spot, strike, time_to_expiry, rate, dividend, mid)
        if abs(mid_price - price) < 1e-8:
            return mid
        if mid_price > price:
            high = mid
        else:
            low = mid

    return 0.5 * (low + high)


def best_expiries(all_expiries: list[str], targets: list[int]) -> list[str]:
    expiry_dates = [datetime.strptime(value, "%Y-%m-%d").date() for value in all_expiries]
    today = date.today()
    picked: list[str] = []

    for target_days in targets:
        target_date = date.fromordinal(today.toordinal() + target_days)
        nearest = min(expiry_dates, key=lambda candidate: abs((candidate - target_date).days))
        nearest_text = nearest.isoformat()
        if nearest_text not in picked:
            picked.append(nearest_text)

    return picked


ticker = yf.Ticker("SPY")

try:
    spot_price = float(ticker.fast_info.get("lastPrice") or ticker.history(period="5d")["Close"].iloc[-1])
except Exception:
    spot_price = float(ticker.history(period="1d")["Close"].iloc[-1])

risk_free_rate = 0.05
dividend_yield = 0.01
target_maturities = [30, 90, 180, 365, 730]
expiries = best_expiries(list(ticker.options), target_maturities)

all_data = []

for expiry in expiries:
    option_chain = ticker.option_chain(expiry)
    calls = option_chain.calls

    for _, row in calls.iterrows():
        bid = float(row["bid"] or 0.0)
        ask = float(row["ask"] or 0.0)
        last_price = float(row["lastPrice"] or 0.0)
        market_price = 0.5 * (bid + ask) if bid > 0.0 and ask > 0.0 else last_price
        if market_price <= 0.0:
            continue

        maturity_days = (datetime.strptime(expiry, "%Y-%m-%d").date() - date.today()).days
        maturity_years = maturity_days / 365.0
        if maturity_years <= 0.0:
            continue

        strike = float(row["strike"])
        implied_vol = implied_volatility_from_price(
            market_price,
            spot_price,
            strike,
            maturity_years,
            risk_free_rate,
            dividend_yield,
        )
        if implied_vol is None:
            continue

        all_data.append({
            "expiry": expiry,
            "maturity_days": maturity_days,
            "strike": strike,
            "market_price": market_price,
            "bid": bid,
            "ask": ask,
            "implied_vol": implied_vol,
            "volume": float(row["volume"] or 0.0),
            "open_interest": float(row["openInterest"] or 0.0),
            "underlying_price": spot_price,
        })

df = pd.DataFrame(all_data)
df = df.sort_values(["expiry", "strike"]).reset_index(drop=True)

with open("spy_options.csv", "w", encoding="utf-8", newline="") as output_file:
    df.to_csv(output_file, index=False)

print(df.head())
print("\nSaved to spy_options.csv with computed implied vols")