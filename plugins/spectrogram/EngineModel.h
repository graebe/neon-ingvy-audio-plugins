// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram's editor model (editor/Model.h), answered by the receiver.
 *
 * THE COLUMNS GO STRAIGHT THROUGH. takeColumns hands the editor's own buffers
 * to srecv_frame, which drains every channel in step, sums the view's
 * channels in power and measures the clash between the two compared ones
 * into them: one copy, from the engine's rings into the bytes the picture is
 * written from, with nothing allocated per column or per tick.
 *
 * THE REST ARE SNAPSHOTS, refreshed where they change and read by reference
 * until then: the band centres when a range is applied or a receiver built
 * (the engine derives them from the range asked for, and drops columns
 * measured on any other, so the two never disagree), the buses that exist
 * twice a second while a window is open (a Listen-In appears at human speed;
 * probing creates nothing), and the session whenever its revision moves --
 * which includes an edit the receiver has not adopted yet, so a control never
 * shows the old choice for a frame. The transport is the audio thread's last
 * publish, read as it is asked for.
 *
 * COMMANDS GO THROUGH THE SESSION, as a host's load does, and are applied at
 * once -- this is the message thread -- each marking the set unsaved.
 *
 * Owned by the processor. Message thread, every call.
 */
#pragma once

#include "Model.h"

#include <cstdint>
#include <vector>

namespace ni::spectrogram
{

class Processor;

class EngineModel final : public Model
{
public:
    /* Half a second between two looks at which buses exist. */
    static constexpr double sourcesEveryMs = 500.0;

    explicit EngineModel (Processor&);
    ~EngineModel() override;

    /* ---- EditorModel */
    int numParameters() const override { return 0; }
    juce::RangedAudioParameter& parameter (int index) override;
    int takeRings (float* strengths, int capacity) override;
    bool motion() const override;
    void setMotion (bool on) override;

    /* ---- Model */
    int takeColumns (std::uint8_t* levels, std::uint8_t* clash, int capacity, int& clashCount) override;
    const std::vector<float>& bandCentres() const override { return hz; }
    Transport transport() const override;
    const std::vector<Source>& sources() const override { return buses; }
    const Session& session() const override;

    void setRange (float lo, float hi) override;
    void setLook (const std::vector<int>& view, int compareA, int compareB, bool clash,
                  const std::vector<int>& listen) override;
    void setClashCriteria (float floorDb, float balanceDb) override;

    /* ---- what the processor tells it */

    /* The receiver's axis, again: a range applied, a receiver built. */
    void readAxis();
    /* The buses that exist, at once (a window opening), or when due. */
    void readSources();
    void readSourcesIfDue (double nowMs);

private:
    Processor& processor;

    std::vector<float> hz, scratch;
    std::vector<Source> buses;
    double sourcesAt = -sourcesEveryMs;
    std::vector<unsigned char> listing;

    mutable Session shown;
    mutable std::uint32_t shownRevision = 0;
    mutable bool shownAny = false;

    JUCE_DECLARE_NON_COPYABLE (EngineModel)
};

} // namespace ni::spectrogram
