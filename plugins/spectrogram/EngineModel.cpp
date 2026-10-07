// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor's model, answered by the receiver. EngineModel.h says how.
 */
#include "EngineModel.h"

#include "MachineSettings.h"
#include "Spectrogram.h"
#include "SpectrogramProcessor.h"

#include <algorithm>

namespace ni::spectrogram
{

namespace
{
/* The source list's bound: sixteen lines of "<slot>:<live>:<rate>:<label>",
 * a label at most 31 bytes. */
constexpr int listingBytes = 1024;

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

/* "<slot>:<live>:<rate>:<label>" a line. A label may hold a colon: it is
 * everything after the third. */
std::vector<Source> parseSources (const unsigned char* text, int length)
{
    std::vector<Source> out;
    const juce::String all (juce::CharPointer_UTF8 (reinterpret_cast<const char*> (text)), (size_t) length);
    for (const auto& line : juce::StringArray::fromLines (all))
    {
        const int a = line.indexOfChar (':');
        const int b = a < 0 ? -1 : line.indexOfChar (a + 1, ':');
        const int c = b < 0 ? -1 : line.indexOfChar (b + 1, ':');
        if (c < 0)
            continue;
        Source s;
        s.slot = line.substring (0, a).getIntValue();
        s.live = line.substring (a + 1, b).getIntValue() != 0;
        s.sampleRate = line.substring (b + 1, c).getIntValue();
        s.label = line.substring (c + 1);
        if (s.slot > 0)
            out.push_back (std::move (s));
    }
    return out;
}
} // namespace

static_assert (SPECTRO_BANDS == ni::ui::Spectrogram::defaultBands,
               "the picture holds as many bands as the analyzer measures");
static_assert (Model::maxColumns <= SPECTRO_COLUMN_CAPACITY,
               "one take is never more than the analyzer's rings hold");

EngineModel::EngineModel (Processor& p) : processor (p), scratch ((size_t) SPECTRO_BANDS), listing ((size_t) listingBytes)
{
    hz.reserve ((size_t) SPECTRO_BANDS);
    readAxis();
}

EngineModel::~EngineModel() = default;

/* Never asked: numParameters() is 0. The host's Bypass is the only one. */
juce::RangedAudioParameter& EngineModel::parameter (int)
{
    jassertfalse;
    return processor.bypassParameter();
}

int EngineModel::takeRings (float* strengths, int capacity)
{
    return processor.ground().takeRings (strengths, capacity);
}

bool EngineModel::motion() const
{
    return ni::MachineSettings::motion ("spectrogram");
}

void EngineModel::setMotion (bool on)
{
    ni::MachineSettings::setMotion ("spectrogram", on);
}

/*
 * ONE TICK'S PICTURE, into the editor's buffers: every channel drained in
 * step, the view summed, and the clash between the two channels the session
 * compares -- not between whatever is on screen -- when it is wanted. Nothing
 * while a receiver for a new rate is still to be built: its columns would be
 * measured on another axis.
 */
int EngineModel::takeColumns (std::uint8_t* levels, std::uint8_t* clash, int capacity, int& clashCount)
{
    clashCount = 0;
    auto* recv = processor.receiver();
    if (recv == nullptr || processor.rebuildPending() || levels == nullptr || capacity <= 0)
        return 0;
    const int bands = srecv_bands (recv);
    if (bands <= 0 || bands != (int) hz.size())
        return 0;
    const auto& look = processor.session().applied();
    const bool compare = look.clashOn && clash != nullptr;
    return srecv_frame (recv, look.view.data(), (int) look.view.size(), compare ? look.cmpA : -1,
                        compare ? look.cmpB : -1, levels, compare ? clash : nullptr,
                        juce::jmin (capacity, (int) maxColumns), &clashCount);
}

Transport EngineModel::transport() const
{
    return processor.transport();
}

const Session& EngineModel::session() const
{
    const auto revision = processor.session().revision();
    if (shownAny && revision == shownRevision)
        return shown;
    const auto f = processor.session().get();
    shown.rangeLo = f.rangeLo;
    shown.rangeHi = f.rangeHi;
    shown.view = f.view.empty() ? std::vector<int> { 0 } : f.view;
    shown.compareA = f.cmpA;
    shown.compareB = f.cmpB;
    shown.clash = f.clashOn;
    shown.clashFloorDb = f.clashFloorDb;
    shown.clashBalanceDb = f.clashBalanceDb;
    shown.listen.assign (f.sources.begin(), f.sources.end());
    shownRevision = revision;
    shownAny = true;
    return shown;
}

/* The zoom. The engine refuses an undrawable range and clamps the top to
 * Nyquist; the axis it makes of it is read when the range is applied. */
void EngineModel::setRange (float lo, float hi)
{
    processor.session().edit ([&] (spectro::state::Fields& f) {
        f.rangeLo = lo;
        f.rangeHi = hi;
    });
    processor.sessionEdited();
}

/* An empty view is the own channel: a spectrogram showing nothing is a
 * broken plugin, not a view. */
void EngineModel::setLook (const std::vector<int>& view, int compareA, int compareB, bool clashOn,
                           const std::vector<int>& listen)
{
    std::vector<unsigned int> slots;
    for (const int s : listen)
        if (s > 0)
            slots.push_back ((unsigned int) s);
    const std::vector<int> v = view.empty() ? std::vector<int> { 0 } : view;
    processor.session().edit ([&] (spectro::state::Fields& f) {
        f.view = v;
        f.cmpA = juce::jmax (0, compareA);
        f.cmpB = juce::jmax (0, compareB);
        f.clashOn = clashOn;
        f.sources = slots;
    });
    processor.sessionEdited();
}

void EngineModel::setClashCriteria (float floorDb, float balanceDb)
{
    processor.session().edit ([&] (spectro::state::Fields& f) {
        f.clashFloorDb = floorDb;
        f.clashBalanceDb = balanceDb;
    });
    processor.sessionEdited();
}

/* Into the scratch first, so a tick that finds the same axis allocates and
 * moves nothing. */
void EngineModel::readAxis()
{
    auto* recv = processor.receiver();
    const int n = recv == nullptr ? 0 : srecv_band_hz (recv, scratch.data(), (int) scratch.size());
    if (n == (int) hz.size() && std::equal (hz.begin(), hz.end(), scratch.begin()))
        return;
    hz.assign (scratch.begin(), scratch.begin() + juce::jmax (0, n));
}

/* Every bus that exists, sending or not: a muted Listen-In is still where the
 * user put it. */
void EngineModel::readSources()
{
    sourcesAt = nowMs();
    const int n = srecv_slots (listing.data(), (int) listing.size());
    if (n < 0 || n > (int) listing.size())
        return;
    auto fresh = parseSources (listing.data(), n);
    if (fresh != buses)
        buses = std::move (fresh);
}

void EngineModel::readSourcesIfDue (double now)
{
    if (now - sourcesAt >= sourcesEveryMs)
        readSources();
}

} // namespace ni::spectrogram
