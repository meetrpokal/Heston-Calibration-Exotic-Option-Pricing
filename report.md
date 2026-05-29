# Heston Calibration and Exotic Option Pricing Report

## Objective

The objective of this project was to calibrate the Heston stochastic volatility model using market implied volatility data and price an exotic barrier option under the calibrated model.

## Calibration

The Heston model parameters were calibrated using the Levenberg-Marquardt optimization algorithm. Calibration quality was measured using RMSE between market and model implied volatilities.

## Results

- Calibration RMSE achieved below 1%
- Monte Carlo simulation used 100,000 paths
- Barrier option priced under both Heston and Black-Scholes models
- Heston price differed from Black-Scholes due to stochastic volatility effects

## Greeks

Delta and Vega were computed using finite difference approximation methods.

## Conclusion

The project successfully demonstrated calibration, pricing, and risk analysis for equity derivatives using QuantLib and C++.