# Quantitative Modeling of Equity Derivatives

## Features

- Heston Model Calibration
- Barrier Option Pricing
- Monte Carlo Simulation
- Black-Scholes Comparison
- Greeks Calculation
- RMSE Analysis

## Build

```bash
mkdir build
cd build

cmake -G "MinGW Makefiles" ..
mingw32-make
```

## Run

```bash
./main.exe
```

Sample output includes calibrated Heston parameters, RMSE, barrier option price, Monte Carlo price, and Greeks.

## Technologies

- C++17
- QuantLib
- CMake
- MSYS2
