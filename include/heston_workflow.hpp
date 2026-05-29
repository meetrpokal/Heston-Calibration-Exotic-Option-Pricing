#pragma once

#include <ql/quantlib.hpp>

#include <string>
#include <vector>

struct VolSurfacePoint {
    QuantLib::Real strike;
    QuantLib::Real maturity;
    QuantLib::Volatility volatility;
};

struct HestonParameters {
    QuantLib::Real v0;
    QuantLib::Real kappa;
    QuantLib::Real theta;
    QuantLib::Real sigma;
    QuantLib::Real rho;
};

class HestonWorkflow {
public:
    HestonWorkflow(QuantLib::Real spot, const QuantLib::Date& valuationDate);

    std::vector<VolSurfacePoint> loadVolSurfaceCsv(const std::string& filePath) const;

    QuantLib::Handle<QuantLib::YieldTermStructure> buildRiskFreeCurve(QuantLib::Rate rate) const;
    QuantLib::Handle<QuantLib::YieldTermStructure> buildDividendCurve(QuantLib::Rate rate) const;

    QuantLib::ext::shared_ptr<QuantLib::HestonModel> buildHestonModel(
        const QuantLib::Handle<QuantLib::YieldTermStructure>& riskFreeCurve,
        const QuantLib::Handle<QuantLib::YieldTermStructure>& dividendCurve,
        const HestonParameters& initialGuess) const;

    HestonParameters calibrateHestonModel(
        const std::vector<VolSurfacePoint>& marketSurface,
        const QuantLib::Handle<QuantLib::YieldTermStructure>& riskFreeCurve,
        const QuantLib::Handle<QuantLib::YieldTermStructure>& dividendCurve,
        const QuantLib::ext::shared_ptr<QuantLib::HestonModel>& model) const;

    QuantLib::Real priceBarrierOptionMC(
        const HestonParameters& params,
        const QuantLib::Handle<QuantLib::YieldTermStructure>& riskFreeCurve,
        const QuantLib::Handle<QuantLib::YieldTermStructure>& dividendCurve,
        QuantLib::Option::Type optionType,
        QuantLib::Barrier::Type barrierType,
        QuantLib::Real strike,
        QuantLib::Real barrier,
        QuantLib::Real rebate,
        QuantLib::Time maturityYears,
        QuantLib::Size timeSteps,
        QuantLib::Size paths) const;

private:
    QuantLib::Period maturityToPeriod(QuantLib::Real maturityYears) const;

    QuantLib::Calendar calendar_;
    QuantLib::DayCounter dayCounter_;
    QuantLib::Real spot_;
    QuantLib::Date valuationDate_;
};