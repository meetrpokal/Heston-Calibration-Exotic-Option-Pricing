#include <ql/quantlib.hpp>
#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <chrono>
#include <ctime>

using namespace QuantLib;

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

int main() {
    try {
        Date today = currentQuantLibDate();
        Settings::instance().evaluationDate() = today;

        Calendar cal = TARGET();
        DayCounter dc = Actual365Fixed();
        Rate r = 0.02;

        Handle<YieldTermStructure> flat(ext::make_shared<FlatForward>(today, r, dc));
        Date oneYear = today + Period(1, Years);
        Real df = flat->discount(oneYear);
        Real expected = std::exp(-r * 1.0);
        QL_REQUIRE(std::fabs(df - expected) < 1e-12, "Flat curve discount mismatch");

        std::cout << "Curve test passed. DF(1y)=" << df << "\n";

        Real spot = 100.0;
        Handle<Quote> spotH(ext::make_shared<SimpleQuote>(spot));
        Handle<YieldTermStructure> riskFree(ext::make_shared<FlatForward>(today, 0.01, dc));
        Handle<YieldTermStructure> dividend(ext::make_shared<FlatForward>(today, 0.0, dc));

        Real v0 = 0.02, kappa = 1.0, theta = 0.02, sigma_calib = 0.5, rho = -0.2;
        ext::shared_ptr<HestonProcess> calibProc(new HestonProcess(riskFree, dividend, spotH, v0, kappa, theta, sigma_calib, rho));
        ext::shared_ptr<HestonModel> calibModel(new HestonModel(calibProc));

        std::vector<Real> strikes = {80, 90, 100, 110, 120};
        std::vector<Real> maturities = {0.25, 0.5, 1.0};

        std::vector<ext::shared_ptr<HestonModelHelper>> helperStrong;
        std::vector<ext::shared_ptr<CalibrationHelper>> calibHelpers;

        for (Real T : maturities) {
            Period mp(int(T * 365), Days);
            for (Real K : strikes) {
                Real baseVol = 0.20 + 0.01 * ((K - spot) / spot) + 0.02 * (T - 0.5);
                if (baseVol < 0.001) baseVol = 0.001;
                ext::shared_ptr<Quote> volQ(ext::make_shared<SimpleQuote>(baseVol));
                ext::shared_ptr<HestonModelHelper> h(ext::make_shared<HestonModelHelper>(mp, TARGET(), spotH, K, Handle<Quote>(volQ), riskFree, dividend, BlackCalibrationHelper::ImpliedVolError));
                helperStrong.push_back(h);
                calibHelpers.push_back(ext::static_pointer_cast<CalibrationHelper>(h));
            }
        }

        LevenbergMarquardt opt;
        EndCriteria ec(1000, 500, 1e-8, 1e-8, 1e-8);

        std::vector<Real> initialErrors;
        for (auto &h : helperStrong) {
            h->setPricingEngine(ext::shared_ptr<PricingEngine>(new AnalyticHestonEngine(calibModel)));
        }
        for (auto &h : calibHelpers) {
            initialErrors.push_back(h->calibrationError());
        }

        Real initSq = 0.0;
        for (Real e : initialErrors) initSq += e * e;

        try {
            calibModel->calibrate(calibHelpers, opt, ec);
        } catch (...) {
            QL_FAIL("Calibration threw an exception");
        }

        std::vector<Real> postErrors;
        for (auto &h : helperStrong) {
            h->setPricingEngine(ext::shared_ptr<PricingEngine>(new AnalyticHestonEngine(calibModel)));
        }
        for (auto &h : calibHelpers) {
            postErrors.push_back(h->calibrationError());
        }

        Real postSq = 0.0;
        for (Real e : postErrors) postSq += e * e;

        QL_REQUIRE(postSq <= initSq, "Calibration did not reduce total squared error");
        std::cout << "Calibration test passed. initErr=" << initSq << " postErr=" << postSq << "\n";

        Size N = 10000;
        Real S0 = 100.0, K = 100.0, sigma = 0.2, T = 1.0, r0 = 0.01;
        std::mt19937_64 rng(42);
        std::normal_distribution<Real> nd(0.0, 1.0);
        Real sum = 0.0, sumsq = 0.0;
        for (Size i = 0; i < N; ++i) {
            Real z = nd(rng);
            Real ST = S0 * std::exp((r0 - 0.5 * sigma * sigma) * T + sigma * std::sqrt(T) * z);
            Real pay = std::max(ST - K, 0.0);
            sum += pay;
            sumsq += pay * pay;
        }

        Real mean = sum / static_cast<Real>(N);
        Real var = (sumsq - static_cast<Real>(N) * mean * mean) / static_cast<Real>(N - 1);
        Real se = std::sqrt(var / static_cast<Real>(N));
        Real disc = std::exp(-r0 * T);
        Real price = disc * mean;
        Real seDisc = disc * se;

        QL_REQUIRE(seDisc < std::max(0.01, 0.05 * price), "Monte Carlo standard error too large");
        std::cout << "MC test passed. price=" << price << " se=" << seDisc << "\n";

        std::cout << "All tests passed." << std::endl;
        return 0;
    } catch (std::exception &e) {
        std::cerr << "Test failure: " << e.what() << std::endl;
        return 1;
    }
}