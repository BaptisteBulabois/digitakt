#include "PatternChain.h"

#include <algorithm>
#include <cmath>

namespace takt
{
namespace
{
double safeTempo(double value, bool inherit) noexcept
{
    if (!std::isfinite(value)) return inherit ? 0.0 : 120.0;
    if (inherit && value <= 0.0) return 0.0;
    return std::clamp(value, 30.0, 300.0);
}
float safeSwing(float value, bool inherit) noexcept
{
    if (!std::isfinite(value)) return inherit ? -1.0f : 0.0f;
    if (inherit && value < 0.0f) return -1.0f;
    return std::clamp(value, 0.0f, 1.0f);
}
}

PatternAddress PatternAddress::fromBankSlot(int bank, int slot) noexcept
{
    return { std::clamp(bank, 0, 7) * 16 + std::clamp(slot, 1, 16) - 1 };
}
int PatternAddress::bank() const noexcept { return std::clamp(index, 0, 127) / 16; }
int PatternAddress::slot() const noexcept { return std::clamp(index, 0, 127) % 16 + 1; }

int PatternChain::boundedPattern(int index) noexcept { return std::clamp(index, 0, patternSlots - 1); }
PatternSettings PatternChain::sanitized(PatternSettings settings) noexcept
{
    settings.length = std::clamp(settings.length, 1, 128);
    settings.tempo = safeTempo(settings.tempo, false);
    settings.swing = safeSwing(settings.swing, false);
    return settings;
}
SongRow PatternChain::sanitized(SongRow row) noexcept
{
    row.pattern.index = boundedPattern(row.pattern.index);
    row.repeats = std::clamp(row.repeats, 1, 64);
    row.length = row.length <= 0 ? 0 : std::clamp(row.length, 2, 1024);
    row.tempo = safeTempo(row.tempo, true);
    row.swing = safeSwing(row.swing, true);
    return row;
}
void PatternChain::setPatternSettings(PatternAddress pattern, PatternSettings settings) noexcept
{
    patterns_[static_cast<std::size_t>(boundedPattern(pattern.index))] = sanitized(settings);
}
PatternSettings PatternChain::patternSettings(PatternAddress pattern) const noexcept
{
    return patterns_[static_cast<std::size_t>(boundedPattern(pattern.index))];
}
SongRow PatternChain::defaultRow(PatternAddress pattern) const noexcept
{
    SongRow result;
    result.pattern.index = boundedPattern(pattern.index);
    result.muteMask = patternSettings(pattern).muteMask;
    return result;
}
void PatternChain::setSong(int slot, const Song& value) noexcept
{
    auto& destination = songs_[static_cast<std::size_t>(std::clamp(slot, 0, songSlots - 1))];
    destination.rowCount = std::clamp(value.rowCount, 0, songRowCapacity);
    destination.endLoop = value.endLoop;
    destination.tempo = safeTempo(value.tempo, true);
    for (int row = 0; row < songRowCapacity; ++row)
        destination.rows[static_cast<std::size_t>(row)] = sanitized(value.rows[static_cast<std::size_t>(row)]);
    if (mode_ == Mode::Song && songSlot_ == std::clamp(slot, 0, songSlots - 1))
    {
        row_ = std::clamp(row_, 0, std::max(0, destination.rowCount - 1));
        queuedRow_ = queuedRow_ >= destination.rowCount ? -1 : queuedRow_;
    }
}
const Song& PatternChain::song(int slot) const noexcept
{
    return songs_[static_cast<std::size_t>(std::clamp(slot, 0, songSlots - 1))];
}
PatternAddress PatternChain::chainPattern(int position) const noexcept
{
    if (chainCount_ == 0) return {};
    return chain_[static_cast<std::size_t>(std::clamp(position, 0, chainCount_ - 1))];
}
int PatternChain::playbackLength() const noexcept
{
    if (mode_ == Mode::Song && song(songSlot_).rowCount > 0)
    {
        const auto& active = song(songSlot_).rows[static_cast<std::size_t>(row_)];
        if (active.length > 0) return active.length;
    }
    return patternSettings({ currentPattern_ }).length;
}
PatternChain::Transition PatternChain::current(bool hostTempo) const noexcept
{
    Transition result;
    result.pattern = { currentPattern_ };
    result.row = currentRow();
    result.length = playbackLength();
    const auto settings = patternSettings(result.pattern);
    result.muteMask = settings.muteMask;
    result.tempo = settings.tempo;
    result.swing = settings.swing;
    result.applyTempo = !hostTempo;
    if (mode_ == Mode::Song && song(songSlot_).rowCount > 0)
    {
        const auto& active = song(songSlot_).rows[static_cast<std::size_t>(row_)];
        result.muteMask = active.muteMask;
        if (active.tempo > 0.0) result.tempo = active.tempo;
        if (active.swing >= 0.0f) result.swing = active.swing;
        if (song(songSlot_).tempo > 0.0) result.tempo = song(songSlot_).tempo;
    }
    return result;
}
PatternChain::Transition PatternChain::activate(Mode mode, int position, int row, bool forceKit) noexcept
{
    const auto previousMode = mode_;
    const int previousPattern = currentPattern_, previousRow = row_, previousSong = songSlot_;
    mode_ = mode;
    if (mode == Mode::Pattern) currentPattern_ = boundedPattern(position);
    else if (mode == Mode::Chain)
    {
        chainPosition_ = std::clamp(position, 0, std::max(0, chainCount_ - 1));
        currentPattern_ = chainPattern(chainPosition_).index;
    }
    else
    {
        songSlot_ = std::clamp(position, 0, songSlots - 1);
        row_ = std::clamp(row, 0, std::max(0, song(songSlot_).rowCount - 1));
        if (song(songSlot_).rowCount > 0)
            currentPattern_ = song(songSlot_).rows[static_cast<std::size_t>(row_)].pattern.index;
        else playing_ = false;
    }
    repeat_ = rowStep_ = patternStep_ = 0;
    pending_ = false;
    queuedRow_ = -1;
    auto result = current();
    result.changed = true;
    result.patternChanged = previousPattern != currentPattern_;
    result.rowChanged = mode == Mode::Song
        && (previousMode != mode || previousRow != row_ || previousSong != songSlot_);
    result.applyKit = (result.patternChanged || forceKit) && !performKit_;
    result.stopped = mode == Mode::Song && song(songSlot_).rowCount == 0;
    return result;
}
PatternChain::Transition PatternChain::selectPattern(PatternAddress pattern) noexcept
{
    chainCount_ = 0;
    queuedRow_ = -1;
    loopRow_ = false;
    if (!playing_) return activate(Mode::Pattern, boundedPattern(pattern.index), 0, true);
    pending_ = true;
    pendingMode_ = Mode::Pattern;
    pendingPosition_ = boundedPattern(pattern.index);
    pendingRow_ = 0;
    return current();
}
PatternChain::Transition PatternChain::setChain(const PatternAddress* values, int count) noexcept
{
    // Null/empty chain cancels chain mode without changing the current pattern.
    if (values == nullptr || count <= 0) return selectPattern({ currentPattern_ });
    chainCount_ = std::clamp(count, 1, patternChainCapacity);
    for (int index = 0; index < chainCount_; ++index)
        chain_[static_cast<std::size_t>(index)] = { boundedPattern(values[index].index) };
    queuedRow_ = -1;
    loopRow_ = false;
    if (!playing_) return activate(Mode::Chain, 0, 0, true);
    pending_ = true;
    pendingMode_ = Mode::Chain;
    pendingPosition_ = pendingRow_ = 0;
    return current();
}
PatternChain::Transition PatternChain::selectSong(int slot, int firstRow) noexcept
{
    chainCount_ = 0;
    loopRow_ = false;
    queuedRow_ = -1;
    slot = std::clamp(slot, 0, songSlots - 1);
    firstRow = std::clamp(firstRow, 0, std::max(0, song(slot).rowCount - 1));
    if (!playing_) return activate(Mode::Song, slot, firstRow, true);
    // Explicit plugin adaptation: entering a different song during playback
    // uses a pattern boundary, matching normal pattern cueing.
    pending_ = true;
    pendingMode_ = Mode::Song;
    pendingPosition_ = slot;
    pendingRow_ = firstRow;
    return current();
}
bool PatternChain::queueSongRow(int row) noexcept
{
    if (mode_ != Mode::Song || row < 0 || row >= song(songSlot_).rowCount) return false;
    if (!playing_) activate(Mode::Song, songSlot_, row, true);
    else queuedRow_ = row;
    return true;
}
PatternAddress PatternChain::queuedPattern() const noexcept
{
    if (!pending_)
    {
        if (queuedRow_ >= 0 && mode_ == Mode::Song)
            return song(songSlot_).rows[static_cast<std::size_t>(queuedRow_)].pattern;
        return { currentPattern_ };
    }
    if (pendingMode_ == Mode::Chain) return chainPattern(pendingPosition_);
    if (pendingMode_ == Mode::Song && song(pendingPosition_).rowCount > 0)
        return song(pendingPosition_).rows[static_cast<std::size_t>(pendingRow_)].pattern;
    return { pendingMode_ == Mode::Pattern ? pendingPosition_ : currentPattern_ };
}
PatternChain::Transition PatternChain::play() noexcept
{
    auto result = current();
    if (mode_ == Mode::Song && song(songSlot_).rowCount == 0)
    {
        result.stopped = true;
        return result;
    }
    result.changed = !playing_;
    playing_ = true;
    return result;
}
PatternChain::Transition PatternChain::stop(bool rewind) noexcept
{
    playing_ = false;
    Transition result;
    if (rewind)
    {
        pending_ = false;
        queuedRow_ = -1;
        if (mode_ == Mode::Song) result = activate(mode_, songSlot_, 0);
        else if (mode_ == Mode::Chain && chainCount_ > 0) result = activate(mode_, 0, 0);
        else if (mode_ == Mode::Chain) result = activate(Mode::Pattern, currentPattern_, 0);
        else result = activate(mode_, currentPattern_, 0);
    }
    else result = current();
    result.changed = true;
    result.stopped = true;
    return result;
}
PatternChain::Transition PatternChain::advanceStep(bool hostTempo) noexcept
{
    auto result = current(hostTempo);
    if (!playing_) return result;
    if (mode_ == Mode::Song && song(songSlot_).rowCount == 0)
    {
        playing_ = false;
        result.changed = result.stopped = true;
        return result;
    }
    ++rowStep_;
    ++patternStep_;
    const bool patternBoundary = patternStep_ >= patternSettings({ currentPattern_ }).length;
    if (patternBoundary) patternStep_ = 0;
    if (pending_ && patternBoundary)
        result = activate(pendingMode_, pendingPosition_, pendingRow_, true);
    else if (pending_)
    {
        // A selection that exits the current arrangement finishes the current
        // pattern even if a shorter song row ends first. Do not lose the cue
        // through an automatic row transition/reset before that boundary.
        result = current(hostTempo);
    }
    else if (rowStep_ >= playbackLength())
    {
        if (mode_ == Mode::Pattern)
        {
            rowStep_ = 0;
            result = current(hostTempo);
            result.changed = true;
        }
        else if (mode_ == Mode::Chain)
            result = activate(Mode::Chain, (chainPosition_ + 1) % chainCount_, 0);
        else if (queuedRow_ >= 0)
            result = activate(Mode::Song, songSlot_, queuedRow_);
        else
        {
            const auto& activeSong = song(songSlot_);
            const auto& activeRow = activeSong.rows[static_cast<std::size_t>(row_)];
            if (loopRow_ || repeat_ + 1 < activeRow.repeats)
            {
                rowStep_ = patternStep_ = 0;
                repeat_ = std::min(repeat_ + 1, activeRow.repeats - 1);
                result = current(hostTempo);
                result.changed = true;
            }
            else if (row_ + 1 < activeSong.rowCount)
                result = activate(Mode::Song, songSlot_, row_ + 1);
            else if (activeSong.endLoop)
                result = activate(Mode::Song, songSlot_, 0);
            else
            {
                playing_ = false;
                rowStep_ = patternStep_ = 0;
                result = current(hostTempo);
                result.changed = result.stopped = true;
            }
        }
    }
    else result = current(hostTempo);
    result.applyTempo = !hostTempo;
    return result;
}
}
