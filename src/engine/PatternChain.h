#pragma once

#include <array>
#include <cstdint>

namespace takt
{
// Digitakt II OS 1.17 manual sections 10.1, 10.9 and 10.10. This model
// owns arrangement metadata, not sample buffers or audio-thread parameter state.
constexpr int patternSlots = 128;
constexpr int patternChainCapacity = 64;
constexpr int songSlots = 16; // The manual specifies 16 songs, not 99.
constexpr int songRowCapacity = 99;

struct PatternAddress
{
    int index = 0;
    static PatternAddress fromBankSlot(int bank, int slot) noexcept;
    int bank() const noexcept;
    int slot() const noexcept; // Display number 1..16; bank is 0..7 (A..H).
};

struct PatternSettings
{
    int length = 16;
    double tempo = 120.0;
    float swing = 0.0f; // Plugin-normalized swing, independent of hardware display.
    std::uint16_t muteMask = 0;
};

struct SongRow
{
    PatternAddress pattern;
    int repeats = 1; // 1..64: explicit implementation bound; manual omits range.
    int length = 0; // 0 follows pattern length; otherwise 2..1024 steps.
    double tempo = 0.0; // 0 follows pattern; song tempo overrides both.
    float swing = -1.0f; // -1 follows pattern; otherwise normalized 0..1.
    std::uint16_t muteMask = 0;
};

struct Song
{
    std::array<SongRow, songRowCapacity> rows{};
    int rowCount = 0;
    bool endLoop = true;
    double tempo = 0.0; // 0 means individual row/pattern tempos.
};

class PatternChain
{
public:
    enum class Mode { Pattern, Chain, Song };
    struct Transition
    {
        bool changed = false;
        bool patternChanged = false;
        bool rowChanged = false;
        bool applyKit = false; // Processor must keep its current kit when false.
        bool stopped = false; // Stop the internal arrangement, never the DAW.
        PatternAddress pattern;
        int row = -1;
        int length = 16;
        std::uint16_t muteMask = 0;
        double tempo = 120.0;
        float swing = 0.0f;
        bool applyTempo = true;
    };

    void setPatternSettings(PatternAddress pattern, PatternSettings settings) noexcept;
    PatternSettings patternSettings(PatternAddress pattern) const noexcept;
    SongRow defaultRow(PatternAddress pattern) const noexcept;
    void setSong(int slot, const Song& song) noexcept;
    const Song& song(int slot) const noexcept;

    // Choosing a normal pattern discards the transient chain and exits Song mode.
    // While playing, activation occurs at the current pattern's next boundary.
    Transition selectPattern(PatternAddress pattern) noexcept;
    // Chains are transient. Storage/persistence belongs to the processor; the
    // hardware discards chains on pattern/song selection and power-off.
    Transition setChain(const PatternAddress* patterns, int count) noexcept;
    int chainSize() const noexcept { return chainCount_; }
    PatternAddress chainPattern(int position) const noexcept;
    Transition selectSong(int slot, int firstRow = 0) noexcept;
    bool queueSongRow(int row) noexcept; // Jump at the next row boundary.
    void setRowLoop(bool enabled) noexcept { loopRow_ = enabled; }
    bool rowLoop() const noexcept { return loopRow_; }
    void setPerformKit(bool enabled) noexcept { performKit_ = enabled; }
    bool performKit() const noexcept { return performKit_; }

    Transition play() noexcept; // Resume current row/position after one STOP.
    Transition stop(bool rewind = false) noexcept; // Double STOP: rewind=true.
    // Called exactly once per sequencer step. No allocations, locks, or sample
    // destruction. Caller owns synchronization and must split blocks at steps.
    // DAW adaptation: tempo remains informative when hostTempo=true; the model
    // never asks Ableton to change tempo or stop its global transport.
    Transition advanceStep(bool hostTempo = false) noexcept;
    Transition current(bool hostTempo = false) const noexcept;

    Mode mode() const noexcept { return mode_; }
    bool playing() const noexcept { return playing_; }
    PatternAddress currentPattern() const noexcept { return { currentPattern_ }; }
    int currentSong() const noexcept { return mode_ == Mode::Song ? songSlot_ : -1; }
    int currentRow() const noexcept { return mode_ == Mode::Song ? row_ : -1; }
    int currentRepeat() const noexcept { return repeat_; } // Zero-based.
    int currentStep() const noexcept { return rowStep_; } // Zero-based in row.
    bool hasQueuedSelection() const noexcept { return pending_ || queuedRow_ >= 0; }
    PatternAddress queuedPattern() const noexcept;

private:
    Transition activate(Mode mode, int position, int row, bool forceKit = false) noexcept;
    int playbackLength() const noexcept;
    static int boundedPattern(int index) noexcept;
    static SongRow sanitized(SongRow row) noexcept;
    static PatternSettings sanitized(PatternSettings settings) noexcept;

    std::array<PatternSettings, patternSlots> patterns_{};
    std::array<Song, songSlots> songs_{};
    std::array<PatternAddress, patternChainCapacity> chain_{};
    int chainCount_ = 0, chainPosition_ = 0;
    Mode mode_ = Mode::Pattern, pendingMode_ = Mode::Pattern;
    int currentPattern_ = 0, songSlot_ = 0, row_ = 0, repeat_ = 0;
    int rowStep_ = 0, patternStep_ = 0, pendingPosition_ = 0, pendingRow_ = 0;
    int queuedRow_ = -1;
    bool playing_ = false, pending_ = false, loopRow_ = false, performKit_ = false;
};
}
