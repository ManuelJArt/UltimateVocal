// Prueba offline del motor: señal armónica desafinada -> debe converger a la nota de la escala.
#include "PitchEngine.h"
#include <cstdio>
#include <vector>

static float estimateF0 (const std::vector<float>& x, int start, int len, double sr)
{
    // autocorrelación normalizada, 80..1000 Hz; elige el lag más corto cercano al máximo
    // (evita confundir el periodo con sus múltiplos)
    const int lo = (int) (sr / 1000), hi = (int) (sr / 80);
    std::vector<double> nc ((size_t) hi + 2, 0.0);
    double best = -1e9;
    for (int lag = lo; lag < hi; ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (int i = 0; i < len; ++i)
        {
            const double a = x[(size_t) (start + i)], b = x[(size_t) (start + i + lag)];
            s += a * b; e1 += a * a; e2 += b * b;
        }
        nc[(size_t) lag] = s / std::sqrt (e1 * e2 + 1e-12);
        best = std::max (best, nc[(size_t) lag]);
    }
    for (int lag = lo + 1; lag < hi - 1; ++lag)
        if (nc[(size_t) lag] >= 0.95 * best && nc[(size_t) lag] >= nc[(size_t) lag - 1] && nc[(size_t) lag] >= nc[(size_t) lag + 1])
        {
            const double s0 = nc[(size_t) lag - 1], s1 = nc[(size_t) lag], s2 = nc[(size_t) lag + 1];
            const double d = s0 - 2 * s1 + s2;
            const double off = std::abs (d) > 1e-12 ? 0.5 * (s0 - s2) / d : 0.0;
            return (float) (sr / (lag + off));
        }
    return 0.0f;
}

static std::vector<float> run (double sr, float inHz, uv::EngineSettings st, int seconds, int& latency)
{
    uv::PitchEngine eng;
    eng.prepare (sr, 1);
    eng.setSettings (st);
    latency = eng.getLatencySamples();
    const int N = (int) (sr * seconds);
    std::vector<float> out ((size_t) N + 4096);
    double phase = 0.0;
    for (int n = 0; n < N + 4096; ++n)
    {
        float x = 0.0f;
        for (int h = 1; h <= 8; ++h) x += std::sin ((float) (h * phase)) / (float) h;
        x *= 0.3f;
        phase += 2.0 * 3.14159265358979 * inHz / sr;
        float wet, dry; float in = x;
        eng.processSample (&in, &wet, &dry);
        out[(size_t) n] = wet;
    }
    return out;
}

int main()
{
    int fails = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        uv::EngineSettings st;
        // Do mayor
        for (int pc : { 0, 2, 4, 5, 7, 9, 11 }) st.noteEnabled[(size_t) pc] = true;
        st.retuneMs = 0.0f;

        struct Case { float inHz, expectHz; const char* name; } cases[] = {
            { 210.0f, 220.0f, "210 Hz -> A3 (220)" },
            { 250.0f, 246.94f, "250 Hz -> B3 (246.9)" },
            { 330.0f, 329.63f, "330 Hz -> E4 (329.6)" },
        };
        for (auto& c : cases)
        {
            int lat = 0;
            auto out = run (sr, c.inHz, st, 2, lat);
            const int start = (int) (sr * 1.2);
            const float f = estimateF0 (out, start, (int) (sr * 0.1), sr);
            const float cents = 1200.0f * std::log2 (f / c.expectHz);
            const bool ok = std::abs (cents) < 25.0f;
            std::printf ("sr=%6.0f  %-22s -> %.2f Hz (%.1f cents)  latency=%d smp (%.1f ms)  %s\n",
                         sr, c.name, f, cents, lat, 1000.0 * lat / sr, ok ? "OK" : "FAIL");
            fails += ok ? 0 : 1;
        }

        // sin corrección (amount = 0): debe conservar el tono de entrada
        st.amount = 0.0f;
        int lat = 0;
        auto out = run (sr, 210.0f, st, 2, lat);
        const float f = estimateF0 (out, (int) (sr * 1.2), (int) (sr * 0.1), sr);
        const float cents = 1200.0f * std::log2 (f / 210.0f);
        const bool ok = std::abs (cents) < 25.0f;
        std::printf ("sr=%6.0f  %-22s -> %.2f Hz (%.1f cents)  %s\n", sr, "amount=0 passthrough", f, cents, ok ? "OK" : "FAIL");
        fails += ok ? 0 : 1;
    }
    std::printf (fails ? "\n%d FALLOS\n" : "\nTODO OK\n", fails);
    return fails ? 1 : 0;
}
