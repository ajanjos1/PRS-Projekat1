#include "ErosionSimulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace prs
{
    namespace
    {
        // Koliko se Δt smije povećati iz koraka u korak (izbjegava nagle skokove kad voda tek počne teći).
        constexpr double kMaxTimeStepGrowth = 1.25;
    }

    double SimulationStatistics::waterBalanceError() const
    {
        const double expected = initialWaterVolume + rainVolume - boundaryWaterOutflow - infiltratedVolume -
                                evaporatedVolume + negativeDepthCorrection;
        return finalWaterVolume - expected;
    }

    double SimulationStatistics::waterBalanceRelativeError() const
    {
        const double reference = initialWaterVolume + rainVolume;
        return reference > 0.0 ? waterBalanceError() / reference : 0.0;
    }

    double SimulationStatistics::materialBalanceRelativeError() const
    {
        return grossErodedVolume > 0.0 ? materialBalanceError / grossErodedVolume : 0.0;
    }

    ErosionSimulation::ErosionSimulation(const Heightmap& terrain, const SimulationConfig& config)
        : _config(config), _pipes(config.flow), _erosion(config.erosion)
    {
        const std::string scaleProblem = terrain.scale.validate();
        if (!scaleProblem.empty())
            throw std::invalid_argument(scaleProblem);
        const std::string configProblem = config.validate();
        if (!configProblem.empty())
            throw std::invalid_argument(configProblem);
        if (terrain.elevation.empty())
            throw std::invalid_argument("Teren je prazan");

        _state.allocate(terrain.width(), terrain.height(), terrain.scale.cellSizeMeters);
        _state.terrain = terrain.elevation;
        _state.terrainNext = terrain.elevation;
        _initialTerrain = terrain.elevation;

        const double depth = config.erosion.erodibleDepthMeters;
        for (std::size_t i = 0; i < _state.cellCount(); ++i)
            _state.bedrock[i] = depth > 0.0 ? terrain.elevation[i] - depth : std::numeric_limits<double>::lowest();

        _lastTimeStep = config.flow.maxTimeStepSeconds;
        _stats.initialWaterVolume = totalWaterVolume(); // 0: simulacija počinje sa suhim terenom
    }

    bool ErosionSimulation::finished() const
    {
        return _stats.aborted || _time >= _config.totalSeconds();
    }

    double ErosionSimulation::step()
    {
        if (finished())
            return 0.0;

        const double rainEnd = _config.rainSeconds();
        const double phaseEnd = (_time < rainEnd) ? rainEnd : _config.totalSeconds();
        const double remaining = phaseEnd - _time;

        // ---- izbor Δt
        double dt;
        const bool rainAhead = _config.rainfall.depthAddedBetween(_time, phaseEnd) > 0.0;
        if (!_waterPresent && !rainAhead)
        {
            // Nigdje nema vode i do kraja faze ne pada kiša: nema protoka ni erozije. Jedini proces je
            // taloženje eventualnog suspendovanog sedimenta, čiji je izraz (1 − e^(−Kd·Δt)) tačan za bilo
            // koji Δt – zato se ostatak faze može preći u jednom koraku.
            dt = remaining;
        }
        else
        {
            dt = _pipes.stableTimeStep(_lastWaveSpeed, _state.cellSize);
            dt = std::min(dt, _lastTimeStep * kMaxTimeStepGrowth);
            if (dt < _config.flow.minTimeStepSeconds && remaining > _config.flow.minTimeStepSeconds)
            {
                _stats.aborted = true;
                _stats.abortReason = "Stabilan vremenski korak je pao ispod minimalnog (" +
                                     std::to_string(_config.flow.minTimeStepSeconds) +
                                     " s) – numericka nestabilnost ili ekstremni parametri.";
                return 0.0;
            }
            if (dt >= remaining * (1.0 - 1e-12))
                dt = remaining; // posljednji korak faze tačno pogađa kraj kiše / kraj simulacije
        }

        // ---- 1. kiša (samo dio koji pada u [t, t + Δt])
        const double rainDepth = _config.rainfall.depthAddedBetween(_time, _time + dt);
        _stats.rainVolume += _pipes.addUniformWater(_state, rainDepth);

        // ---- 2.–4. protoci kroz virtuelne cijevi (ograničeni dostupnom vodom)
        _pipes.computeOutflowFlux(_state, dt);

        // ---- 5.–6. nova dubina vode i brzina
        const WaterStepStats water = _pipes.updateWaterAndVelocity(_state, dt);
        _stats.boundaryWaterOutflow += water.boundaryOutflowVolume;
        _stats.negativeDepthCorrection += water.negativeDepthCorrection;
        _stats.peakWaterDepth = std::max(_stats.peakWaterDepth, water.maxDepth);
        _stats.peakSpeed = std::max(_stats.peakSpeed, water.maxSpeed);

        // ---- 7.–10. kapacitet, erozija/depozicija, transport sedimenta, nova visina terena
        if (_config.erosion.enabled)
        {
            const ErosionStepStats ero = _erosion.erodeAndDeposit(_state, dt);
            _stats.grossErodedVolume += ero.erodedVolume;
            _stats.grossDepositedVolume += ero.depositedVolume;

            const TransportStepStats tr = _erosion.transportSediment(_state, dt);
            _stats.sedimentBoundaryOutflow += tr.boundaryOutflowVolume;
            _stats.sedimentNegativeCorrection += tr.negativeCorrection;
        }

        // ---- gubici vode (ako su uključeni)
        const LossStepStats loss = _pipes.applyLosses(_state, dt);
        _stats.infiltratedVolume += loss.infiltratedVolume;
        _stats.evaporatedVolume += loss.evaporatedVolume;

        _waterPresent = water.maxDepth > 0.0 && loss.waterRemains;
        _lastWaveSpeed = water.maxWaveSpeed;
        _lastTimeStep = std::max(dt, _config.flow.minTimeStepSeconds);

        _time = (dt == remaining) ? phaseEnd : _time + dt;
        _stats.steps += 1;
        _sumTimeSteps += dt;
        if (_stats.steps == 1)
            _stats.minTimeStep = _stats.maxTimeStep = dt;
        else
        {
            // Posljednji korak faze je skraćen da pogodi kraj – ne računa se u minimum.
            if (dt != remaining)
                _stats.minTimeStep = std::min(_stats.minTimeStep, dt);
            _stats.maxTimeStep = std::max(_stats.maxTimeStep, dt);
        }
        return dt;
    }

    const SimulationStatistics& ErosionSimulation::run(const ProgressCallback& progress, int reportCount,
                                                       double heartbeatSeconds)
    {
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        auto lastReport = start;
        const double total = _config.totalSeconds();
        reportCount = std::max(1, reportCount);
        int nextReport = 1;

        while (!finished())
        {
            step();
            if (!progress)
                continue;
            const double fraction = total > 0.0 ? _time / total : 1.0;
            const bool milestone = fraction * reportCount >= nextReport || finished();
            const auto now = Clock::now();
            const bool heartbeat = !milestone && heartbeatSeconds > 0.0 &&
                                   std::chrono::duration<double>(now - lastReport).count() >= heartbeatSeconds;
            if (milestone || heartbeat)
            {
                SimulationProgress p;
                p.step = _stats.steps;
                p.simulatedSeconds = _time;
                p.totalSeconds = total;
                p.lastTimeStep = _lastTimeStep;
                p.wallSeconds = std::chrono::duration<double>(now - start).count();
                p.raining = _time < _config.rainSeconds();
                p.milestone = milestone;
                progress(p);
                lastReport = now;
                while (fraction * reportCount >= nextReport)
                    ++nextReport;
            }
        }
        finalize();
        _stats.wallSeconds = std::chrono::duration<double>(Clock::now() - start).count();
        return _stats;
    }

    void ErosionSimulation::finalize()
    {
        if (_finalized)
            return;
        _finalized = true;
        _stats.suspendedAtEndVolume = totalSuspendedSedimentVolume();
        if (_config.erosion.enabled && _config.erosion.settleSuspendedSedimentAtEnd)
            _stats.settledAtEndVolume = _erosion.settleSuspendedSediment(_state);
        updateFinalStatistics();
    }

    double ErosionSimulation::totalWaterVolume() const
    {
        double sum = 0.0;
        for (double d : _state.water.values())
            sum += d;
        return sum * _state.cellArea();
    }

    double ErosionSimulation::totalSuspendedSedimentVolume() const
    {
        double sum = 0.0;
        for (double s : _state.sediment.values())
            sum += s;
        return sum * _state.cellArea();
    }

    double ErosionSimulation::materialImbalance() const
    {
        // Sabiraju se RAZLIKE (b − b0), a ne apsolutne visine: tako zaokruživanje ne zavisi od visine terena.
        double sum = 0.0;
        for (std::size_t i = 0; i < _state.cellCount(); ++i)
            sum += (_state.terrain[i] - _initialTerrain[i]) + _state.sediment[i];
        return sum * _state.cellArea() + _stats.sedimentBoundaryOutflow;
    }

    void ErosionSimulation::updateFinalStatistics()
    {
        SimulationStatistics& st = _stats;
        st.simulatedSeconds = _time;
        st.meanTimeStep = st.steps > 0 ? _sumTimeSteps / static_cast<double>(st.steps) : 0.0;
        st.finalWaterVolume = totalWaterVolume();
        st.materialBalanceError = materialImbalance();

        const double area = _state.cellArea();
        const std::size_t n = _state.cellCount();
        double lowered = 0.0, raised = 0.0, absSum = 0.0;
        double maxLow = 0.0, maxHigh = 0.0;
        std::int64_t changed = 0;
        bool finite = true;
        double minWater = std::numeric_limits<double>::infinity();
        double minSed = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < n; ++i)
        {
            const double b = _state.terrain[i];
            const double d = _state.water[i];
            const double s = _state.sediment[i];
            if (!std::isfinite(b) || !std::isfinite(d) || !std::isfinite(s) || !std::isfinite(_state.velocityX[i]) ||
                !std::isfinite(_state.velocityY[i]))
                finite = false;
            minWater = std::min(minWater, d);
            minSed = std::min(minSed, s);
            const double delta = b - _initialTerrain[i];
            if (delta < 0.0)
            {
                lowered += -delta;
                maxLow = std::max(maxLow, -delta);
            }
            else
            {
                raised += delta;
                maxHigh = std::max(maxHigh, delta);
            }
            absSum += std::abs(delta);
            if (std::abs(delta) > 1.0e-3)
                ++changed;
        }
        st.netLoweredVolume = lowered * area;
        st.netRaisedVolume = raised * area;
        st.maxLowering = maxLow;
        st.maxRaise = maxHigh;
        st.meanAbsChange = n > 0 ? absSum / static_cast<double>(n) : 0.0;
        st.cellsChangedMoreThan1mm = changed;
        st.allFinite = finite;
        st.minWaterDepth = minWater;
        st.minSediment = minSed;
    }
}
