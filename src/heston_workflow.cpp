#include "heston_workflow.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>

using namespace QuantLib;

HestonWorkflow::HestonWorkflow(Real spot, const Date& valuationDate)
: calendar_(TARGET()), dayCounter_(Actual365Fixed()), spot_(spot), valuationDate_(valuationDate) {
    Settings::instance().evaluationDate() = valuationDate_;
}

std::vector<VolSurfacePoint> HestonWorkflow::loadVolSurfaceCsv(const std::string& filePath) const {
    std::ifstream input(filePath);
    std::vector<VolSurfacePoint> surface;

    if (!input.is_open()) {
        throw std::runtime_error("Unable to open vol surface CSV: " + filePath);
    }

    std::string line;
    bool headerSkipped = false;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        if (!headerSkipped) {
            headerSkipped = true;
            continue;
        }

        std::stringstream stream(line);
        std::string strikeText;
        std::string maturityText;
        std::string volatilityText;

        if (!std::getline(stream, strikeText, ',')) {
            continue;
        }
        if (!std::getline(stream, maturityText, ',')) {
            continue;
        }
        if (!std::getline(stream, volatilityText, ',')) {
            continue;
        }

        surface.push_back(VolSurfacePoint{
            std::stod(strikeText),
            std::stod(maturityText),
            std::stod(volatilityText)
        });
    }

    return surface;
}

Handle<YieldTermStructure> HestonWorkflow::buildRiskFreeCurve(Rate rate) const {
    return Handle<YieldTermStructure>(
        ext::make_shared<FlatForward>(valuationDate_, rate, dayCounter_));
}

Handle<YieldTermStructure> HestonWorkflow::buildDividendCurve(Rate rate) const {
    return Handle<YieldTermStructure>(
        ext::make_shared<FlatForward>(valuationDate_, rate, dayCounter_));
}

ext::shared_ptr<HestonModel> HestonWorkflow::buildHestonModel(
    const Handle<YieldTermStructure>& riskFreeCurve,
    const Handle<YieldTermStructure>& dividendCurve,
    const HestonParameters& initialGuess) const {

    const auto spotHandle = Handle<Quote>(ext::make_shared<SimpleQuote>(spot_));
    const auto process = ext::make_shared<HestonProcess>(
        riskFreeCurve,
        dividendCurve,
        spotHandle,
        initialGuess.v0,
        initialGuess.kappa,
        initialGuess.theta,
        initialGuess.sigma,
        initialGuess.rho);

    return ext::make_shared<HestonModel>(process);
}

Period HestonWorkflow::maturityToPeriod(Real maturityYears) const {
    const int months = std::max(1, static_cast<int>(std::lround(maturityYears * 12.0)));
    return Period(months, Months);
}

HestonParameters HestonWorkflow::calibrateHestonModel(
    const std::vector<VolSurfacePoint>& marketSurface,
    const Handle<YieldTermStructure>& riskFreeCurve,
    const Handle<YieldTermStructure>& dividendCurve,
    const ext::shared_ptr<HestonModel>& model) const {

    std::vector<ext::shared_ptr<CalibrationHelper>> helpers;
    helpers.reserve(marketSurface.size());

    for (const auto& point : marketSurface) {
        const auto maturity = maturityToPeriod(point.maturity);
        const auto quote = Handle<Quote>(ext::make_shared<SimpleQuote>(point.volatility));
        auto helper = ext::make_shared<HestonModelHelper>(
            maturity,
            calendar_,
            spot_,
            point.strike,
            quote,
            riskFreeCurve,
            dividendCurve);
        helper->setPricingEngine(ext::make_shared<AnalyticHestonEngine>(model));
        helpers.push_back(helper);
    }

    LevenbergMarquardt optimizer;
    const EndCriteria endCriteria(1000, 500, 1e-8, 1e-8, 1e-8);
    model->calibrate(helpers, optimizer, endCriteria);

    const Array params = model->params();
    return HestonParameters{
        params[4],
        params[1],
        params[0],
        params[2],
        params[3]
    };
}

Real HestonWorkflow::priceBarrierOptionMC(
    const HestonParameters& params,
    const Handle<YieldTermStructure>& riskFreeCurve,
    const Handle<YieldTermStructure>& dividendCurve,
    Option::Type optionType,
    Barrier::Type barrierType,
    Real strike,
    Real barrier,
    Real rebate,
    Time maturityYears,
    Size timeSteps,
    Size paths) const {

    const Rate riskFreeRate = riskFreeCurve->zeroRate(maturityYears, Continuous, NoFrequency).rate();
    const Rate dividendRate = dividendCurve->zeroRate(maturityYears, Continuous, NoFrequency).rate();
    const Time dt = maturityYears / static_cast<Time>(timeSteps);
    const Real sqrtDt = std::sqrt(dt);
    const Real discountFactor = std::exp(-riskFreeRate * maturityYears);
    const Real correlationScale = std::sqrt(std::max<Real>(0.0, 1.0 - params.rho * params.rho));

    std::mt19937_64 rng(42U);
    std::normal_distribution<Real> normal(0.0, 1.0);

    auto simulatePath = [&](Real z1Sign, Real z2Sign) {
        Real spot = spot_;
        Real variance = std::max<Real>(params.v0, 0.0);
        bool knockedOut = false;
        bool knockedIn = false;

        for (Size step = 0; step < timeSteps; ++step) {
            const Real z1 = z1Sign * normal(rng);
            const Real zPerp = z2Sign * normal(rng);
            const Real z2 = params.rho * z1 + correlationScale * zPerp;

            const Real variancePositive = std::max<Real>(variance, 0.0);
            const Real driftVariance = params.kappa * (params.theta - variancePositive) * dt;
            const Real diffusionVariance = params.sigma * std::sqrt(variancePositive) * sqrtDt * z2;
            variance = std::max<Real>(variance + driftVariance + diffusionVariance, 0.0);

            const Real spotDrift = (riskFreeRate - dividendRate - 0.5 * variancePositive) * dt;
            const Real spotDiffusion = std::sqrt(variancePositive) * sqrtDt * z1;
            spot *= std::exp(spotDrift + spotDiffusion);

            if (barrierType == Barrier::UpOut && spot >= barrier) {
                knockedOut = true;
                break;
            }
            if (barrierType == Barrier::DownOut && spot <= barrier) {
                knockedOut = true;
                break;
            }
            if (barrierType == Barrier::UpIn && spot >= barrier) {
                knockedIn = true;
            }
            if (barrierType == Barrier::DownIn && spot <= barrier) {
                knockedIn = true;
            }
        }

        const Real payoff = std::max<Real>(
            (optionType == Option::Call ? spot - strike : strike - spot),
            0.0);

        if (barrierType == Barrier::UpIn || barrierType == Barrier::DownIn) {
            return knockedIn ? payoff : rebate;
        }

        return knockedOut ? rebate : payoff;
    };

    Real priceSum = 0.0;
    const Size pairCount = paths / 2;
    for (Size i = 0; i < pairCount; ++i) {
        const Real pathValueOne = simulatePath(1.0, 1.0);
        const Real pathValueTwo = simulatePath(-1.0, -1.0);
        priceSum += 0.5 * (pathValueOne + pathValueTwo);
    }

    if (paths % 2 != 0) {
        priceSum += simulatePath(1.0, 1.0);
    }

    const Real expectedPayoff = priceSum / static_cast<Real>(paths);
    return discountFactor * expectedPayoff;
}