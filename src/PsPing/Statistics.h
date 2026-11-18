// PsPing statistics and histogram output: the five-field stats accumulator
// (sub_1400044E0) and the bucket builder and printer (sub_140004B30 /
// sub_140004C90).
#pragma once

#include "PingCommon.h"

namespace psping
{
    class Stats
    {
      public:
        void Add(bool success, float ms)
        {
            ++sent_;
            if (success)
            {
                ++received_;
                sum_ += ms;
                min_ = std::min(min_, ms);
                max_ = std::max(max_, ms);
            }
        }

        void Print() const
        {
            const int lost = sent_ - received_;
            const int lossPct = sent_ > 0 ? lost * 100 / sent_ : 0;
            std::printf("  Sent = %d, Received = %d, Lost = %d (%d%% loss),\n", sent_, received_, lost, lossPct);
            const float avg = received_ != 0 ? sum_ / static_cast<float>(received_) : 0.0f;
            const float shownMin = received_ != 0 ? min_ : 0.0f;
            std::printf("  Minimum = %.2fms, Maximum = %.2fms, Average = %.2fms\n", static_cast<double>(shownMin),
                        static_cast<double>(max_), static_cast<double>(avg));
        }

      private:
        int sent_ = 0;
        int received_ = 0;
        float min_ = (std::numeric_limits<float>::max)();
        float max_ = 0.0f;
        float sum_ = 0.0f;
    };

    class Histogram
    {
      public:
        void Store(float ms)
        {
            samples_.push_back(ms);
        }

        // Print with the supplied column header. Mirrors sub_140004C90: fewer
        // than two samples prints the "not enough data" line, otherwise the
        // header and one row per bucket.
        void Print(const Options& options) const
        {
            if (samples_.size() < 2)
            {
                std::printf("Not enough data for histogram. Trying increasing the request size or\n"
                            "iteration count.\n\n");
                return;
            }
            std::printf("\nLatency\tCount\n");
            if (!options.customBounds.empty())
            {
                PrintCustom(options.customBounds);
            }
            else
            {
                PrintAuto(options.buckets);
            }
        }

      private:
        // sub_140004B30: width = (max - min) / (buckets - 1), bound[i] = min +
        // i*width, and each sample lands in bucket (int)((sample - min) / width).
        void PrintAuto(int buckets) const
        {
            if (buckets < 2)
            {
                buckets = 2;
            }
            float lo = samples_[0];
            float hi = samples_[0];
            for (const float s : samples_)
            {
                lo = std::min(lo, s);
                hi = std::max(hi, s);
            }
            const float width = (hi - lo) / static_cast<float>(buckets - 1);
            std::vector<unsigned> counts(static_cast<size_t>(buckets), 0);
            for (const float s : samples_)
            {
                int idx = width > 0.0f ? static_cast<int>((s - lo) / width) : 0;
                idx = std::clamp(idx, 0, buckets - 1);
                ++counts[static_cast<size_t>(idx)];
            }
            for (int i = 0; i < buckets; ++i)
            {
                const float bound = lo + static_cast<float>(i) * width;
                std::printf("%.2f\t%d\n", static_cast<double>(bound), counts[static_cast<size_t>(i)]);
            }
        }

        // Explicit upper bounds plus an overflow bucket printed as "#INF",
        // modelled from the FLT_MAX branch of sub_140004C90.
        void PrintCustom(const std::vector<float>& boundsIn) const
        {
            std::vector<float> bounds = boundsIn;
            std::sort(bounds.begin(), bounds.end());
            std::vector<unsigned> counts(bounds.size() + 1, 0);
            for (const float s : samples_)
            {
                size_t idx = bounds.size();
                for (size_t i = 0; i < bounds.size(); ++i)
                {
                    if (s <= bounds[i])
                    {
                        idx = i;
                        break;
                    }
                }
                ++counts[idx];
            }
            for (size_t i = 0; i < bounds.size(); ++i)
            {
                std::printf("%.2f\t%d\n", static_cast<double>(bounds[i]), counts[i]);
            }
            std::printf("#INF\t%d\n", counts[bounds.size()]);
        }

        std::vector<float> samples_;
    };
} // namespace psping
