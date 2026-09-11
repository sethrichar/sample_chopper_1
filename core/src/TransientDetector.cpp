#include "chopper/TransientDetector.h"
#include "chopper/Fft.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <cstdlib>

namespace chopper {
namespace {

size_t framesFromMs(float ms, double sr) { return size_t(std::max(0.0, ms * sr / 1000.0)); }

/// Short-time peak envelope (block size `blk`) over mono[from, to).
std::vector<float> peakEnvelope(const std::vector<float>& x, size_t from, size_t to, size_t blk)
{
    std::vector<float> env;
    for (size_t s = from; s + blk <= to; s += blk) {
        float m = 0; for (size_t i = 0; i < blk; ++i) m = std::max(m, std::fabs(x[s + i]));
        env.push_back(m);
    }
    return env;
}

} // namespace

std::vector<float> TransientDetector::detectionFunction(const std::vector<float>& mono, double sampleRate) const
{
    const int N = settings_.frameSize, H = settings_.hopSize;
    if (mono.size() < size_t(N)) return {};
    Fft fft(N);
    std::vector<float> window(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) window[size_t(i)] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * float(i) / float(N - 1)));

    // Log-spaced band filterbank (6 bands/octave). Raw per-bin flux under-weights kicks and bass,
    // whose energy sits in a handful of bins; per-band flux gives every octave a similar say.
    const int numBins = N / 2 + 1;
    const double binHz = sampleRate / N;
    std::vector<std::pair<int, int>> bands;   // [firstBin, lastBin]
    {
        const double fMin = 30.0, fMax = std::min(16000.0, sampleRate * 0.45);
        const double step = std::pow(2.0, 1.0 / 6.0);
        int lastHi = 0;
        for (double f = fMin; f < fMax; f *= step) {
            int lo = int(std::floor(f / std::sqrt(step) / binHz + 0.5));
            int hi = int(std::floor(f * std::sqrt(step) / binHz + 0.5)) - 1;
            lo = std::max(lo, lastHi + 1);          // no overlap, no gaps
            hi = std::max(hi, lo);
            if (hi >= numBins) break;
            bands.emplace_back(lo, hi);
            lastHi = hi;
        }
        if (bands.empty()) bands.emplace_back(1, numBins - 1);
    }

    // Pad N/2 at the start so frame n is centred on sample n*H.
    std::vector<float> padded(static_cast<size_t>(N / 2), 0.0f);
    padded.insert(padded.end(), mono.begin(), mono.end());
    padded.resize(padded.size() + size_t(N), 0.0f);

    const size_t numFrames = (padded.size() - size_t(N)) / size_t(H) + 1;
    std::vector<float> frame(static_cast<size_t>(N));
    std::vector<float> mags, cur(bands.size()), prev;
    std::vector<float> odf(numFrames, 0.0f);
    const float norm = 2.0f / float(N);   // full-scale sine -> ~1.0 at its bin (hann halves it)
    const float gamma = 100.0f;           // log compression: makes quiet hits count too

    for (size_t n = 0; n < numFrames; ++n) {
        const float* src = padded.data() + n * size_t(H);
        for (int i = 0; i < N; ++i) frame[size_t(i)] = src[i] * window[size_t(i)];
        fft.magnitudes(frame.data(), mags);
        for (size_t b = 0; b < bands.size(); ++b) {
            float e = 0; for (int k = bands[b].first; k <= bands[b].second; ++k) e = std::max(e, mags[size_t(k)]);
            cur[b] = std::log1p(gamma * e * norm);
        }
        if (!prev.empty()) {
            float flux = 0;
            for (size_t b = 0; b < cur.size(); ++b) flux += std::max(0.0f, cur[b] - prev[b]);
            odf[n] = flux;
        }
        prev = cur;
    }
    const float mx = *std::max_element(odf.begin(), odf.end());
    if (mx > 0) for (auto& v : odf) v /= mx;
    return odf;
}

std::vector<Onset> TransientDetector::detect(const AudioData& audio) const
{
    std::vector<Onset> result;
    if (audio.empty()) return result;
    const double sr = audio.sampleRate;
    const int H = settings_.hopSize, N = settings_.frameSize;
    const std::vector<float> mono = audio.mono();
    const std::vector<float> odf = detectionFunction(mono, sr);
    if (odf.empty()) return result;

    // ---- adaptive peak picking --------------------------------------------------------------
    const float sens  = std::clamp(settings_.sensitivity, 0.0f, 1.0f);
    const float delta = 0.02f + 0.45f * (1.0f - sens) * (1.0f - sens);   // absolute floor above local mean
    const int   wLocal = std::max(1, int(std::lround(0.20 * sr / H)));   // ±200 ms mean window
    const int   wPeak  = 3;                                              // must be max within ±3 hops

    // prefix sums for the sliding mean
    std::vector<double> cum(odf.size() + 1, 0.0);
    for (size_t i = 0; i < odf.size(); ++i) cum[i + 1] = cum[i] + odf[i];

    struct Cand { size_t frame; float strength; };
    std::vector<Cand> cands;
    for (size_t n = 1; n + 1 < odf.size(); ++n) {
        const float v = odf[n];
        if (v <= 0) continue;
        bool isMax = true;
        for (int d = -wPeak; d <= wPeak && isMax; ++d) {
            if (d == 0) continue;
            const long j = long(n) + d;
            if (j >= 0 && j < long(odf.size()) && odf[size_t(j)] > v) isMax = false;
        }
        if (!isMax) continue;
        const size_t lo = n > size_t(wLocal) ? n - size_t(wLocal) : 0;
        const size_t hi = std::min(odf.size(), n + size_t(wLocal) + 1);
        const float mean = float((cum[hi] - cum[lo]) / double(hi - lo));
        if (v > mean + delta) cands.push_back({ n, v });
    }

    // ---- enforce minimum spacing: strongest wins -------------------------------------------
    const size_t minDist = std::max<size_t>(1, framesFromMs(settings_.minSliceMs, sr));
    std::vector<Cand> byStrength = cands;
    std::stable_sort(byStrength.begin(), byStrength.end(), [](const Cand& a, const Cand& b) { return a.strength > b.strength; });
    std::vector<Cand> kept;
    for (const auto& c : byStrength) {
        bool ok = true;
        for (const auto& k : kept)
            if (size_t(std::llabs(static_cast<long long>(k.frame) - static_cast<long long>(c.frame))) * size_t(H) < minDist) { ok = false; break; }
        if (ok) kept.push_back(c);
    }
    std::sort(kept.begin(), kept.end(), [](const Cand& a, const Cand& b) { return a.frame < b.frame; });

    // ---- sample-accurate refinement ------------------------------------------------------
    // The ODF peak tells us roughly where the hit is (within a hop or two). Around it we look at a
    // fine peak envelope, find the steepest rise, and then walk back to where the envelope first
    // crossed 20% of the way from the pre-hit level to the attack peak. That is the cut point.
    const size_t total = mono.size();
    const size_t blk = 32;
    const size_t ctxBlocks = std::max<size_t>(1, framesFromMs(10.0f, sr) / blk);   // ±10 ms context
    const size_t preRoll = framesFromMs(settings_.preRollMs, sr);
    const size_t zcSearch = framesFromMs(1.5f, sr);   // short: a long search drifts on bass-heavy material

    for (const auto& c : kept) {
        if (c.frame <= 1) { result.push_back({ 0, c.strength }); continue; }   // file starts mid-hit
        const long centre = long(c.frame) * H;
        const size_t from = size_t(std::max(0L, centre - N / 2 - H));
        const size_t to   = std::min(total, size_t(centre + N / 2 + H));
        size_t onset = size_t(std::max(0L, centre));
        if (to > from + 2 * blk) {
            const auto env = peakEnvelope(mono, from, to, blk);
            if (env.size() >= 3) {
                // steepest *relative* rise (log ratio): a jump from -40 dB to -6 dB beats noise wobble
                size_t kStar = 0; float best = -1.0f;
                for (size_t k = 0; k + 1 < env.size(); ++k) {
                    const float d = std::log(env[k + 1] + 1e-4f) - std::log(env[k] + 1e-4f);
                    if (d > best) { best = d; kStar = k; }
                }
                const size_t kHi = std::min(env.size() - 1, kStar + ctxBlocks);
                float pre = env[kStar], peak = env[kStar];
                for (size_t k = 0; k <= kStar; ++k) pre = std::min(pre, env[k]);
                for (size_t k = kStar; k <= kHi; ++k) peak = std::max(peak, env[k]);
                const float thr = pre + 0.2f * (peak - pre);
                size_t k = std::min(kStar + 1, env.size() - 1);
                while (k > 0 && env[k - 1] >= thr) --k;
                onset = from + k * blk;
            }
        }
        onset = onset > preRoll ? onset - preRoll : 0;
        if (settings_.snapToZeroCrossing && onset > 0) {
            const size_t lo = onset > zcSearch ? onset - zcSearch : 0;
            for (size_t i = onset; i > lo; --i) {
                if ((mono[i] >= 0) != (mono[i - 1] >= 0)) { onset = i; break; }
            }
        }
        if (onset >= total) continue;
        result.push_back({ onset, c.strength });
    }

    // refinement can reorder/merge neighbours; tidy up
    std::sort(result.begin(), result.end(), [](const Onset& a, const Onset& b) { return a.sample < b.sample; });
    std::vector<Onset> tidy;
    for (const auto& o : result) {
        if (!tidy.empty() && o.sample - tidy.back().sample < minDist) {
            if (o.strength > tidy.back().strength) tidy.back() = o;
        } else tidy.push_back(o);
    }
    return tidy;
}

} // namespace chopper
