#pragma once
// ============================================================================
//  Ultimate Vocal - motor de afinación (sin dependencias de JUCE)
//
//  * Detección de tono: YIN con interpolación parabólica
//  * Cuantización a escala con histéresis y velocidad de retune
//  * Síntesis: TD-PSOLA (grains síncronos con el periodo) con alineación a
//    una retícula de épocas, que mantiene la fase y evita artefactos de peine
//  * Formantes: se desplazan independientemente del tono (relectura del grain)
// ============================================================================
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace uv
{
constexpr float kMinF0 = 90.0f;     // Hz  (límite inferior de detección)
constexpr float kMaxF0 = 1000.0f;   // Hz  (límite superior)
constexpr float kFormantMin = 0.70f;
constexpr float kFormantMax = 1.42f;

inline float hzToMidi (float hz)   { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
inline float midiToHz (float midi) { return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f); }

// ----------------------------------------------------------------------------
struct EngineSettings
{
    std::array<bool, 12> noteEnabled {};   // clases de nota habilitadas (0 = Do)
    float retuneMs   = 20.0f;              // 0 = instantáneo (efecto robótico)
    float amount     = 1.0f;               // 0..1 cantidad de corrección
    float transpose  = 0.0f;               // semitonos
    float fineCents  = 0.0f;               // cents
    float formantSt  = 0.0f;               // semitonos de desplazamiento de formante
};

// ----------------------------------------------------------------------------
class PitchEngine
{
public:
    static constexpr int kHop = 256;                 // muestras entre análisis
    static constexpr int kHistSize = 1024;           // entradas de historial de f0

    struct Frame
    {
        float f0 = 0.0f;          // Hz (0 = sin voz)
        float conf = 0.0f;
        bool  voiced = false;
        float midiIn = 0.0f;      // nota detectada (MIDI fraccionario)
        float midiTarget = 0.0f;  // nota objetivo tras cuantizar
        float shiftSt = 0.0f;     // desplazamiento aplicado (semitonos)
    };

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = std::max (1, std::min (2, numChannels));

        decim = std::max (1, (int) std::lround (sr / 24000.0));
        srDet = (float) (sr / decim);
        tauMin = std::max (2, (int) (srDet / kMaxF0));
        tauMax = (int) (srDet / kMinF0) + 1;
        winDet = 512;
        detBufSize = 1;
        while (detBufSize < winDet + tauMax + 8) detBufSize <<= 1;
        detBuf.assign ((size_t) detBufSize, 0.0f);
        yinDiff.assign ((size_t) tauMax + 2, 0.0f);
        yinCmnd.assign ((size_t) tauMax + 2, 0.0f);

        maxPeriod = (int) std::ceil (sr / kMinF0);
        wCap = (int) std::ceil (maxPeriod * 1.2);
        delay = (int) std::ceil (wCap * (1.0 + kFormantMax + 0.5)) + 8;

        ringSize = 1;
        while (ringSize < delay * 4) ringSize <<= 1;
        ringMask = ringSize - 1;
        for (int c = 0; c < 2; ++c)
        {
            inRing[(size_t) c].assign ((size_t) ringSize, 0.0f);
            outRing[(size_t) c].assign ((size_t) ringSize, 0.0f);
        }

        // tabla de ventana Hann periódica
        hannTable.resize (kHannN + 1);
        for (int i = 0; i <= kHannN; ++i)
            hannTable[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * 3.14159265358979f * (float) i / (float) kHannN));

        reset();
    }

    void reset()
    {
        std::fill (detBuf.begin(), detBuf.end(), 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inRing[(size_t) c].begin(), inRing[(size_t) c].end(), 0.0f);
            std::fill (outRing[(size_t) c].begin(), outRing[(size_t) c].end(), 0.0f);
        }
        nIn = 0;
        decCount = 0; decAcc = 0.0f; detWrite = 0;
        hopCount = 0;
        frameCount = 0;
        for (auto& f : hist) f = Frame {};
        shiftSmoothed = 0.0f;
        currentNote = -1;
        lastEpoch = 0.0;
        tsNext = (double) delay;
        lastT0 = (float) (sr / 200.0);
        wasVoiced = false;
        latestFrame = Frame {};
    }

    void setSettings (const EngineSettings& s) { settings = s; }
    int  getLatencySamples() const noexcept { return delay; }
    Frame getLatestFrame() const noexcept { return latestFrame; }
    uint64_t getFrameCount() const noexcept { return frameCount; }
    Frame getFrame (uint64_t idx) const noexcept { return hist[idx % kHistSize]; }

    // Procesa una muestra (todos los canales). `dry` devuelve la señal original
    // retardada por la misma latencia, para mezclar seco/húmedo alineado.
    void processSample (const float* in, float* wet, float* dry)
    {
        const int idx = (int) (nIn & (uint64_t) ringMask);
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c)
        {
            inRing[(size_t) c][(size_t) idx] = in[c];
            mono += in[c];
        }
        mono /= (float) channels;

        pushDetectionSample (mono);
        if (++hopCount >= kHop)
        {
            hopCount = 0;
            analyse();
        }

        spawnGrains();

        const int readIdx = (int) ((nIn - (uint64_t) delay) & (uint64_t) ringMask);
        const bool haveHistory = nIn >= (uint64_t) delay;
        for (int c = 0; c < channels; ++c)
        {
            wet[c] = outRing[(size_t) c][(size_t) readIdx];
            outRing[(size_t) c][(size_t) readIdx] = 0.0f;
            dry[c] = haveHistory ? inRing[(size_t) c][(size_t) readIdx] : 0.0f;
        }
        ++nIn;
    }

private:
    // ------------------------------------------------------------------ YIN --
    void pushDetectionSample (float x)
    {
        decAcc += x;
        if (++decCount >= decim)
        {
            detBuf[(size_t) (detWrite & (detBufSize - 1))] = decAcc / (float) decim;
            ++detWrite;
            decAcc = 0.0f;
            decCount = 0;
        }
    }

    float sampleDet (int back) const
    {
        // muestra más reciente = back 0
        return detBuf[(size_t) ((detWrite - 1 - back) & (detBufSize - 1))];
    }

    bool runYin (float& f0Out, float& confOut)
    {
        if (detWrite < winDet + tauMax + 2) return false;

        // energía para descartar silencio
        double energy = 0.0;
        for (int j = 0; j < winDet; ++j) { const float v = sampleDet (j); energy += (double) v * v; }
        if (energy / winDet < 1.0e-7) return false;

        // función de diferencia
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < winDet; ++j)
            {
                const float d = sampleDet (j) - sampleDet (j + tau);
                sum += (double) d * d;
            }
            yinDiff[(size_t) tau] = (float) sum;
        }

        // media acumulada normalizada
        yinCmnd[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += yinDiff[(size_t) tau];
            yinCmnd[(size_t) tau] = running > 1.0e-12 ? (float) (yinDiff[(size_t) tau] * tau / running) : 1.0f;
        }

        const float threshold = 0.15f;
        int tauEst = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (yinCmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && yinCmnd[(size_t) tau + 1] < yinCmnd[(size_t) tau]) ++tau;
                tauEst = tau;
                break;
            }
        }

        if (tauEst < 0)
        {
            // mínimo global como candidato débil
            float best = 1.0f; int bt = -1;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (yinCmnd[(size_t) tau] < best) { best = yinCmnd[(size_t) tau]; bt = tau; }
            if (bt < 0 || best > 0.35f) return false;
            tauEst = bt;
        }

        // interpolación parabólica
        float tauF = (float) tauEst;
        if (tauEst > 1 && tauEst < tauMax)
        {
            const float s0 = yinCmnd[(size_t) tauEst - 1];
            const float s1 = yinCmnd[(size_t) tauEst];
            const float s2 = yinCmnd[(size_t) tauEst + 1];
            const float denom = (s0 + s2 - 2.0f * s1);
            if (std::abs (denom) > 1.0e-9f)
                tauF += 0.5f * (s0 - s2) / denom;
        }

        f0Out = srDet / tauF;
        confOut = 1.0f - yinCmnd[(size_t) tauEst];
        return f0Out >= kMinF0 && f0Out <= kMaxF0;
    }

    // ------------------------------------------------------ cuantización ----
    float quantise (float midi)
    {
        bool any = false;
        for (bool b : settings.noteEnabled) any = any || b;
        if (! any) { currentNote = -1; return midi; }

        const int centre = (int) std::lround (midi);
        int best = -1; float bestDist = 1.0e9f;
        for (int n = centre - 7; n <= centre + 7; ++n)
        {
            const int pc = ((n % 12) + 12) % 12;
            if (! settings.noteEnabled[(size_t) pc]) continue;
            const float d = std::abs (midi - (float) n);
            if (d < bestDist) { bestDist = d; best = n; }
        }
        if (best < 0) return midi;

        // histéresis: solo cambia de nota si la nueva está claramente más cerca
        if (currentNote >= 0 && currentNote != best)
        {
            const int pcCur = ((currentNote % 12) + 12) % 12;
            if (settings.noteEnabled[(size_t) pcCur])
            {
                const float dCur = std::abs (midi - (float) currentNote);
                if (dCur - bestDist < 0.12f) best = currentNote;
            }
        }
        currentNote = best;
        return (float) best;
    }

    void analyse()
    {
        Frame f;
        float f0 = 0.0f, conf = 0.0f;
        const bool voiced = runYin (f0, conf);
        f.voiced = voiced;
        f.f0 = voiced ? f0 : 0.0f;
        f.conf = voiced ? conf : 0.0f;

        const float hopSec = (float) kHop / (float) sr;
        float desired = 0.0f;

        if (voiced)
        {
            const float midi = hzToMidi (f0);
            f.midiIn = midi;
            const float target = quantise (midi) + settings.transpose + settings.fineCents * 0.01f;
            f.midiTarget = target;
            desired = (target - midi) * settings.amount;
            lastT0 = (float) (sr / f0);
            if (! wasVoiced) shiftSmoothed = desired * 0.0f; // arranca desde 0 tras silencio
        }
        else
        {
            currentNote = -1;
            f.midiIn = hist[(frameCount + kHistSize - 1) % kHistSize].midiIn;
            f.midiTarget = f.midiIn;
        }
        wasVoiced = voiced;

        const float tau = std::max (0.0f, settings.retuneMs) * 0.001f;
        const float a = tau < 1.0e-4f ? 1.0f : 1.0f - std::exp (-hopSec / tau);
        shiftSmoothed += (desired - shiftSmoothed) * (voiced ? a : std::min (1.0f, a * 0.5f + 0.05f));
        f.shiftSt = shiftSmoothed;

        hist[frameCount % kHistSize] = f;
        latestFrame = f;
        ++frameCount;
    }

    // -------------------------------------------------------------- PSOLA ---
    const Frame& frameForTime (double ts) const
    {
        // el análisis YIN cubre [t-win, t]; su centro está unos winDet*decim/2 atrás
        const double centreOffset = 0.5 * winDet * decim;
        int64_t k = (int64_t) std::floor ((ts + centreOffset) / (double) kHop) - 1;
        const int64_t newest = (int64_t) frameCount - 1;
        if (k > newest) k = newest;
        if (k < 0) k = 0;
        if (newest - k >= kHistSize) k = newest - kHistSize + 1;
        return hist[(size_t) (k % kHistSize)];
    }

    float readInterp (int ch, double pos) const
    {
        const double fl = std::floor (pos);
        const float frac = (float) (pos - fl);
        const auto i0 = (int) ((int64_t) fl & ringMask);
        const auto i1 = (i0 + 1) & ringMask;
        const auto& r = inRing[(size_t) ch];
        return r[(size_t) i0] + frac * (r[(size_t) i1] - r[(size_t) i0]);
    }

    float hann (float phase01) const
    {
        const float p = phase01 * (float) kHannN;
        const int i = std::min (kHannN - 1, std::max (0, (int) p));
        const float fr = p - (float) i;
        return hannTable[(size_t) i] + fr * (hannTable[(size_t) i + 1] - hannTable[(size_t) i]);
    }

    void spawnGrains()
    {
        const float formantRatio = std::min (kFormantMax, std::max (kFormantMin,
                                   std::pow (2.0f, settings.formantSt / 12.0f)));

        // Genera todos los grains cuyos datos de entrada ya están disponibles
        for (int guard = 0; guard < 4; ++guard)
        {
            const Frame& fr = frameForTime (tsNext);
            const float T0 = std::min ((float) maxPeriod, std::max ((float) (sr / kMaxF0), lastT0Of (fr)));
            const float ratio = fr.voiced ? std::pow (2.0f, fr.shiftSt / 12.0f) : std::pow (2.0f, fr.shiftSt / 12.0f);
            const float r = std::min (2.0f, std::max (0.5f, ratio));
            const float hop = T0 / r;
            const int W = std::max (8, (int) std::min ((float) wCap, std::max (T0, hop)));
            const int span = (int) std::ceil (W * formantRatio) + 2;

            // ¿tenemos ya todas las muestras necesarias?
            if (tsNext + 0.5 * T0 + span > (double) nIn - 1.0) break;

            // época de análisis más cercana (retícula de periodo T0)
            double k = std::round ((tsNext - lastEpoch) / (double) T0);
            if (k < 0.0) k = 0.0;
            double epoch = lastEpoch + k * (double) T0;
            if (k == 0.0 && lastEpoch > tsNext + T0) epoch = lastEpoch;
            lastEpoch = epoch;

            const int centre = (int) std::lround (tsNext);
            const float gain = std::min (1.0f, hop / (float) W);
            for (int j = 0; j < 2 * W; ++j)
            {
                const float w = hann ((float) j / (float) (2 * W)) * gain;
                const double srcPos = epoch + (double) (j - W) * (double) formantRatio;
                const int outIdx = (int) ((int64_t) (centre + (j - W)) & ringMask);
                for (int c = 0; c < channels; ++c)
                    outRing[(size_t) c][(size_t) outIdx] += w * readInterp (c, srcPos);
            }
            tsNext += (double) hop;
        }
    }

    float lastT0Of (const Frame& fr) const
    {
        return fr.voiced && fr.f0 > 0.0f ? (float) (sr / fr.f0) : lastT0;
    }

    // ---------------------------------------------------------- estado ------
    static constexpr int kHannN = 4096;

    double sr = 48000.0;
    int channels = 1;

    // detección
    int decim = 2, tauMin = 20, tauMax = 300, winDet = 512, detBufSize = 2048;
    float srDet = 24000.0f;
    std::vector<float> detBuf, yinDiff, yinCmnd;
    int decCount = 0; float decAcc = 0.0f; int detWrite = 0; int hopCount = 0;

    // síntesis
    int maxPeriod = 534, wCap = 641, delay = 1900, ringSize = 8192, ringMask = 8191;
    std::array<std::vector<float>, 2> inRing, outRing;
    std::vector<float> hannTable;
    uint64_t nIn = 0;
    double tsNext = 0.0, lastEpoch = 0.0;
    float lastT0 = 240.0f;

    // historial de análisis
    std::array<Frame, kHistSize> hist {};
    uint64_t frameCount = 0;
    Frame latestFrame {};
    float shiftSmoothed = 0.0f;
    int currentNote = -1;
    bool wasVoiced = false;

    EngineSettings settings;
};

} // namespace uv
