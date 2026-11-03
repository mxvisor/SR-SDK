/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 *  the Software, and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 *  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/* The implementations of TinySoundFont and TinyMidiLoader.  This file is C,
 * not C++ (the rest of the SDK's .c files are compiled as C++): the library's
 * static tables then need no C++ runtime (__cxa_guard_*), which the link line
 * of the port does not have. */

#define TSF_IMPLEMENTATION
#include "tsf.h"
#define TML_IMPLEMENTATION
#include "tml.h"

/* The levels of the regions as fluidsynth (and the EMU cards the General MIDI
 * banks were made on) gives them, called once after tsf_load_*:
 * - the initial attenuation counts 0.4 of its centibels: TSF takes 0.01 dB per
 *   centibel, 0.04 here (its clamp at 144 cB stays, fluidsynth's is the same);
 *   with TSF's 0.01 the regions a bank attenuates the most (the cymbals and
 *   the hi-hats) come out several dB too loud against the rest;
 * - a resonant lowpass lowers the gain by half the height of its peak (SF2
 *   2.01, 8.1.3, as fluidsynth's filter_gain); TSF keeps the full gain. */
void Game_TsfLevelRegions(tsf *f)
{
    int p, r;

    for (p = 0; p < f->presetNum; p++)
    {
        for (r = 0; r < f->presets[p].regionNum; r++)
        {
            struct tsf_region *region = &f->presets[p].regions[r];

            region->attenuation = region->attenuation * 4.0f + region->initialFilterQ / 20.0f;
        }
    }
}

/* tsf_channel_note_on for presets whose regions share an exclusive class
 * (the hi-hats of the General MIDI drum kits are a left and a right sample in
 * one group): tsf_note_on makes each new region silence every voice of its
 * group, also the ones the same note has just started, so only the last
 * sample of the pair sounds (the hi-hats come out on the right side alone).
 * Here the voices of earlier notes in the group are silenced first and the
 * note starts without groups, which leaves the pair whole. */
static int tsf_note_on_groups(tsf *f, int channel, int key, float vel)
{
    struct tsf_preset *preset;
    struct tsf_region *region, *end;
    struct tsf_voice *v, *vend;
    unsigned int groups[64];
    int preset_index, velocity = (int)(vel * 127), i, count = 0, result;

    if (!f->channels || channel >= f->channels->channelNum || vel <= 0.0f)
        return tsf_channel_note_on(f, channel, key, vel);

    preset_index = f->channels->channels[channel].presetIndex;
    if (preset_index < 0 || preset_index >= f->presetNum)
        return tsf_channel_note_on(f, channel, key, vel);
    preset = &f->presets[preset_index];
    end = preset->regions + preset->regionNum;

    for (region = preset->regions; region != end; region++)
    {
        if (key < region->lokey || key > region->hikey || velocity < region->lovel || velocity > region->hivel) continue;
        if (region->group == 0) continue;
        for (i = 0; i < count && groups[i] != region->group; i++) {}
        if (i == count && count < 64) groups[count++] = region->group;
    }

    if (count == 0) return tsf_channel_note_on(f, channel, key, vel);

    for (v = f->voices, vend = v + f->voiceNum; v != vend; v++)
    {
        if (v->playingPreset != preset_index) continue;
        for (i = 0; i < count; i++)
        {
            if (v->region->group == groups[i]) { tsf_voice_endquick(f, v); break; }
        }
    }

    /* the groups of the regions this note plays are off while it starts */
    {
        unsigned int saved[1024];
        int n = 0;

        for (region = preset->regions; region != end; region++, n++)
        {
            if (n >= 1024) break;
            saved[n] = region->group;
            if (key >= region->lokey && key <= region->hikey && velocity >= region->lovel && velocity <= region->hivel)
                region->group = 0;
        }
        result = tsf_channel_note_on(f, channel, key, vel);
        for (region = preset->regions, i = 0; i < n; region++, i++) region->group = saved[i];
    }
    return result;
}

/* The velocity of a note makes its level as the square of the velocity, 40 dB
 * over the whole range, as the General MIDI synthesizers and the SoundFont
 * specification have it; TinySoundFont has the velocity itself, 20 dB.  The
 * voices of the note get the second half, and the exclusive classes work as in
 * tsf_note_on_groups. */
int Game_TsfChannelNoteOn(tsf *f, int channel, int key, float vel)
{
    unsigned int first = f->voicePlayIndex;
    struct tsf_voice *v, *vend;
    int result = tsf_note_on_groups(f, channel, key, vel);

    if (vel > 0.0f && vel < 1.0f)
    {
        float db = tsf_gainToDecibels(vel);

        for (v = f->voices, vend = v + f->voiceNum; v != vend; v++)
        {
            if (v->playingPreset != -1 && v->playIndex >= first && v->playIndex < f->voicePlayIndex)
                v->noteGainDB += db;
        }
    }
    return result;
}

/* tsf_channel_midi_control with the volume (7) and expression (11) as the
 * square of their values each, the curve of the General MIDI synthesizers (the
 * level is 40 dB down at a tenth of the value); TinySoundFont raises the product
 * to the third power, and a part at the volume 55 is 7 dB quieter than it
 * should be next to the one at 127. */
int Game_TsfChannelMidiControl(tsf *f, int channel, int controller, int value)
{
    int result = tsf_channel_midi_control(f, channel, controller, value);

    if (result && (controller == 7 || controller == 39 || controller == 11 || controller == 43))
    {
        struct tsf_channel *c = &f->channels->channels[channel];
        float x = (c->midiVolume / 16383.0f) * (c->midiExpression / 16383.0f);

        tsf_channel_set_volume(f, channel, x * x);
    }
    return result;
}
