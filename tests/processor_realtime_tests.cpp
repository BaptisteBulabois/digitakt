#include "PluginProcessor.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>

namespace
{
thread_local bool measuring = false;
thread_local std::size_t cppAllocations = 0, cppFrees = 0;
thread_local std::size_t cAllocations = 0, cFrees = 0;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void* allocate(std::size_t bytes)
{
    if (measuring) ++cppAllocations;
    if (auto* memory = std::malloc(bytes == 0 ? 1 : bytes)) return memory;
    throw std::bad_alloc();
}

void release(void* memory) noexcept
{
    if (measuring && memory != nullptr) ++cppFrees;
    std::free(memory);
}

void* allocateAligned(std::size_t bytes, std::size_t alignment)
{
    if (measuring) ++cppAllocations;
    void* memory = nullptr;
    if (posix_memalign(&memory, alignment, bytes == 0 ? alignment : bytes) != 0)
        throw std::bad_alloc();
    return memory;
}

struct Measurement
{
    Measurement()
    {
        cppAllocations = cppFrees = cAllocations = cFrees = 0;
        measuring = true;
    }
    ~Measurement() { measuring = false; }
};

void measuredBlock(TaktAudioProcessor& processor, juce::AudioBuffer<float>& audio,
                   juce::MidiBuffer& midi, const char* description, bool print = true)
{
    {
        Measurement count;
        processor.processBlock(audio, midi);
    }
    if (print || cppAllocations != 0 || cppFrees != 0 || cAllocations != 0 || cFrees != 0)
        std::cout << description << ": new=" << cppAllocations << ", delete=" << cppFrees
                  << ", malloc/calloc/realloc=" << cAllocations << ", free=" << cFrees << '\n';
    require(cppAllocations == 0 && cppFrees == 0 && cAllocations == 0 && cFrees == 0,
            "The complete processBlock allocated or released heap storage");
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int frame = 0; frame < audio.getNumSamples(); ++frame)
            require(std::isfinite(audio.getSample(channel, frame)), "Non-finite rendered sample");
}
}

void* operator new(std::size_t bytes) { return allocate(bytes); }
void* operator new[](std::size_t bytes) { return allocate(bytes); }
void operator delete(void* memory) noexcept { release(memory); }
void operator delete[](void* memory) noexcept { release(memory); }
void operator delete(void* memory, std::size_t) noexcept { release(memory); }
void operator delete[](void* memory, std::size_t) noexcept { release(memory); }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{ try { return allocate(bytes); } catch (...) { return nullptr; } }
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{ try { return allocate(bytes); } catch (...) { return nullptr; } }
void operator delete(void* memory, const std::nothrow_t&) noexcept { release(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { release(memory); }
void* operator new(std::size_t bytes, std::align_val_t alignment)
{ return allocateAligned(bytes, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t bytes, std::align_val_t alignment)
{ return allocateAligned(bytes, static_cast<std::size_t>(alignment)); }
void operator delete(void* memory, std::align_val_t) noexcept { release(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { release(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { release(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { release(memory); }

extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void __real_free(void*);

extern "C" void* __wrap_malloc(std::size_t bytes)
{
    if (measuring) ++cAllocations;
    return __real_malloc(bytes);
}
extern "C" void* __wrap_calloc(std::size_t count, std::size_t bytes)
{
    if (measuring) ++cAllocations;
    return __real_calloc(count, bytes);
}
extern "C" void* __wrap_realloc(void* memory, std::size_t bytes)
{
    if (measuring)
    {
        ++cAllocations;
        if (memory != nullptr) ++cFrees;
    }
    return __real_realloc(memory, bytes);
}
extern "C" void __wrap_free(void* memory)
{
    if (measuring && memory != nullptr) ++cFrees;
    __real_free(memory);
}

int main()
{
    try
    {
        // MessageManager supports APVTS timers without an editor or device.
        juce::MessageManager::getInstance();
        {
            auto processor = std::make_unique<TaktAudioProcessor>();
            processor->setParameter("play", 0);
            processor->setParameter("hostSync", 0);
            processor->prepareToPlay(48000, 512);
            juce::AudioBuffer<float> audio(2, 512);
            juce::MidiBuffer none;
            measuredBlock(*processor, audio, none, "First prepared processBlock");

            // A long ignored SysEx used to allocate through MidiMessage's
            // owning copy before the processor decided to ignore it.
            std::array<juce::uint8, 1024> sysex{};
            sysex.front() = 0xf0; sysex.back() = 0xf7;
            {
                Measurement verifyInstrumentation;
                const juce::MidiMessage owningCopy(sysex.data(), static_cast<int>(sysex.size()), 0);
                // The copy is destroyed before this scope turns counting off.
            }
            require(cAllocations > 0 && cFrees > 0,
                    "C allocator instrumentation did not detect a known owning SysEx allocation/free");
            juce::MidiBuffer ignored;
            ignored.addEvent(sysex.data(), static_cast<int>(sysex.size()), 0);
            ignored.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 1);
            ignored.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 2);
            measuredBlock(*processor, audio, ignored, "Ignored SysEx/controllers");

            juce::MidiBuffer burst;
            for (int event = 0; event < 600; ++event)
                burst.addEvent(juce::MidiMessage::noteOn(1, 36 + event % 16, juce::uint8(110)),
                               event % 512);
            for (int track = 0; track < 16; ++track)
                burst.addEvent(juce::MidiMessage::noteOff(1, 36 + track), 511);
            measuredBlock(*processor, audio, burst, "Overflow MIDI burst");
            require(processor->getDroppedMidiEvents() > 0, "MIDI burst did not exercise overflow");

            for (int track = 0; track < 16; ++track)
            {
                const auto prefix = "t" + juce::String(track + 1) + "_";
                processor->setParameter(prefix + "machine", 1 + track % 6);
                processor->setParameter(prefix + "ampMode", 2);
                processor->setParameter(prefix + "filterMachine", 1 + track % 6);
                processor->setParameter(prefix + "chorusSend", .5f);
                for (int lfo = 1; lfo <= 3; ++lfo)
                {
                    processor->setParameter(prefix + "lfo" + juce::String(lfo) + "_depth", 10);
                    processor->setParameter(prefix + "lfo" + juce::String(lfo) + "_destination",
                                            static_cast<float>(lfo));
                }
            }
            processor->setParameter("chorusVolume", .5f);
            juce::MidiBuffer allTracks;
            for (int track = 0; track < 16; ++track)
                allTracks.addEvent(juce::MidiMessage::noteOn(1, 36 + track, juce::uint8(110)), 0);
            measuredBlock(*processor, audio, allTracks, "Dirty controls, 16 tracks/48 LFOs/filter/chorus");
            for (int block = 0; block < 32; ++block)
                measuredBlock(*processor, audio, none, "Active modern DSP", block == 31);

            auto selected = std::make_unique<TaktAudioProcessor::PatternSnapshot>(
                *processor->getPatternSnapshot(0));
            selected->trackParameters[0][1] = 1;
            require(processor->setPatternSnapshot(17, *selected) && processor->setCurrentPattern(17),
                    "Cannot establish a pending bank selection");
            measuredBlock(*processor, audio, allTracks, "Prepared immutable bank transition");
            juce::MemoryBlock saved;
            processor->getStateInformation(saved);
            processor->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            measuredBlock(*processor, audio, allTracks, "First block after current project recall");
        }
        juce::DeletedAtShutdown::deleteAll();
        juce::MessageManager::deleteInstance();
        std::cout << "PASS: whole processor render has zero C/C++ heap allocation or release in tested callbacks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        measuring = false;
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
