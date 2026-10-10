#include "Engine.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

int main()
{
    constexpr int frames = 256, blocks = 300;
    auto sample = std::make_shared<takt::Sample>();
    sample->sampleRate = 48000;
    for (int i = 0; i < 24000; ++i)
    {
        sample->left.push_back(static_cast<float>(.08 * std::sin(i * .035)));
        sample->right.push_back(static_cast<float>(.08 * std::sin(i * .047)));
    }
    for (int scenario = 0; scenario < 6; ++scenario)
    {
        const char* names[] = {"legacy_drive_bits", "modern_eq_bw_srr", "modern_three_lfos", "silence", "sorted_512_events", "unsorted_512_events"};
        std::vector<double> times;
        double checksum = 0;
        for (int trial = 0; trial < 5; ++trial)
        {
            auto engine = std::make_unique<takt::Engine>();
            engine->prepare(48000, frames);
            takt::TrackParams p;
            p.loop = true; p.playback = takt::PlaybackMode::ForwardLoop;
            p.drive = .6f; p.bitDepth = 10; p.decay = 30;
            p.delaySend = p.reverbSend = 0;
            if (scenario != 0)
            {
                p.machine = takt::Machine::Oneshot;
                p.amplitudeEnvelope.mode = takt::EnvelopeMode::Adsr;
                p.amplitudeEnvelope.sustain = .8f;
                p.filter.machine = takt::FilterMachine::Eq;
                p.filter.eqGain = 6; p.filter.eqQ = 2;
                p.filter.base = 30; p.filter.width = 70;
                p.trackFx.srr = 32;
            }
            if (scenario == 2)
            {
                const takt::LfoDestination dest[] = {takt::LfoDestination::Pitch,takt::LfoDestination::Cutoff,takt::LfoDestination::Pan};
                for (int i = 0; i < 3; ++i)
                { p.lfos[i].depth = 4; p.lfos[i].destination = dest[i]; p.lfos[i].speed = 16; }
            }
            std::vector<takt::TriggerEvent> events;
            for (int track = 0; track < takt::numTracks; ++track)
            {
                if (scenario != 3) engine->setSample(track, sample);
                engine->setTrackParams(track, p);
                events.push_back({0,track,.8f,0});
            }
            if (scenario >= 4)
            {
                events.clear();
                for (int i = 0; i < 512; ++i) events.push_back({i / 2,i % 16,.8f,0});
                if (scenario == 5) { std::mt19937 rng(42); std::shuffle(events.begin(), events.end(), rng); }
            }
            std::array<float,frames> left{},right{};
            engine->process(left.data(),right.data(),frames,{},events.data(),static_cast<int>(events.size()));
            const auto start = std::chrono::steady_clock::now();
            for (int block = 0; block < blocks; ++block)
            {
                for (int track = 0; track < takt::numTracks; ++track) engine->setTrackParams(track,p);
                engine->process(left.data(),right.data(),frames,{},scenario >= 4 ? events.data() : nullptr,scenario >= 4 ? static_cast<int>(events.size()) : 0);
                checksum += left[block % frames];
            }
            times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        }
        std::sort(times.begin(),times.end());
        std::cout << names[scenario] << ' ' << times[2] << " ms checksum " << checksum << '\n';
    }
}
