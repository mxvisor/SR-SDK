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

/* tsf_channel_note_on for presets whose regions share an exclusive class
 * (the hi-hats of the General MIDI drum kits are a left and a right sample in
 * one group): tsf_note_on makes each new region silence every voice of its
 * group, also the ones the same note has just started, so only the last
 * sample of the pair sounds (the hi-hats come out on the right side alone).
 * Here the voices of earlier notes in the group are silenced first and the
 * note starts without groups, which leaves the pair whole. */
int Game_TsfChannelNoteOn(tsf *f, int channel, int key, float vel)
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
