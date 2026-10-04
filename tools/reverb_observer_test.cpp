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
            stateIs(reverb, 0, 5, 3),
            "DT1 with wrong Device ID is rejected"
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
    {
        Mt32Reverb reverb;

        reverb.setDeviceID(0x1f);

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
            "configured Device ID is accepted"
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
                0xf0, 0x41, 0x1f, 0x16,
                0x12, 0x7f, 0x01, 0xf7
            }
        );

        check(
            stateIs(reverb, 2, 6, 7),
            "special reset with wrong Device ID is rejected"
        );
    }

    {
        Mt32Reverb reverb;

        std::vector<uint8_t> message = makeDt1(
            0x10,
            {0x10, 0x00, 0x01},
            {0x02, 0x06, 0x07}
        );

        message.insert(message.begin() + 6, 0xf8);

        send(reverb, message);

        check(
            stateIs(reverb, 2, 6, 7),
            "Timing Clock inside SysEx does not abort DT1"
        );
    }

    {
        Mt32Reverb reverb;

        std::vector<uint8_t> message = makeDt1(
            0x10,
            {0x10, 0x00, 0x01},
            {0x01, 0x04, 0x05}
        );

        message.insert(message.begin() + 9, 0xfe);

        send(reverb, message);

        check(
            stateIs(reverb, 1, 4, 5),
            "Active Sensing inside SysEx does not abort DT1"
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
                0xf0, 0x41, 0x10, 0xf8,
                0x16, 0x12, 0x7f, 0x01, 0xf7
            }
        );

        check(
            stateIs(reverb, 0, 5, 3),
            "Timing Clock inside special reset is ignored"
        );
    }

    {
        const uint8_t system_common_statuses[] = {
            0xf1,
            0xf2,
            0xf3,
            0xf4,
            0xf5,
            0xf6
        };

        for (uint8_t status : system_common_statuses) {
            Mt32Reverb reverb;

            const std::vector<uint8_t> interrupted = {
                0xf0,
                0x41,
                0x10,
                0x16,
                0x12,
                0x10,
                0x00,
                0x01,
                0x03,
                status,
                0x07,
                0x06,
                0x70,
                0xf7
            };

            send(reverb, interrupted);

            char description[96];
            std::snprintf(
                description,
                sizeof(description),
                "System Common status %02X aborts an in-progress SysEx",
                unsigned(status)
            );

            check(
                stateIs(reverb, 0, 5, 3),
                description
            );
        }
    }

    {
        Mt32Reverb reverb;

        send(
            reverb,
            {
                0xf0,
                0x41,
                0x10,
                0x16,
                0x12,
                0x10,
                0x00,
                0x01,
                0x03,
                0xf2,
                0x00,
                0x00,
                0xf7
            }
        );

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
            "new F0 recovers after System Common abort"
        );
    }

    {
        struct ReverbClampCase {
            const char *description;
            uint8_t sent_mode;
            uint8_t sent_time;
            uint8_t sent_level;
            int expected_mode;
            int expected_time;
            int expected_level;
        };

        const ReverbClampCase cases[] = {
            {
                "reverb mode 4 is clamped to 3",
                4, 6, 7,
                3, 6, 7
            },
            {
                "reverb mode 7 is clamped to 3",
                7, 6, 7,
                3, 6, 7
            },
            {
                "reverb mode 127 is clamped to 3",
                127, 6, 7,
                3, 6, 7
            },
            {
                "reverb time 8 is clamped to 7",
                2, 8, 7,
                2, 7, 7
            },
            {
                "reverb time 127 is clamped to 7",
                2, 127, 7,
                2, 7, 7
            },
            {
                "reverb level 8 is clamped to 7",
                2, 6, 8,
                2, 6, 7
            },
            {
                "reverb level 127 is clamped to 7",
                2, 6, 127,
                2, 6, 7
            }
        };

        for (const ReverbClampCase &test_case : cases) {
            Mt32Reverb reverb;

            send(
                reverb,
                makeDt1(
                    0x10,
                    {0x10, 0x00, 0x01},
                    {
                        test_case.sent_mode,
                        test_case.sent_time,
                        test_case.sent_level
                    }
                )
            );

            check(
                stateIs(
                    reverb,
                    test_case.expected_mode,
                    test_case.expected_time,
                    test_case.expected_level
                ),
                test_case.description
            );
        }
    }

    std::printf("\n%d check(s) failed.\n", failures);

    if (failures == 0)
        std::puts("PASS");

    return failures == 0 ? 0 : 1;
}
