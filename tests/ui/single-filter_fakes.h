// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A single filter's model with no audio behind it, for the editor's tests:
 * the product's parameters as its table declares them, read from the
 * engine's own C ABI (sf_param_*: pure, any thread), and the picture the
 * engine's shell designs from them (sf_shell_read) with no block ever run --
 * so the window is tested on the curves and handles the plugin draws, and
 * nothing of the audio thread. The real processor is
 * tests/single-filter/sf_processor.cpp's.
 */
#pragma once

#include "Model.h"

#include "fakes.h"
#include "sf_shell.h"

#include <cmath>

namespace ni::sf::test
{

class FakeModel final : public Model
{
public:
    explicit FakeModel (int s) : product (s), shell (sf_shell_create ((uint32_t) s, 48000.0)), raw (std::make_unique<SfPicture>())
    {
        const int n = sf_param_count ((uint32_t) s);
        for (int i = 0; i < n; ++i)
        {
            NiParamInfo info {};
            sf_param_info ((uint32_t) s, i, &info);
            const auto text = [s, i] (int which, double v, int option) {
                char b[64] {};
                if (which == 0)
                    sf_param_key ((uint32_t) s, i, b, sizeof b);
                else if (which == 1)
                    sf_param_name ((uint32_t) s, i, b, sizeof b);
                else if (which == 2)
                    sf_param_label ((uint32_t) s, i, option, b, sizeof b);
                else
                    sf_param_text ((uint32_t) s, i, v, b, sizeof b);
                return juce::String::fromUTF8 (b);
            };
            if (info.kind == NiParamKind_Choice)
            {
                juce::StringArray labels;
                for (int o = 0; o <= (int) info.max; ++o)
                    labels.add (text (2, 0.0, o));
                params.addChoice (text (0, 0.0, 0), text (1, 0.0, 0), labels, (int) info.default_value);
            }
            else
            {
                const juce::NormalisableRange<float> range (
                    (float) info.min, (float) info.max,
                    [s, i] (float, float, float t) { return (float) sf_param_denormalise ((uint32_t) s, i, (double) t); },
                    [s, i] (float, float, float v) { return (float) sf_param_normalise ((uint32_t) s, i, (double) v); },
                    [s, i] (float, float, float v) { return (float) sf_param_snap ((uint32_t) s, i, (double) v); });
                params.addFloat (text (0, 0.0, 0), text (1, 0.0, 0), range, (float) info.default_value,
                                 [text] (float v) { return text (3, (double) v, 0); },
                                 [s, i] (const juce::String& t) {
                                     double v = 0.0;
                                     return sf_param_parse ((uint32_t) s, i, t.toRawUTF8(), &v) ? (float) v : 0.0f;
                                 });
            }
        }
        count = n;
        /* The shell's Bypass, after the table (ni::bypassSpec). */
        params.addBool ("bypass", "Bypass", false);
    }

    ~FakeModel() override { sf_shell_destroy (shell); }

    ni::ui::test::FakeEditorModel common;
    ni::ui::test::FakeParameters& params = common.params;

    int shape() const override { return product; }
    juce::String productName() const override
    {
        static const char* const names[] = { "", "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Band Pass", "Notch", "All Pass", "Tilt" };
        return names[product];
    }
    int indexOf (int f) const override { return sf_field_index ((uint32_t) product, (uint32_t) f); }
    int numParameters() const override { return count; }
    juce::RangedAudioParameter& parameter (int i) override { return common.parameter (i); }
    juce::RangedAudioParameter& bypassParameter() override { return common.parameter (count); }

    bool bipolar (int i) const override
    {
        NiParamInfo info {};
        return sf_param_info ((uint32_t) product, i, &info) && info.bipolar;
    }

    double plain (int i) const
    {
        auto& p = const_cast<FakeModel*> (this)->parameter (i);
        return (double) p.convertFrom0to1 (p.getValue());
    }

    const Picture& picture() override
    {
        double v[SF_PARAM_MAX] {};
        for (int i = 0; i < count; ++i)
            v[i] = plain (i);
        if (sf_shell_read (shell, v, (size_t) count, raw.get()))
        {
            const auto& p = *raw;
            shown.sampleRate = p.sample_rate;
            shown.clamped = p.clamped;
            shown.handleHz = p.handle_hz;
            shown.handleDb = p.handle_db;
            shown.edgeHz = { p.edge_hz[0], p.edge_hz[1] };
            std::copy (std::begin (p.hz), std::end (p.hz), shown.hz.begin());
            std::copy (std::begin (p.curve_db), std::end (p.curve_db), shown.curve.begin());
            std::copy (std::begin (p.total_db), std::end (p.total_db), shown.total.begin());
            std::copy (std::begin (p.phase_deg), std::end (p.phase_deg), shown.phase.begin());
            ++shown.serial;
        }
        return shown;
    }

    unsigned uses (int order, int family) const override { return sf_uses ((uint32_t) product, order, family); }
    int orderPlayed (int family, int order) const override { return sf_order_played ((uint32_t) product, family, order); }
    double qForEdge (double freq, double edgeHz) const override { return sf_q_for_edge (freq, edgeHz); }

private:
    const int product;
    int count = 0;
    SfShell* shell;
    std::unique_ptr<SfPicture> raw;
    Picture shown;
};

} // namespace ni::sf::test
