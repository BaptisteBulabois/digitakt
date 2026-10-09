#include "engine/PatternChain.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace
{
bool monitorAllocations = false;
std::size_t allocationCount = 0;
}
void* operator new(std::size_t bytes)
{
    if (monitorAllocations) ++allocationCount;
    if (void* result = std::malloc(bytes == 0 ? 1 : bytes)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace
{
using namespace takt;
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
PatternChain::Transition steps(PatternChain& model, int count, bool host = false)
{
    PatternChain::Transition result;
    for (int index = 0; index < count; ++index) result = model.advanceStep(host);
    return result;
}
void testAddressAndCue()
{
    require(PatternAddress::fromBankSlot(7, 16).index == 127, "H16 is slot127");
    require(PatternAddress::fromBankSlot(3, 9).bank() == 3, "bank identity");
    require(PatternAddress{127}.slot() == 16, "slot is display1..16");
    PatternChain model;
    model.setPatternSettings({0}, {4, 110.0, 0.25f, 0});
    model.selectPattern({0});
    model.play();
    steps(model, 1);
    model.selectPattern({127});
    require(model.currentPattern().index == 0 && model.queuedPattern().index == 127,
            "playing cue retains current pattern until boundary");
    require(!steps(model, 2).patternChanged, "cue never switches early");
    const auto transition = model.advanceStep();
    require(transition.patternChanged && transition.applyKit && transition.pattern.index == 127,
            "cue applies target at boundary with kit");
    require(model.currentStep() == 0 && !model.hasQueuedSelection(), "cue consumes and resets");
    model.stop();
    require(model.selectPattern({14}).pattern.index == 14, "stopped selection immediate");
    model.setPerformKit(true);
    require(!model.selectPattern({15}).applyKit, "Perform Kit retains existing kit");
}
void testChain()
{
    PatternChain model;
    model.setPatternSettings({0}, {2, 120.0, 0, 0});
    model.setPatternSettings({16}, {3, 120.0, 0, 0});
    const PatternAddress chain[] = {{0}, {16}, {0}};
    model.setChain(chain, 3);
    model.play();
    require(steps(model, 2).pattern.index == 16, "chain crosses banks");
    require(steps(model, 3).pattern.index == 0, "chain preserves pattern order");
    require(steps(model, 2).pattern.index == 0 && model.chainSize() == 3, "chain loops after last entry");
    model.selectPattern({127});
    require(model.chainSize() == 0, "normal pattern selection destroys transient chain");
    require(steps(model, 2).pattern.index == 127 && model.mode() == PatternChain::Mode::Pattern,
            "chain exits via queued normal pattern");
    model.setChain(nullptr, 9);
    require(model.chainSize() == 0, "null chain remains safe");
    model.stop(true);
    std::array<PatternAddress, 70> longChain{};
    for (int i = 0; i < 70; ++i) longChain[static_cast<std::size_t>(i)].index = i;
    model.setChain(longChain.data(), 70);
    require(model.chainSize() == 64 && model.chainPattern(100).index == 63, "chain capacity64");
    model.play();
    model.selectPattern({12});
    model.stop(true);
    model.play();
    require(model.mode() == PatternChain::Mode::Pattern, "double STOP after discarded chain avoids empty-chain playback");
    steps(model, 5);
}
Song arrangement()
{
    Song value;
    value.rowCount = 3;
    value.rows[0] = {{0}, 2, 3, 100.0, 0.2f, 1};
    value.rows[1] = {{17}, 1, 2, 0.0, -1.0f, 0x8000};
    value.rows[2] = {{127}, 1, 4, 150.0, 0.6f, 0x0200};
    value.endLoop = true;
    return value;
}
void testSongs()
{
    PatternChain model;
    model.setPatternSettings({17}, {8, 135.0, 0.4f, 0x0020});
    model.setSong(15, arrangement());
    auto state = model.selectSong(15, 1);
    require(state.row == 1 && state.pattern.index == 17 && state.length == 2,
            "start middle uses selected row, not row0");
    require(state.muteMask == 0x8000 && state.tempo == 135.0 && state.swing == 0.4f,
            "row mutes replace pattern defaults; tempo and swing inherit");
    model.play();
    require(steps(model, 2).row == 2, "advance from middle to next row");
    require(steps(model, 4).row == 0, "END loop returns first row even after start-middle");
    require(steps(model, 3).row == 0 && model.currentRepeat() == 1,
            "row play count includes initial playback");
    require(steps(model, 3).row == 1 && model.currentRepeat() == 0, "repeat count advances once complete");
    model.stop(true);
    require(model.currentRow() == 0 && model.currentStep() == 0, "double STOP first row");
    model.play();
    steps(model, 2);
    model.stop();
    require(model.currentStep() == 2, "single STOP retains song position");
    model.play();
    require(steps(model, 1).row == 0 && model.currentRepeat() == 1, "PLAY resumes retained row progress");
    model.setRowLoop(true);
    steps(model, 30);
    require(model.currentRow() == 0 && model.currentRepeat() == 1, "row loop remains bounded");
    require(model.queueSongRow(2) && model.hasQueuedSelection(), "explicit row cue available");
    require(model.queuedPattern().index == 127, "row cue target shown");
    require(steps(model, 3).row == 2, "row cue overrides row loop at row boundary");
    model.setRowLoop(false);
    auto stopSong = arrangement();
    stopSong.endLoop = false;
    model.setSong(15, stopSong);
    const auto end = steps(model, 4);
    require(end.stopped && !model.playing() && model.currentRow() == 2, "END STOP stops arrangement only");
    require(!model.queueSongRow(3), "invalid row cue rejected");
}
void testLengthTempoDefaultsAndSanitizing()
{
    PatternChain model;
    model.setPatternSettings({2}, {2, 125.0, 0.5f, 0x55aa});
    require(model.defaultRow({2}).muteMask == 0x55aa, "new row starts with pattern mutes");
    Song value;
    value.rowCount = 1;
    value.rows[0] = model.defaultRow({2});
    value.rows[0].length = 7;
    value.rows[0].tempo = 140;
    value.tempo = 160;
    model.setSong(0, value);
    model.selectSong(0);
    model.play();
    const auto mid = steps(model, 6, true);
    require(model.currentStep() == 6 && mid.tempo == 160 && !mid.applyTempo,
            "long row loops underlying pattern but host tempo is authoritative");
    require(steps(model, 1).row == 0 && model.currentStep() == 0, "row length uses sequencer steps");
    value.rows[0].length = 1024;
    value.rows[0].repeats = 64;
    value.endLoop = false;
    model.stop(true);
    model.setSong(0, value);
    model.selectSong(0);
    model.play();
    steps(model, 1024 * 64 - 1);
    require(model.playing() && model.currentRepeat() == 63, "max64 repeats lasts complete final row");
    require(model.advanceStep().stopped, "max row length/repeats completes at exact boundary");
    value.rowCount = 999;
    value.tempo = std::numeric_limits<double>::quiet_NaN();
    value.rows[0] = {{999}, -8, 9999, std::numeric_limits<double>::infinity(),
                     std::numeric_limits<float>::quiet_NaN(), 0xffff};
    model.setSong(100, value);
    const auto& sanitized = model.song(15);
    require(sanitized.rowCount == 99 && sanitized.tempo == 0.0, "song capacity and invalid tempo sanitized");
    require(sanitized.rows[0].pattern.index == 127 && sanitized.rows[0].length == 1024
            && sanitized.rows[0].repeats == 1 && sanitized.rows[0].tempo == 0.0
            && sanitized.rows[0].swing == -1.0f, "row limits and nonfinite values sanitized");
    model.setPatternSettings({-1}, {-50, -20, std::numeric_limits<float>::infinity(), 0});
    const auto pattern = model.patternSettings({0});
    require(pattern.length == 1 && pattern.tempo == 30.0 && pattern.swing == 0.0f,
            "pattern metadata invalid values bounded");
    Song empty;
    model.stop();
    model.setSong(0, empty);
    require(model.selectSong(0).stopped && model.play().stopped && !model.playing(), "empty song does not play");
}
void testBoundarySongCueAndNoAllocation()
{
    PatternChain model;
    model.setPatternSettings({0}, {4, 120.0, 0, 0});
    model.selectPattern({0});
    model.play();
    model.setSong(1, arrangement());
    model.selectSong(1, 2);
    steps(model, 3);
    require(model.mode() == PatternChain::Mode::Pattern, "song cue respects current pattern boundary");
    const auto selection = model.advanceStep();
    require(selection.row == 2 && selection.pattern.index == 127, "song cue retains start row");
    model.selectPattern({20});
    steps(model, 4);
    require(model.hasQueuedSelection() && model.currentPattern().index == 127,
            "short song-row boundary cannot destroy queued normal pattern");
    require(steps(model, 12).pattern.index == 20, "queued song exit finishes current pattern");
    model.stop();
    model.selectSong(1, 0);
    model.play();
    const auto before = allocationCount;
    monitorAllocations = true;
    for (int i = 0; i < 100000; ++i) model.advanceStep(i % 2 == 0);
    monitorAllocations = false;
    require(allocationCount == before, "arrangement advancement allocates nothing");
}
}

int main()
{
    try
    {
        testAddressAndCue();
        testChain();
        testSongs();
        testLengthTempoDefaultsAndSanitizing();
        testBoundarySongCueAndNoAllocation();
        std::cout << "PASS pattern cue, chains, songs, repeats, mutes, tempo and allocation-free advancement\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        monitorAllocations = false;
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
