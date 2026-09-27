#include "Dsp.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <vector>

namespace Dsp {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

Stats stats(const QVector<double> &v)
{
    Stats s;
    double sum = 0, sum2 = 0;
    for (double x : v) {
        if (!std::isfinite(x)) {
            continue;
        }
        if (s.n == 0) {
            s.min = s.max = x;
        }
        s.min = std::min(s.min, x);
        s.max = std::max(s.max, x);
        sum += x;
        sum2 += x * x;
        s.n++;
    }
    if (s.n > 0) {
        s.mean = sum / s.n;
        s.rms = std::sqrt(sum2 / s.n);
        s.std = std::sqrt(std::max(0.0, sum2 / s.n - s.mean * s.mean));
    }
    return s;
}

bool spectrum(const QVector<double> &x, double fs, QVector<double> &freq, QVector<double> &amp)
{
    freq.clear();
    amp.clear();
    QVector<double> y;
    for (double v : x) {
        if (std::isfinite(v)) {
            y.push_back(v);
        }
    }
    if (y.size() < 16 || !(fs > 0)) {
        return false;
    }
    int n = 1;
    while (n * 2 <= y.size()) {
        n *= 2;
    }
    y = y.mid(y.size() - n);
    const double mean = std::accumulate(y.begin(), y.end(), 0.0) / n; // remove DC: its leakage hides low tones
    std::vector<std::complex<double>> a(static_cast<size_t>(n));
    double wsum = 0;
    for (int i = 0; i < n; i++) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (n - 1)));
        wsum += w;
        a[static_cast<size_t>(i)] = std::complex<double>((y[i] - mean) * w, 0.0);
    }
    // iterative radix-2 Cooley-Tukey
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[static_cast<size_t>(i)], a[static_cast<size_t>(j)]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; k++) {
                const std::complex<double> u = a[static_cast<size_t>(i + k)];
                const std::complex<double> v = a[static_cast<size_t>(i + k + len / 2)] * w;
                a[static_cast<size_t>(i + k)] = u + v;
                a[static_cast<size_t>(i + k + len / 2)] = u - v;
                w *= wl;
            }
        }
    }
    for (int k = 0; k <= n / 2; k++) {
        freq.push_back(k * fs / n);
        const double m = std::abs(a[static_cast<size_t>(k)]) / wsum; // coherent gain of the window
        amp.push_back((k == 0 || k == n / 2) ? m : 2.0 * m);
    }
    return true;
}

double integratePart(const QVector<double> &tMs, const QVector<double> &y, bool positive)
{
    double e = 0;
    for (int i = 1; i < tMs.size() && i < y.size(); i++) {
        const double a = y[i - 1], b = y[i];
        if (!std::isfinite(a) || !std::isfinite(b)) {
            continue;
        }
        const double pa = positive ? std::max(a, 0.0) : std::min(a, 0.0);
        const double pb = positive ? std::max(b, 0.0) : std::min(b, 0.0);
        e += 0.5 * (pa + pb) * (tMs[i] - tMs[i - 1]) * 1e-3;
    }
    return e;
}

double integrate(const QVector<double> &tMs, const QVector<double> &y)
{
    return integratePart(tMs, y, true) + integratePart(tMs, y, false);
}

} // namespace Dsp
