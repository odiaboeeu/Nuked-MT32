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
 *  Reverb, using the behavioural Boss reverb model vendored from Munt.
 *
 *  The MT-32's reverb is a separate chip and no decap of it exists, so this
 *  is NOT part of the chip-accurate emulation - it is an approximation
 *  standing in for hardware nobody has imaged. See munt/README.md.
 *
 *  Mode, time and level follow the machine: the MIDI stream is watched for
 *  writes to the MT-32 system area (0x10 0x00 0x01..0x03), which is how a
 *  song sets its own reverb, and they can also be pinned from the command
 *  line.
 */
#pragma once
#include <stdint.h>

class Mt32Reverb {
public:
    explicit Mt32Reverb(bool oldMT32 = true);
    ~Mt32Reverb();

    void init();                       // allocate the four mode models
    void setMode(int mode);            // 0 room, 1 hall, 2 plate, 3 tap delay
    void setParameters(int time, int level);   // each 0-7
    void setDeviceID(uint8_t deviceID)
    {
        device_id_ = deviceID & 0x1f;
    }
    void lockSettings(bool locked) { locked_ = locked; }

    // Feed every MIDI byte here so reverb settings track the song.
    // Ignored while settings are locked by the command line.
    void observeMidiByte(uint8_t b);

    // Adds wet output to dry interleaved stereo frames. The reverb input
    // contains only LA32 partials whose Reverb Switch is enabled.
    void process(
        int16_t *frames,
        const int16_t *reverbInputFrames,
        int count
    );

    int mode()  const { return mode_; }
    int time()  const { return time_; }
    int level() const { return level_; }

private:
    const bool old_mt32_;
    void *models_[4] = {nullptr, nullptr, nullptr, nullptr};
    int mode_ = 0, time_ = 5, level_ = 3;
    uint8_t device_id_ = 0x10;
    bool locked_ = false;

    // Incremental SysEx sniffer state. The final byte before F7 is retained
    // as the checksum, so complete DT1 messages do not need to be buffered.
    enum { IDLE, IN_SYSEX } sx_state_ = IDLE;
    uint8_t sx_header_[7] = {};
    int sx_header_len_ = 0;
    bool sx_pending_valid_ = false;
    uint8_t sx_pending_ = 0;
    uint32_t sx_sum_ = 0;
    uint32_t sx_data_len_ = 0;
    uint8_t sx_reverb_values_[3] = {};
    uint8_t sx_reverb_mask_ = 0;
};
