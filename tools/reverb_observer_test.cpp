/*
 * Unit tests for the external MT-32 reverb SysEx observer.
 *
 * These tests do not initialize the audio model and require no ROM images.
 * They validate only MIDI parsing and the exposed reverb state.
 */
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "reverb.h"

static int failures = 0;

static void check(bool condition, const char *name)
{
    std::printf("  %s  %s\n", condition ? "[ok]  " : "[FAIL]", name);
    if (!condition)
        ++failures;
}

static uint8_t rolandChecksum(
    const std::vector<uint8_t> &address,
    const std::vector<uint8_t> &data
)
{
    unsigned int sum = 0;

    for (uint8_t value : address)
        sum += value;

    for (uint8_t value : data)
        sum += value;

    return static_cast<uint8_t>((128 - (sum & 0x7f)) & 0x7f);
}

static std::vector<uint8_t> makeDt1(
    uint8_t device,
    const std::vector<uint8_t> &address,
    const std::vector<uint8_t> &data,
    bool validChecksum = true
)
{
    std::vector<uint8_t> message = {
        0xf0,
        0x41,
        device,
        0x16,
        0x12
    };

    message.insert(message.end(), address.begin(), address.end());
    message.insert(message.end(), data.begin(), data.end());

    uint8_t checksum = rolandChecksum(address, data);

    if (!validChecksum)
        checksum = static_cast<uint8_t>((checksum + 1) & 0x7f);

    message.push_back(checksum);
    message.push_back(0xf7);

    return message;
}

static void send(Mt32Reverb &reverb, const std::vector<uint8_t> &message)
{
    for (uint8_t value : message)
        reverb.observeMidiByte(value);
}

static bool stateIs(
    const Mt32Reverb &reverb,
    int mode,
    int time,
    int level
)
{
    return reverb.mode() == mode
        && reverb.time() == time
        && reverb.level() == level;
}

int main()
{
    std::puts("MT-32 reverb SysEx observer test");

    {
        Mt32Reverb reverb;
        check(
            stateIs(reverb, 0, 5, 3),
            "initial state is mode 0, time 5, level 3"
        );
    }

    {
        Mt32Reverb reverb;

        send(
            reverb,
            makeDt1(
                0x10,
                {0x10, 0x00, 0x01},
                {0x02, 0x06, 0x07}
            )
        );

        check(
            stateIs(reverb, 2, 6, 7),
            "valid DT1 updates mode, time and level"
        );
    }

    {
        Mt32Reverb reverb;

        send(
            reverb,
            makeDt1(
                0x10,
                {0x10, 0x00, 0x01},
                {0x03, 0x07, 0x06},
                false
            )
        );

        check(
            stateIs(reverb, 0, 5, 3),
            "invalid Roland checksum is rejected"
        );
    }

    {
        Mt32Reverb reverb;

        send(
            reverb,
            makeDt1(
                0x10,
                {0x20, 0x00, 0x01},
                {0x03, 0x07, 0x07}
            )
        );

        check(
            stateIs(reverb, 0, 5, 3),
            "DT1 outside the system area is ignored"
        );
    }

    {
        Mt32Reverb reverb;
        std::vector<uint8_t> data(128);

        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<uint8_t>((i * 7) & 0x7f);

        data[0] = 0x01;
        data[1] = 0x02;
        data[2] = 0x03;

        send(
            reverb,
            makeDt1(
                0x10,
                {0x10, 0x00, 0x01},
                data
            )
        );

        check(
            stateIs(reverb, 1, 2, 3),
            "long valid DT1 updates reverb without truncation"
        );
    }

    {
        Mt32Reverb reverb;
        std::vector<uint8_t> data(128);

        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<uint8_t>((i * 5) & 0x7f);

        data[0] = 0x03;
        data[1] = 0x07;
        data[2] = 0x06;

        send(
            reverb,
            makeDt1(
                0x10,
                {0x10, 0x00, 0x01},
                data,
                false
            )
        );

        check(
            stateIs(reverb, 0, 5, 3),
            "long DT1 with invalid checksum is rejected"
        );
    }

    {
        Mt32Reverb reverb;

        const std::vector<uint8_t> incomplete = {
            0xf0,
            0x41,
            0x10,
            0x16,
            0x12,
            0x10,
            0x00,
            0x01,
            0x03,
            0x07,
            0x07
        };

        send(reverb, incomplete);

        check(
            stateIs(reverb, 0, 5, 3),
            "incomplete SysEx without F7 does not update state"
        );
    }

    {
        Mt32Reverb reverb;

        reverb.lockSettings(true);

        send(
            reverb,
            makeDt1(
                0x10,
                {0x10, 0x00, 0x01},
                {0x03, 0x07, 0x07}
            )
        );

        check(
            stateIs(reverb, 0, 5, 3),
            "locked settings ignore incoming SysEx"
        );
    }

    {
        Mt32Reverb reverb;

        send(
            reverb,
            makeDt1(
                0x1f,
                {0x10, 0x00, 0x01},
                {0x01, 0x04, 0x05}
            )
        );

        check(
            stateIs(reverb, 1, 4, 5),
            "observer remains device-ID agnostic"
        );
    }

    {
        Mt32Reverb reverb;

        const std::vector<uint8_t> aborted = {
            0xf0,
            0x41,
            0x10,
            0x16,
            0x12,
            0x10,
            0x00,
            0x01,
            0x03,
            0x90,
            0x40,
            0x7f,
            0xf7
        };

        send(reverb, aborted);

        check(
            stateIs(reverb, 0, 5, 3),
            "illegal status aborts an in-progress SysEx"
        );
    }

    {
        Mt32Reverb reverb;

        send(
            reverb,
            makeDt1(
                0x10,
                {0x10, 0x00, 0x00},
                {0x40, 0x03, 0x07, 0x06}
            )
        );

        check(
            stateIs(reverb, 3, 7, 6),
            "DT1 starting before reverb crosses into mode, time and level"
        );
    }

    {
        Mt32Reverb reverb;

        const std::vector<uint8_t> restarted = {
            0xf0,
            0x41,
            0x10,
            0x16,
            0x12,
            0x10,
            0x00,
            0x01,
            0x03,
            0xf0
        };

        send(reverb, restarted);

        send(
            reverb,
            makeDt1(
                0x10,
                {0x10, 0x00, 0x01},
                {0x02, 0x04, 0x05}
            )
        );

        check(
            stateIs(reverb, 2, 4, 5),
            "new F0 discards an incomplete SysEx and starts a new message"
        );
    }

    {
        Mt32Reverb reverb;

        const std::vector<uint8_t> tooShort = {
            0xf0,
            0x41,
            0x10,
            0x16,
            0x12,
            0x10,
            0x00,
            0xf7
        };

        send(reverb, tooShort);

        check(
            stateIs(reverb, 0, 5, 3),
            "SysEx without complete address and checksum is ignored"
        );
    }

    {
        Mt32Reverb reverb;

        send(
            reverb,
            makeDt1(
                0x10,
                {0x10, 0x00, 0x01},
                {0x02, 0x06, 0x07}
            )
        );

        send(
            reverb,
            {
                0xf0,
                0x41,
                0x10,
                0x16,
                0x12,
                0x7f,
                0x01,
                0xf7
            }
        );

        check(
            stateIs(reverb, 0, 5, 3),
            "special reset restores mode 0, time 5 and level 3"
        );
    }
    std::printf("\n%d check(s) failed.\n", failures);

    if (failures == 0)
        std::puts("PASS");

    return failures == 0 ? 0 : 1;
}
