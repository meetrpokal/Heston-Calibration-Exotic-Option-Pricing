# Quantitative Modeling of Equity Derivatives

This repository implements an end-to-end Heston calibration and exotic option pricing
pipeline (barrier pricing) using QuantLib. It includes Monte Carlo pricing (antithetic
variates), a Black–Scholes baseline, finite-difference fallbacks, and a small test
suite.

## Features

- Heston model calibration (Levenberg–Marquardt)
- Down-and-out barrier option pricing (Monte Carlo + Fd where available)
- Black–Scholes comparison and Greeks (FD / MC fallback)
- Curve building example (discount & dividend outputs)
- Unit tests (`test_runner.exe`)

## Architecture

Below is a high-level architecture diagram (Mermaid) describing the main
components and data flow in the project. It shows where CSV data is parsed,
where curves are built, where calibration and pricing run, and where outputs
are produced.

```mermaid
flowchart LR
	Data[CSV: data/spy_options.csv] --> Parser[src/main.cpp\n(CSV parser & preprocessing)]
	Parser --> Curve[Curve Builder\n(ZeroCurve / Dividends)]
	Curve --> Calibration[Heston Calibration\n(HestonModelHelper + LM)]
	Calibration --> Pricers
	subgraph Pricers [Pricing Engines]
		Pricers --> FD[FdHestonBarrierEngine\n(FD when valid)]
		Pricers --> MC[Monte Carlo\n(antithetic variates)]
		Pricers --> BS[Black–Scholes\n(baseline)]
	end
	Pricers --> Outputs[Console + outputs/*.csv]
	Tests[tests/test.cpp] --> Build[Build / CMake]
	Build --> Parser
	style Data fill:#f9f,stroke:#333,stroke-width:1px
	style Pricers fill:#efe,stroke:#333,stroke-width:1px
```

Short notes:

- `src/main.cpp` implements the end-to-end pipeline: parse CSV → build curves →
	calibrate Heston → price barrier options (FD if available, otherwise MC)
- `tests/test.cpp` contains lightweight unit checks for curve building,
	calibration convergence, and Monte Carlo standard error.
- Build is performed via `CMakeLists.txt` and targets `main` and `test_runner`.

## Requirements

- MSYS2 (UCRT64) recommended on Windows
- QuantLib built and installed for the target environment
- CMake 3.15+ and a MinGW toolchain (UCRT64)

> On Windows the project is known to build reliably in the MSYS2 UCRT64 shell.

## Build (recommended)

Open the MSYS2 UCRT64 shell, then run:

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j4
```

Alternative (older style):

```bash
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
mingw32-make -j4
```

## Run

Basic run (defaults):

```bash
./build/main.exe
```

Example with Monte Carlo settings:

```bash
./build/main.exe --paths 200000 --timesteps 252 --seed 42
```

Run the test suite:

```bash
./build/test_runner.exe
```

## Outputs

- `outputs/discounts.csv` — sample discount curve produced by the example
- `outputs/dividends.csv` — sample dividend curve produced by the example
- The program prints calibrated Heston parameters, RMSE per slice, Monte Carlo
	barrier price with standard error, Black–Scholes baseline price, and computed
	Greeks (FD or MC fallback).

## Sample Output

A successful run prints lines similar to:

```text
Loaded options from data/spy_options.csv: 242 rows
Constructed calibration surface with 63 rows from market prices
RMSE = 0.0333635
Monte Carlo Barrier Price: 46.3224
Standard Error: 2.39771
Using ATM vol implied from calibrated Heston: 0.29875
Delta (FD): 0.618059
Vega (FD): -0.0989788
```

To verify the project quickly, run:

```bash
./build/test_runner.exe
./build/main.exe --paths 1000 --timesteps 32 --seed 42
```

## Troubleshooting

- If CMake fails to detect a compiler or QuantLib when run from PowerShell, open the
	MSYS2 UCRT64 shell and rebuild there. Ensure QuantLib headers/libs are visible
	(on MSYS2: `C:/msys64/ucrt64/include` and `C:/msys64/ucrt64/lib`).
- Set PATH in the UCRT64 shell if needed: `export PATH=/ucrt64/bin:$PATH`.
- Some QuantLib engines (FD barrier pricer) may throw interpolation/extrapolation
	errors for extreme inputs — the code falls back to Monte Carlo in those cases.

## Notes

- This project was developed and tested on Windows using MSYS2 UCRT64. Cross-
	platform builds may require adjusting the `CMakeLists.txt` find logic for
	QuantLib and Boost.

If you'd like, I can add a short example command that reproduces the sample
output shown in the repository or open a PR to push this README change to the
remote. Which would you prefer?
