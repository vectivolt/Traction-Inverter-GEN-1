// Dsp — the numerics behind the plots and the analysis page: a radix-2 FFT magnitude spectrum (Hann window),
// per-channel statistics, and trapezoidal energy integration that skips gaps (NaN) instead of bridging them.
#pragma once

#include <QVector>

namespace Dsp {

struct Stats {
    int n = 0; // finite samples
    double min = 0, max = 0, mean = 0, rms = 0, std = 0;
};

Stats stats(const QVector<double> &v);

// Single-sided amplitude spectrum of the last power-of-two samples of x (fs in Hz), Hann-windowed and amplitude
// corrected (a sine of amplitude A reads A at its bin). Returns false when fewer than 16 finite samples.
bool spectrum(const QVector<double> &x, double fs, QVector<double> &freq, QVector<double> &amp);

// Integral of y dt with t in ms (result in y·s); a NaN sample breaks the integration across its gap.
double integrate(const QVector<double> &tMs, const QVector<double> &y);
// The same over only the positive (or only the negative) part of y.
double integratePart(const QVector<double> &tMs, const QVector<double> &y, bool positive);

} // namespace Dsp
