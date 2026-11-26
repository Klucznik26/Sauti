#include "atari8_core.h"

static unsigned chip_index(const Atari8Core *core, uint16_t address)
{
    return core->stereo && (address & 0x10u) ? 1u : 0u;
}

void atari8_core_reset(Atari8Core *core, bool ntsc, bool stereo)
{
    core->stereo = stereo;
    atari8_pokey_reset(&core->pokey[0], ntsc);
    atari8_pokey_reset(&core->pokey[1], ntsc);
}

unsigned atari8_core_channels(const Atari8Core *core)
{
    return core->stereo ? 2u : 1u;
}

bool atari8_core_write(Atari8Core *core, uint16_t address, uint8_t value)
{
    if ((address & 0xff00u) != 0xd200u)
        return false;
    return atari8_pokey_write(&core->pokey[chip_index(core, address)],
                              (uint8_t)(address & 15u), value);
}

bool atari8_core_read(const Atari8Core *core, uint16_t address, uint8_t *value)
{
    if ((address & 0xff00u) != 0xd200u)
        return false;
    return atari8_pokey_read(&core->pokey[chip_index(core, address)],
                             (uint8_t)(address & 15u), value);
}

void atari8_core_step(Atari8Core *core, uint32_t cycles)
{
    atari8_pokey_step(&core->pokey[0], cycles);
    if (core->stereo)
        atari8_pokey_step(&core->pokey[1], cycles);
}

bool atari8_core_irq(const Atari8Core *core)
{
    return atari8_pokey_irq(&core->pokey[0]);
}

void atari8_core_levels(const Atari8Core *core, uint8_t levels[2])
{
    levels[0] = atari8_pokey_level(&core->pokey[0]);
    levels[1] = core->stereo ? atari8_pokey_level(&core->pokey[1]) : 0;
}
