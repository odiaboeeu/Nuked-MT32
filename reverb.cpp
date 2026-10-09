/*
 * Copyright (C) 2024, 2025 nukeykt
 *
 * This file is part of Nuked-MT32.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "reverb.h"
#include "munt/BReverbModel.h"
#include <vector>
#include <string.h>

using MT32Emu::BReverbModel;
using MT32Emu::ReverbMode;

static constexpr int MAX_CHUNK = 1024;

Mt32Reverb::Mt32Reverb(bool oldMT32) : old_mt32_(oldMT32) {}

Mt32Reverb::~Mt32Reverb()
{
    for (int i = 0; i < 4; i++)
        delete static_cast<BReverbModel *>(models_[i]);
}

void Mt32Reverb::init()
{
    for (int i = 0; i < 4; i++) {
        if (models_[i]) continue;
        // Munt compatibility: true selects OLD; false selects NEW-generation settings.
        BReverbModel *m = BReverbModel::createBReverbModel(
            ReverbMode(i), old_mt32_, MT32Emu::RendererType_BIT16S);
        m->open();
        m->setParameters(uint8_t(time_), uint8_t(level_));
        models_[i] = m;
    }
}

void Mt32Reverb::setMode(int mode)
{
    if (mode < 0) mode = 0;
    if (mode > 3) mode = 3;
    if (mode == mode_) return;
    mode_ = mode;
    if (models_[mode_])
        static_cast<BReverbModel *>(models_[mode_])->mute();
}

void Mt32Reverb::setParameters(int time, int level)
{
    if (time  < 0) time  = 0;  if (time  > 7) time  = 7;
    if (level < 0) level = 0;  if (level > 7) level = 7;
    time_ = time; level_ = level;
    for (int i = 0; i < 4; i++)
        if (models_[i])
            static_cast<BReverbModel *>(models_[i])->setParameters(uint8_t(time_), uint8_t(level_));
}

void Mt32Reverb::observeMidiByte(uint8_t b)
{
    if (locked_)
        return;

    // System Real-Time messages may be interleaved anywhere in the MIDI
    // stream, including inside SysEx, without terminating the message.
    if (b >= 0xf8)
        return;

    auto reset = [this]() {
        sx_state_ = IDLE;
        sx_header_len_ = 0;
        sx_pending_valid_ = false;
        sx_pending_ = 0;
        sx_sum_ = 0;
        sx_data_len_ = 0;
        sx_reverb_mask_ = 0;
    };

    if (b == 0xf0) {
        reset();
        sx_state_ = IN_SYSEX;
        return;
    }

    if (sx_state_ != IN_SYSEX)
        return;

    if (b == 0xf7) {
        // The MT-32 special reset has no three-byte address or checksum:
        // F0 41 <device> 16 12 7F 01 F7.
        const bool special_reset =
            sx_header_len_ == 6 &&
            sx_header_[0] == 0x41 &&
            sx_header_[1] == device_id_ &&
            sx_header_[2] == 0x16 &&
            sx_header_[3] == 0x12 &&
            sx_header_[4] == 0x7f &&
            sx_header_[5] == 0x01;

        if (special_reset) {
            setMode(0);
            setParameters(5, 3);
            reset();
            return;
        }

        // Roland MT-32 DT1:
        // 41 <dev> 16 12 <a1 a2 a3> <data...> <checksum>
        //
        // The default Device ID is 0x10, corresponding to the MT-32 default
        // Unit Number 17. Frontends may override it with setDeviceID().
        const bool valid_header =
            sx_header_len_ == 7 &&
            sx_header_[0] == 0x41 &&
            sx_header_[1] == device_id_ &&
            sx_header_[2] == 0x16 &&
            sx_header_[3] == 0x12;

        const bool valid_checksum =
            sx_pending_valid_ &&
            ((sx_sum_ + sx_pending_) & 0x7f) == 0;

        if (valid_header && valid_checksum) {
            int new_mode = mode_;
            int new_time = time_;
            int new_level = level_;

            if (sx_reverb_mask_ & 0x01)
                new_mode = sx_reverb_values_[0];
            if (sx_reverb_mask_ & 0x02)
                new_time = sx_reverb_values_[1];
            if (sx_reverb_mask_ & 0x04)
                new_level = sx_reverb_values_[2];

            if (sx_reverb_mask_ & 0x01)
                setMode(new_mode);

            if (sx_reverb_mask_ & 0x06)
                setParameters(new_time, new_level);
        }

        reset();
        return;
    }

    if (b & 0x80) {
        reset();
        return;
    }

    if (sx_header_len_ < 7) {
        sx_header_[sx_header_len_++] = b;

        if (sx_header_len_ == 7)
            sx_sum_ = sx_header_[4] + sx_header_[5] + sx_header_[6];

        return;
    }

    // Keep one byte pending. When another byte arrives, the previous one is
    // known to be data. At F7, the remaining pending byte is the checksum.
    if (sx_pending_valid_) {
        const uint8_t data = sx_pending_;
        const uint32_t address =
            (uint32_t(sx_header_[4]) << 14) |
            (uint32_t(sx_header_[5]) << 7) |
            uint32_t(sx_header_[6]);

        const uint32_t data_address = address + sx_data_len_;

        sx_sum_ += data;

        if (data_address >= 0x040001 && data_address <= 0x040003) {
            const unsigned int index =
                static_cast<unsigned int>(data_address - 0x040001);

            sx_reverb_values_[index] = data & 0x7f;
            sx_reverb_mask_ |= uint8_t(1u << index);
        }

        ++sx_data_len_;
    }

    sx_pending_ = b;
    sx_pending_valid_ = true;
}

void Mt32Reverb::process(
    int16_t *frames,
    const int16_t *reverbInputFrames,
    int count
)
{
    BReverbModel *m = static_cast<BReverbModel *>(models_[mode_]);
    if (!m || !frames || !reverbInputFrames || count <= 0) return;

    static std::vector<int16_t> il, ir, ol, orr;
    if (int(il.size()) < MAX_CHUNK) { il.resize(MAX_CHUNK); ir.resize(MAX_CHUNK);
                                      ol.resize(MAX_CHUNK); orr.resize(MAX_CHUNK); }

    int done = 0;
    while (done < count) {
        int n = count - done;
        if (n > MAX_CHUNK) n = MAX_CHUNK;

        for (int i = 0; i < n; i++) {
            il[i] = reverbInputFrames[(done + i) * 2];
            ir[i] = reverbInputFrames[(done + i) * 2 + 1];
        }

        if (m->process(il.data(), ir.data(), ol.data(), orr.data(), MT32Emu::Bit32u(n))) {
            // Dry plus wet, saturating - the reverb chip sits alongside the
            // DAC output rather than replacing it.
            for (int i = 0; i < n; i++) {
                int32_t l =
                    int32_t(frames[(done + i) * 2]) +
                    int32_t(ol[i]);
                int32_t r =
                    int32_t(frames[(done + i) * 2 + 1]) +
                    int32_t(orr[i]);
                if (l < -32768) l = -32768; if (l > 32767) l = 32767;
                if (r < -32768) r = -32768; if (r > 32767) r = 32767;
                frames[(done + i) * 2]     = int16_t(l);
                frames[(done + i) * 2 + 1] = int16_t(r);
            }
        }
        done += n;
    }
}
