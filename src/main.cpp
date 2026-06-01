#include <ql/quantlib.hpp>
#include <ql/pricingengines/barrier/fdhestonbarrierengine.hpp>
#include <ql/pricingengines/barrier/analyticbarrierengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/math/randomnumbers/randomsequencegenerator.hpp>
#include <ql/math/randomnumbers/mt19937uniformrng.hpp>
#include <ql/math/randomnumbers/boxmullergaussianrng.hpp>
#include <ql/methods/montecarlo/multipathgenerator.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <chrono>
#include <ctime>
#include <random>
#include <cmath>
#include <vector>
#include <filesystem>
#include <limits>

using namespace std;
using namespace QuantLib;

struct VolData {
    double strike;
    double maturity;
    double volatility;
};

Date currentQuantLibDate() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};

#if defined(_WIN32)
    localtime_s(&localTime, &nowTime);
#else
    localTime = *std::localtime(&nowTime);
#endif

    return Date(
        localTime.tm_mday,
        static_cast<Month>(localTime.tm_mon + 1),
        localTime.tm_year + 1900
    );
}

Volatility impliedVolFromMarketPrice(
    Real marketPrice,
    Real spot,
    Real strike,
    const Date& valuationDate,
    const Date& expiryDate,
    Rate riskFreeRateValue,
    Rate dividendYieldValue,
    const Calendar& calendar,
    Option::Type optionType = Option::Call) {

    Handle<Quote> spotHandle(
        ext::shared_ptr<Quote>(new SimpleQuote(spot))
    );

    Handle<YieldTermStructure> riskFreeRate(
        ext::shared_ptr<YieldTermStructure>(
            new FlatForward(valuationDate, riskFreeRateValue, Actual365Fixed())
        )
    );

    Handle<YieldTermStructure> dividendYield(
        ext::shared_ptr<YieldTermStructure>(
            new FlatForward(valuationDate, dividendYieldValue, Actual365Fixed())
        )
    );

    Handle<BlackVolTermStructure> volGuess(
        ext::shared_ptr<BlackVolTermStructure>(
            new BlackConstantVol(valuationDate, calendar, 0.20, Actual365Fixed())
        )
    );

    ext::shared_ptr<BlackScholesMertonProcess> process(
        new BlackScholesMertonProcess(
            spotHandle,
            dividendYield,
            riskFreeRate,
            volGuess
        )
    );

    ext::shared_ptr<StrikedTypePayoff> payoff(
        new PlainVanillaPayoff(optionType, strike)
    );

    ext::shared_ptr<Exercise> exercise(
        new EuropeanExercise(expiryDate)
    );

    VanillaOption option(payoff, exercise);
    return option.impliedVolatility(marketPrice, process, 1e-8, 500, 1e-4, 5.0);
}

int main(int argc, char* argv[]) {

    Date todaysDate = currentQuantLibDate();
    Settings::instance().evaluationDate() = todaysDate;

    Size numPaths = 100000;
    Size timeSteps = 252;
    BigNatural seed = 42;
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if ((arg == "--paths" || arg == "-n") && i + 1 < argc) {
            numPaths = static_cast<Size>(stoull(argv[++i]));
        } else if ((arg == "--timesteps" || arg == "-t") && i + 1 < argc) {
            timeSteps = static_cast<Size>(stoull(argv[++i]));
        } else if ((arg == "--seed" || arg == "-s") && i + 1 < argc) {
            seed = static_cast<BigNatural>(stoull(argv[++i]));
        }
    }


    struct OptionData {
        Date expiry;
        double strike;
        double market_price;
        double bid;
        double ask;
        double implied_vol;
        double volume;
        double open_interest;
        double underlying_price;
    };

    vector<OptionData> options;
    vector<VolData> volSurface;

    vector<string> dataCandidates = {
        "data/spy_options.csv",
        "../data/spy_options.csv"
    };

    ifstream file;
    string dataPath;
    for (const auto &candidate : dataCandidates) {
        file.open(candidate);
        if (file.is_open()) {
            dataPath = candidate;
            break;
        }
        file.clear();
    }

    if (!file.is_open()) {
        cout << "Failed to open spy_options.csv. Tried: data/spy_options.csv and ../data/spy_options.csv" << endl;
        return 1;
    }

    string line;
    getline(file, line);
    while (getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        stringstream ss(line);
        string token;
        OptionData od;
        od.underlying_price = 0.0;
        if (!getline(ss, token, ',')) continue;
        int y = 0, m = 0, d = 0;
        if (sscanf(token.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
            od.expiry = Date(d, static_cast<Month>(m), y);
        } else {
            continue;
        }
        if (!getline(ss, token, ',')) continue; od.strike = stod(token);
        if (!getline(ss, token, ',')) continue; od.market_price = stod(token);
        if (!getline(ss, token, ',')) od.bid = 0.0; else od.bid = stod(token);
        if (!getline(ss, token, ',')) od.ask = 0.0; else od.ask = stod(token);
        if (!getline(ss, token, ',')) od.implied_vol = 0.0; else od.implied_vol = stod(token);
        if (!getline(ss, token, ',')) od.volume = 0.0; else od.volume = stod(token);
        if (!getline(ss, token, ',')) od.open_interest = 0.0; else od.open_interest = stod(token);
        if (getline(ss, token, ',')) {
            od.underlying_price = stod(token);
        }

        options.push_back(od);
    }
    file.close();

    cout << "Loaded options from " << dataPath << ": " << options.size() << " rows" << endl;

    if (options.empty()) {
        cout << "No usable option rows were loaded from spy_options.csv; cannot calibrate or price." << endl;
        return 1;
    }



    Real spotPrice = 0.0;
    for (const auto& o : options) {
        if (o.underlying_price > 0.0) {
            spotPrice = o.underlying_price;
            break;
        }
    }

    if (spotPrice <= 0.0) {
        spotPrice = 750.0;
        cout << "No underlying price metadata found in CSV; using fallback spot=" << spotPrice << endl;
    } else {
        cout << "Using underlying spot from CSV metadata: " << spotPrice << endl;
    }

    Rate riskFreeRateValue = 0.05;
    Rate dividendYieldValue = 0.01;

    Volatility initialVol = 0.20;

    Calendar calendar = TARGET();
    DayCounter dc = Actual365Fixed();

    Handle<Quote> spotHandle(
        ext::shared_ptr<Quote>(
            new SimpleQuote(spotPrice)
        )
    );

    Handle<YieldTermStructure> riskFreeRate(
        ext::shared_ptr<YieldTermStructure>(
            new FlatForward(
                todaysDate,
                riskFreeRateValue,
                Actual365Fixed()
            )
        )
    );

    Handle<YieldTermStructure> dividendYield(
        ext::shared_ptr<YieldTermStructure>(
            new FlatForward(
                todaysDate,
                dividendYieldValue,
                Actual365Fixed()
            )
        )
    );

    for (const auto& o : options) {
        if (o.market_price <= 0.0) {
            continue;
        }

        const double years = dc.yearFraction(todaysDate, o.expiry);
        if (years <= 0.0) {
            continue;
        }

        try {
            const Volatility iv = impliedVolFromMarketPrice(
                o.market_price,
                spotPrice,
                o.strike,
                todaysDate,
                o.expiry,
                riskFreeRateValue,
                dividendYieldValue,
                calendar,
                Option::Call
            );

            if (iv > 1e-4 && std::isfinite(iv)) {
                VolData v;
                v.strike = o.strike;
                v.maturity = years;
                v.volatility = iv;
                volSurface.push_back(v);
            }
        } catch (const std::exception&) {
            continue;
        }
    }

    cout << "Constructed calibration surface with " << volSurface.size() << " rows from market prices" << endl;

    if (volSurface.empty()) {
        cout << "No usable implied vols could be computed from market prices; regenerate the dataset with download_option.py." << endl;
        return 1;
    }


    std::vector<Date> treasuryDates;
    std::vector<Rate> treasuryZeros;

    treasuryDates.push_back(todaysDate);
    treasuryZeros.push_back(0.013);

    treasuryDates.push_back(todaysDate + Period(1, Months));
    treasuryZeros.push_back(0.0135);

    treasuryDates.push_back(todaysDate + Period(3, Months));
    treasuryZeros.push_back(0.0140);

    treasuryDates.push_back(todaysDate + Period(6, Months));
    treasuryZeros.push_back(0.0150);

    treasuryDates.push_back(todaysDate + Period(1, Years));
    treasuryZeros.push_back(0.0170);

    treasuryDates.push_back(todaysDate + Period(2, Years));
    treasuryZeros.push_back(0.0200);

    treasuryDates.push_back(todaysDate + Period(5, Years));
    treasuryZeros.push_back(0.0300);

    treasuryDates.push_back(todaysDate + Period(10, Years));
    treasuryZeros.push_back(0.0350);

    ext::shared_ptr<ZeroCurve> treasuryZeroCurve(
        new ZeroCurve(treasuryDates, treasuryZeros, Actual365Fixed())
    );

    Handle<YieldTermStructure> treasuryCurve(treasuryZeroCurve);

    cout << "\nTreasury-based zero curve (example):\n";
    cout << "Date         | DiscountFactor | ZeroRate (cont)" << endl;
    vector<Period> queryPeriods = { Period(30, Days), Period(90, Days), Period(180, Days), Period(1, Years), Period(2, Years), Period(5, Years), Period(10, Years) };
    struct CurveRow { Date d; DiscountFactor treasuryDf; Rate treasuryZr; DiscountFactor rfDf; Rate rfZr; DiscountFactor divDf; Rate divZr; };
    std::vector<CurveRow> rows;
    for (auto p : queryPeriods) {
        Date d = todaysDate + p;
        DiscountFactor treasuryDf = treasuryCurve->discount(d);
        Rate treasuryZr = treasuryCurve->zeroRate(d, Actual365Fixed(), Continuous);
        DiscountFactor rfDf = riskFreeRate->discount(d);
        Rate rfZr = riskFreeRate->zeroRate(d, Actual365Fixed(), Continuous);
        DiscountFactor divDf = dividendYield->discount(d);
        Rate divZr = dividendYield->zeroRate(d, Actual365Fixed(), Continuous);
        cout << d << "  | " << treasuryDf << " | " << treasuryZr << endl;
        rows.push_back({d, treasuryDf, treasuryZr, rfDf, rfZr, divDf, divZr});
    }

    std::filesystem::create_directories("outputs");
    std::ofstream out1("outputs/discounts.csv");
    out1 << "date,treasury_discount,treasury_zero_cont,riskfree_discount,riskfree_zero_cont,dividend_discount,dividend_zero_cont\n";
    for (const auto &r : rows) {
        out1 << r.d << "," << r.treasuryDf << "," << r.treasuryZr << "," << r.rfDf << "," << r.rfZr << "," << r.divDf << "," << r.divZr << "\n";
    }
    out1.close();

    std::ofstream out2("outputs/dividends.csv");
    out2 << "date,dividend_discount,dividend_zero_cont\n";
    for (const auto &r : rows) {
        out2 << r.d << "," << r.divDf << "," << r.divZr << "\n";
    }
    out2.close();

    cout << "\nWrote curve outputs to outputs/discounts.csv and outputs/dividends.csv" << endl;


    Real v0 = 0.04;
    Real kappa = 1.0;
    Real theta = 0.04;
    Real sigma = 0.5;
    Real rho = -0.5;


    ext::shared_ptr<HestonProcess> hestonProcess(
        new HestonProcess(
            riskFreeRate,
            dividendYield,
            spotHandle,
            v0,
            kappa,
            theta,
            sigma,
            rho
        )
    );


    ext::shared_ptr<HestonModel> hestonModel(
        new HestonModel(hestonProcess)
    );

    cout << "Heston Model Created Successfully!" << endl;


    std::vector<ext::shared_ptr<CalibrationHelper>> helpers;
    std::vector<ext::shared_ptr<HestonModelHelper>> helperDetails;

    ext::shared_ptr<PricingEngine> engine(
        new AnalyticHestonEngine(hestonModel)
    );

    for (const auto& v : volSurface) {

        Period maturity(int(v.maturity * 365), Days);

        Handle<Quote> volHandle(
            ext::shared_ptr<Quote>(
                new SimpleQuote(v.volatility)
            )
        );

        ext::shared_ptr<HestonModelHelper> helper(
            new HestonModelHelper(
                maturity,
                calendar,
                spotPrice,
                v.strike,
                volHandle,
                riskFreeRate,
                dividendYield,
                BlackCalibrationHelper::ImpliedVolError
            )
        );

        helper->setPricingEngine(engine);

        helpers.push_back(helper);
        helperDetails.push_back(helper);
    }


    LevenbergMarquardt optimizer;

    EndCriteria endCriteria(
        1000,
        500,
        1e-8,
        1e-8,
        1e-8
    );

    hestonModel->calibrate(
        helpers,
        optimizer,
        endCriteria
    );

    cout << "\nCalibrated Parameters:\n";

    cout << "v0     = " << hestonModel->v0() << endl;
    cout << "kappa  = " << hestonModel->kappa() << endl;
    cout << "theta  = " << hestonModel->theta() << endl;
    cout << "sigma  = " << hestonModel->sigma() << endl;
    cout << "rho    = " << hestonModel->rho() << endl;

    double errorSum = 0.0;
    Size errorCount = 0;
    Size fallbackCount = 0;
    map<double, pair<double, Size>> sliceStats;

    cout << "\nCalibration Errors:\n";

    for (Size i = 0; i < helperDetails.size(); ++i) {

        double marketVol = volSurface[i].volatility;
        double modelPrice = helperDetails[i]->modelValue();
        double modelVol = std::numeric_limits<double>::quiet_NaN();
        double error = 0.0;
        bool usedFallback = false;

        try {
            modelVol = helperDetails[i]->impliedVolatility(
                modelPrice,
                1e-8,
                200,
                0.0001,
                5.0
            );
            error = marketVol - modelVol;
        } catch (const std::exception&) {
            error = helperDetails[i]->calibrationError();
            modelVol = marketVol - error;
            usedFallback = true;
        }

        if (!usedFallback) {
            errorSum += error * error;
            sliceStats[volSurface[i].maturity].first += error * error;
            sliceStats[volSurface[i].maturity].second += 1;
            ++errorCount;
        } else {
            ++fallbackCount;
        }

        cout << "Strike: " << volSurface[i].strike
             << "  Market Vol: " << marketVol
             << "  Model Vol: " << modelVol
             << "  Error: " << error;

        if (usedFallback) {
            cout << "  (fallback calibrationError)";
        }

        cout
             << endl;
    }

    double rmse = errorCount > 0 ? sqrt(errorSum / static_cast<double>(errorCount)) : std::numeric_limits<double>::quiet_NaN();

    cout << "\nRMSE = " << rmse << endl;
    if (fallbackCount > 0) {
        cout << "Skipped " << fallbackCount << " rows from RMSE because impliedVolatility() fell back to calibrationError()." << endl;
    }

    cout << "\nRMSE by Maturity Slice:\n";
    for (const auto& entry : sliceStats) {
        double sliceRmse = sqrt(entry.second.first / entry.second.second);
        cout << "Maturity: " << entry.first
             << "  RMSE = " << sliceRmse
             << endl;
    }

    Real strike = std::max(spotPrice * 1.05, spotPrice + 1.0);
    Real barrier = spotPrice * 0.80;

    Date maturityDate = todaysDate + Period(1, Years);

    Option::Type optionType = Option::Call;

    ext::shared_ptr<StrikedTypePayoff> payoff(
        new PlainVanillaPayoff(
            optionType,
            strike
        )
    );

    ext::shared_ptr<Exercise> exercise(
        new EuropeanExercise(maturityDate)
    );

    ext::shared_ptr<BarrierOption> barrierOptionPtr(
        new BarrierOption(
            Barrier::DownOut,
            barrier,
            0.0,
            payoff,
            exercise
        )
    );

    if (barrier >= spotPrice) {
        cout << "Warning: computed barrier (" << barrier << ") >= spot (" << spotPrice << ") - adjusting to 90% of spot." << endl;
        barrier = spotPrice * 0.9;
    }

    cout << "Using strike=" << strike << " barrier=" << barrier << " maturity=" << maturityDate << endl;

    try {
        ext::shared_ptr<PricingEngine> barrierEngine(
            new FdHestonBarrierEngine(hestonModel)
        );

        barrierOptionPtr->setPricingEngine(barrierEngine);

        Real hestonPrice = barrierOptionPtr->NPV();

        cout << "\nBarrier Option Price (Heston): "
             << hestonPrice
             << endl;
    } catch (const QuantLib::Error &e) {
        cout << "QuantLib error during Heston barrier pricing: " << e.what() << endl;
    } catch (const std::exception &e) {
        cout << "STD exception during Heston barrier pricing: " << e.what() << endl;
    }

    Time maturity = 1.0;
    Real payoffSum = 0.0;
    std::vector<Real> payoffs;

    try {
        Real dt = maturity / static_cast<Time>(timeSteps);

        BoxMullerGaussianRng<MersenneTwisterUniformRng> gaussianRng{
            MersenneTwisterUniformRng(seed)
        };

        RandomSequenceGenerator<BoxMullerGaussianRng<MersenneTwisterUniformRng>> rsg(
            timeSteps * 2,
            gaussianRng
        );

        cout << "\nMonte Carlo settings: paths=" << numPaths << " timesteps=" << timeSteps << " seed=" << seed << "\n";

        TimeGrid grid(maturity, timeSteps);

        MultiPathGenerator<RandomSequenceGenerator<BoxMullerGaussianRng<MersenneTwisterUniformRng>>> generator(
            hestonProcess,
            grid,
            rsg,
            false
        );

        payoffs.reserve(numPaths);

    for (Size i = 0; i < numPaths / 2; ++i) {

        Sample<MultiPath> sample = generator.next();
        Sample<MultiPath> antiSample = generator.antithetic();

        MultiPath paths = sample.value;
        MultiPath antiPaths = antiSample.value;

        Path stockPath = paths[0];
        Path antiStockPath = antiPaths[0];

        bool knockedOut = false;
        bool antiKnockedOut = false;

        for (Size j = 0; j < stockPath.length(); ++j) {

            if (stockPath[j] <= barrier) {
                knockedOut = true;
                break;
            }
        }

        for (Size j = 0; j < antiStockPath.length(); ++j) {

            if (antiStockPath[j] <= barrier) {
                antiKnockedOut = true;
                break;
            }
        }

        Real payoffValue = 0.0;
        Real antiPayoffValue = 0.0;

        if (!knockedOut) {

            Real finalPrice =
                stockPath[stockPath.length() - 1];

            payoffValue =
                std::max(finalPrice - strike, 0.0);
        }

        if (!antiKnockedOut) {

            Real finalPrice =
                antiStockPath[antiStockPath.length() - 1];

            antiPayoffValue =
                std::max(finalPrice - strike, 0.0);
        }

        payoffSum += payoffValue + antiPayoffValue;
        payoffs.push_back(payoffValue);
        payoffs.push_back(antiPayoffValue);
    }

    if (numPaths % 2 != 0) {

        Sample<MultiPath> sample = generator.next();

        MultiPath paths = sample.value;

        Path stockPath = paths[0];

        bool knockedOut = false;

        for (Size j = 0; j < stockPath.length(); ++j) {

            if (stockPath[j] <= barrier) {
                knockedOut = true;
                break;
            }
        }

        if (!knockedOut) {
            Real finalPrice = stockPath[stockPath.length() - 1];
            Real payoffValue = std::max(finalPrice - strike, 0.0);
            payoffSum += payoffValue;
            payoffs.push_back(payoffValue);
        } else {
            payoffs.push_back(0.0);
        }
    }

    } catch (const QuantLib::Error &e) {
        cout << "QuantLib error during Monte Carlo pricing: " << e.what() << endl;
    } catch (const std::exception &e) {
        cout << "STD exception during Monte Carlo pricing: " << e.what() << endl;
    }

    Real discountFactor = std::exp(-riskFreeRateValue * maturity);

    Real mcPrice = std::numeric_limits<Real>::quiet_NaN();
    Real standardError = std::numeric_limits<Real>::quiet_NaN();

    if (!payoffs.empty()) {
        mcPrice = discountFactor * payoffSum / static_cast<Real>(numPaths);

        cout << "\nMonte Carlo Barrier Price: " << mcPrice << endl;

        Real mean = payoffSum / static_cast<Real>(payoffs.size());

        Real variance = 0.0;

        for (Real p : payoffs) {
            variance += (p - mean) * (p - mean);
        }

        if (payoffs.size() > 1) {
            variance /= (static_cast<Real>(payoffs.size()) - 1.0);
            Real standardDeviation = std::sqrt(variance);
            standardError = discountFactor * standardDeviation / std::sqrt(static_cast<Real>(payoffs.size()));
        }

        cout << "Standard Error: " << standardError << endl;
    } else {
        cout << "No payoffs were generated in Monte Carlo; price unavailable." << endl;
    }


    double atmVol = 0.20;
    try {
        ext::shared_ptr<StrikedTypePayoff> atmPayoff(
            new PlainVanillaPayoff(Option::Call, spotPrice)
        );

        ext::shared_ptr<Exercise> atmExercise(
            new EuropeanExercise(maturityDate)
        );

        VanillaOption atmVanilla(atmPayoff, atmExercise);
        atmVanilla.setPricingEngine(ext::shared_ptr<PricingEngine>(new AnalyticHestonEngine(hestonModel)));

        Real atmPrice = atmVanilla.NPV();

        Handle<BlackVolTermStructure> bsVolGuess(
            ext::shared_ptr<BlackVolTermStructure>(
                new BlackConstantVol(
                    todaysDate,
                    calendar,
                    0.20,
                    Actual365Fixed()
                )
            )
        );

        ext::shared_ptr<BlackScholesMertonProcess> bsGuessProcess(
            new BlackScholesMertonProcess(
                spotHandle,
                dividendYield,
                riskFreeRate,
                bsVolGuess
            )
        );

        atmVol = atmVanilla.impliedVolatility(
            atmPrice,
            bsGuessProcess,
            1e-8,
            500,
            1e-4,
            5.0
        );

        cout << "Using ATM vol implied from calibrated Heston: " << atmVol << endl;
    } catch (const QuantLib::Error &e) {
        cout << "QuantLib error while computing ATM vol from Heston: " << e.what() << endl;
        cout << "Using fallback ATM vol: " << atmVol << endl;
    } catch (const std::exception &e) {
        cout << "STD exception while computing ATM vol from Heston: " << e.what() << endl;
        cout << "Using fallback ATM vol: " << atmVol << endl;
    }

    Handle<BlackVolTermStructure> blackVolTS(
        ext::shared_ptr<BlackVolTermStructure>(
            new BlackConstantVol(
                todaysDate,
                calendar,
                atmVol,
                Actual365Fixed()
            )
        )
    );

    Real bsPrice = std::numeric_limits<Real>::quiet_NaN();

    try {
        ext::shared_ptr<BlackScholesMertonProcess> bsProcess(
            new BlackScholesMertonProcess(
                spotHandle,
                dividendYield,
                riskFreeRate,
                blackVolTS
            )
        );

        ext::shared_ptr<PricingEngine> bsEngine(
            new AnalyticBarrierEngine(bsProcess)
        );

        barrierOptionPtr->setPricingEngine(bsEngine);

        bsPrice = barrierOptionPtr->NPV();

        cout << "\nBlack-Scholes Barrier Price: "
             << bsPrice
             << endl;
    } catch (const QuantLib::Error &e) {
        cout << "QuantLib error during Black-Scholes pricing: " << e.what() << endl;
    } catch (const std::exception &e) {
        cout << "STD exception during Black-Scholes pricing: " << e.what() << endl;
    }

    cout << "\nComparison:\n";

    cout << "Heston MC Price      : "
         << mcPrice
         << endl;

    cout << "Black-Scholes Price  : "
         << bsPrice
         << endl;

    cout << "Difference           : "
         << std::fabs(mcPrice - bsPrice)
         << endl;

    {
        Real h = std::max(spotPrice * 0.001, 0.5);

        auto priceByFd = [&](ext::shared_ptr<HestonModel> model)->pair<bool, Real> {
            try {
                ext::shared_ptr<PricingEngine> e(new FdHestonBarrierEngine(model));
                barrierOptionPtr->setPricingEngine(e);
                Real p = barrierOptionPtr->NPV();
                return {true, p};
            } catch (...) {
                return {false, std::numeric_limits<Real>::quiet_NaN()};
            }
        };

        auto priceByMcWithParams = [&](Real spotBump, Real sigmaBump, Size paths)->Real {
            Real localPayoffSum = 0.0;
            Size localPaths = paths;

            BoxMullerGaussianRng<MersenneTwisterUniformRng> gaussianRng{MersenneTwisterUniformRng(seed + 123)};
            RandomSequenceGenerator<BoxMullerGaussianRng<MersenneTwisterUniformRng>> rsg(timeSteps * 2, gaussianRng);
            TimeGrid grid(maturity, timeSteps);

            ext::shared_ptr<Quote> spotQ(new SimpleQuote(spotBump));
            Handle<Quote> spotH(spotQ);

            ext::shared_ptr<HestonProcess> proc(new HestonProcess(
                riskFreeRate,
                dividendYield,
                spotH,
                hestonModel->v0(),
                hestonModel->kappa(),
                hestonModel->theta(),
                hestonModel->sigma() + sigmaBump,
                hestonModel->rho()
            ));

            MultiPathGenerator<RandomSequenceGenerator<BoxMullerGaussianRng<MersenneTwisterUniformRng>>> gen(
                proc,
                grid,
                rsg,
                false
            );

            for (Size i = 0; i < localPaths / 2; ++i) {
                Sample<MultiPath> s = gen.next();
                Sample<MultiPath> a = gen.antithetic();

                MultiPath p = s.value;
                MultiPath ap = a.value;

                Path path = p[0];
                Path apath = ap[0];

                bool ko = false, ako = false;
                for (Size j = 0; j < path.length(); ++j) {
                    if (path[j] <= barrier) { ko = true; break; }
                }
                for (Size j = 0; j < apath.length(); ++j) {
                    if (apath[j] <= barrier) { ako = true; break; }
                }

                Real pay = 0.0, apay = 0.0;
                if (!ko) pay = std::max(path[path.length()-1] - strike, 0.0);
                if (!ako) apay = std::max(apath[apath.length()-1] - strike, 0.0);

                localPayoffSum += pay + apay;
            }

            if (localPaths % 2 != 0) {
                Sample<MultiPath> s = gen.next();
                MultiPath p = s.value;
                Path path = p[0];
                bool ko = false;
                for (Size j = 0; j < path.length(); ++j) if (path[j] <= barrier) { ko = true; break; }
                if (!ko) localPayoffSum += std::max(path[path.length()-1] - strike, 0.0);
            }

            Real disc = std::exp(-riskFreeRateValue * maturity);
            return disc * localPayoffSum / static_cast<Real>(localPaths);
        };

        ext::shared_ptr<HestonProcess> pUp(new HestonProcess(riskFreeRate, dividendYield, Handle<Quote>(ext::shared_ptr<Quote>(new SimpleQuote(spotPrice + h))),
            hestonModel->v0(), hestonModel->kappa(), hestonModel->theta(), hestonModel->sigma(), hestonModel->rho()));

        ext::shared_ptr<HestonProcess> pDown(new HestonProcess(riskFreeRate, dividendYield, Handle<Quote>(ext::shared_ptr<Quote>(new SimpleQuote(spotPrice - h))),
            hestonModel->v0(), hestonModel->kappa(), hestonModel->theta(), hestonModel->sigma(), hestonModel->rho()));

        ext::shared_ptr<HestonModel> mUp(new HestonModel(pUp));
        ext::shared_ptr<HestonModel> mDown(new HestonModel(pDown));

        auto fdUp = priceByFd(mUp);
        auto fdDown = priceByFd(mDown);

        Real delta = std::numeric_limits<Real>::quiet_NaN();
        if (fdUp.first && fdDown.first) {
            delta = (fdUp.second - fdDown.second) / (2.0 * h);
            cout << "\nDelta (FD): " << delta << endl;
        } else {
            Size pathsForGreeks = std::max(static_cast<Size>(50000), numPaths / 2);
            Real pUpMc = priceByMcWithParams(spotPrice + h, 0.0, pathsForGreeks);
            Real pDownMc = priceByMcWithParams(spotPrice - h, 0.0, pathsForGreeks);
            delta = (pUpMc - pDownMc) / (2.0 * h);
            cout << "\nDelta (MC fallback): " << delta << " (paths=" << pathsForGreeks << ")" << endl;
        }

        Real volShift = 0.01;
        ext::shared_ptr<HestonProcess> vsUp(new HestonProcess(riskFreeRate, dividendYield, spotHandle,
            hestonModel->v0(), hestonModel->kappa(), hestonModel->theta(), hestonModel->sigma() + volShift, hestonModel->rho()));
        ext::shared_ptr<HestonProcess> vsDown(new HestonProcess(riskFreeRate, dividendYield, spotHandle,
            hestonModel->v0(), hestonModel->kappa(), hestonModel->theta(), hestonModel->sigma() - volShift, hestonModel->rho()));

        ext::shared_ptr<HestonModel> vmUp(new HestonModel(vsUp));
        ext::shared_ptr<HestonModel> vmDown(new HestonModel(vsDown));

        auto fvUp = priceByFd(vmUp);
        auto fvDown = priceByFd(vmDown);

        Real vega = std::numeric_limits<Real>::quiet_NaN();
        if (fvUp.first && fvDown.first) {
            vega = (fvUp.second - fvDown.second) / (2.0 * volShift);
            cout << "Vega (FD): " << vega << endl;
        } else {
            Size pathsForGreeks = std::max(static_cast<Size>(50000), numPaths / 2);
            Real pUpMc = priceByMcWithParams(spotPrice, volShift, pathsForGreeks);
            Real pDownMc = priceByMcWithParams(spotPrice, -volShift, pathsForGreeks);
            vega = (pUpMc - pDownMc) / (2.0 * volShift);
            cout << "Vega (MC fallback): " << vega << " (paths=" << pathsForGreeks << ")" << endl;
        }
    }

    return 0;
}
