import yfinance as yf
import pandas as pd

ticker = yf.Ticker("SPY")

all_data = []

# select first 3 expiries
expiries = ticker.options[:3]

for expiry in expiries:
    opt = ticker.option_chain(expiry)

    calls = opt.calls

    for _, row in calls.iterrows():
        all_data.append({
            "expiry": expiry,
            "strike": row["strike"],
            "market_price": row["lastPrice"],
            "bid": row["bid"],
            "ask": row["ask"],
            "implied_vol": row["impliedVolatility"],
            "volume": row["volume"],
            "open_interest": row["openInterest"]
        })

df = pd.DataFrame(all_data)

# keep only useful rows
df = df[
    (df["implied_vol"] > 0) &
    (df["volume"] > 10)
]

df.to_csv("spy_options.csv", index=False)

print(df.head())
print("\nSaved to spy_options.csv")