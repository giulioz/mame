// license:BSD-3-Clause
// copyright-holders:Valley Bell

// chip 1 devices
// adder 18 bit
// ram A 256word? x 16bit (maybe 204 words? 34 slots)
	// 0: ?
	// 1: rom bank / loop mode
	// 2: frequency
	// 3: env speed/target
	// 4: phase addr (low)
	// 5: phase addr (high)
	// 6: sample end addr (high)
	// 7: sample loop addr (high)
// ram B 256word? x 16bit
	// 0:
	// 1:
	// 2:
	// 3:
	// 4:
	// 5:
	// 6:
	// 7:

// chip 2 devices
// multiplier 28bit -> 22bit (actually 29bit, >>7) (16x12?)
// adder 16bit
// rom 12bit (same as JV-880)
// ram 32word x 12bit

// cycles per voice (?) (32/16/8):
// 0
// 1
// 2
// 3
// 4
// 5
// 6
// 7 compare loop

// register write:
//  00/01 - current env value
//  02/03 - current env value / ROM bank (bits 10-13) / loop mode (bits 14-15)
//  04/05 - frequency (2.14 fixed point, 0x4000 = 32000 Hz)
//  06/07 - volume (signed speed/target)
//  08/09 - sample start address, fraction (2.14 fixed point, i.e. 1 byte = 0x4000)
//  0A/0B - sample start address (high word, i.e. address bits 2..17)
//  0C/0D - sample end address (high word)
//  0E/0F - sample loop address (high word)
//  10 - read voice volume lsb (only 03 lsb, 02 is 00)
//  12 - read voice volume msb and bank/loop
//  14 - read voice freq
//  16 - read voice volume speed/target
//  18 - read voice phase low word
//  1A - read voice phase high word
//  1C - read voice sample end
//  1E - read voice sample loop
//  1F - voice select
//  11/13/15/17 - voice enable mask (11 = least significant 8 bits, 17 = most significant 8 bits)
//  19/1B/1D - ?? config
//
// register read:
//  01 - last sample data (used by main CPU to read sample table from PCM ROM)
//  02/03 - readback for 10/12/1a

// interrupts:
// low when vol envelope is happening?

// env lengths:
// 0x01: 28.6s
// 0x04: 21.56s
// 0x08: 16.20s
// 0x0c: 10.88s
// 0x10: 8.22s
// 0x14: 5.6s
// 0x18: 4.26s
// 0x1c: 2.94s
// 0x20: 2.28s
// 0x30: 0.570s
// 0x40: 0.198s
// 0x50: 0.105s
// 0x60: 0.0816s
// 0x70: 0.084s
// 0x7f: 0.0784s

// 0x90: 0.031s
// 0xa0: 0.124s
// 0xb0: 0.5s
// 0xc0: 1.988s
// 0xd0: 7.94s
// 0xf0: 128s

// volume:
// 0x00: 0x00000000
// 0x10: 0x00000000
// 0x20: 0x00000000
// 0x30: 0x00002000
// 0x40: 0x00004000
// 0x50: 0x00008000
// 0x60: 0x00010000
// 0x70: 0x00020000
// 0x80: 0x00040000
// 0x90: 0x00080000
// 0xa0: 0x00100000
// 0xb0: 0x00200000
// 0xc0: 0x00400000
// 0xd0: 0x00800000
// 0xe0: 0x01000000
// 0xf0: 0x02000000
// 0xff: 0x03e00000


// loops (start:00010000 lp:00020000 end:00030000)
// 00: 00010000 -> 00033fff -> 00024000 -> 00033fff -> ...   forward loop
// 01: reverse
// 02: ping-pong
// 03: ?


#include "emu.h"
#include "roland_lp.h"
#include "multibyte.h"

#define LOG_REGISTERS (1U << 1)
#define LOG_PITCH     (1U << 2)

#define VERBOSE (0)
#include "logmacro.h"

// Internally the chip uses a bunch of logic gates to simulate a rom with those values
static constexpr int32_t env_limit_table[] = {
    0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000,
    0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000,
    0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000,
    0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000,
    0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000,
    0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000,
    0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0000000, 0x0002000,
    0x0002000, 0x0002000, 0x0002000, 0x0002000, 0x0002000, 0x0002000, 0x0002000,
    0x0002000, 0x0002000, 0x0002000, 0x0002000, 0x0002000, 0x0002000, 0x0002000,
    0x0002000, 0x0004000, 0x0004000, 0x0004000, 0x0004000, 0x0004000, 0x0004000,
    0x0004000, 0x0004000, 0x0006000, 0x0006000, 0x0006000, 0x0006000, 0x0006000,
    0x0006000, 0x0006000, 0x0006000, 0x0008000, 0x0008000, 0x0008000, 0x0008000,
    0x000a000, 0x000a000, 0x000a000, 0x000a000, 0x000c000, 0x000c000, 0x000c000,
    0x000c000, 0x000e000, 0x000e000, 0x000e000, 0x000e000, 0x0010000, 0x0010000,
    0x0012000, 0x0012000, 0x0014000, 0x0014000, 0x0016000, 0x0016000, 0x0018000,
    0x0018000, 0x001a000, 0x001a000, 0x001c000, 0x001c000, 0x001e000, 0x001e000,
    0x0020000, 0x0022000, 0x0024000, 0x0026000, 0x0028000, 0x002a000, 0x002c000,
    0x002e000, 0x0030000, 0x0032000, 0x0034000, 0x0036000, 0x0038000, 0x003a000,
    0x003c000, 0x003e000, 0x0040000, 0x0044000, 0x0048000, 0x004c000, 0x0050000,
    0x0054000, 0x0058000, 0x005c000, 0x0060000, 0x0064000, 0x0068000, 0x006c000,
    0x0070000, 0x0074000, 0x0078000, 0x007c000, 0x0080000, 0x0088000, 0x0090000,
    0x0098000, 0x00a0000, 0x00a8000, 0x00b0000, 0x00b8000, 0x00c0000, 0x00c8000,
    0x00d0000, 0x00d8000, 0x00e0000, 0x00e8000, 0x00f0000, 0x00f8000, 0x0100000,
    0x0110000, 0x0120000, 0x0130000, 0x0140000, 0x0150000, 0x0160000, 0x0170000,
    0x0180000, 0x0190000, 0x01a0000, 0x01b0000, 0x01c0000, 0x01d0000, 0x01e0000,
    0x01f0000, 0x0200000, 0x0220000, 0x0240000, 0x0260000, 0x0280000, 0x02a0000,
    0x02c0000, 0x02e0000, 0x0300000, 0x0320000, 0x0340000, 0x0360000, 0x0380000,
    0x03a0000, 0x03c0000, 0x03e0000, 0x0400000, 0x0440000, 0x0480000, 0x04c0000,
    0x0500000, 0x0540000, 0x0580000, 0x05c0000, 0x0600000, 0x0640000, 0x0680000,
    0x06c0000, 0x0700000, 0x0740000, 0x0780000, 0x07c0000, 0x0800000, 0x0880000,
    0x0900000, 0x0980000, 0x0a00000, 0x0a80000, 0x0b00000, 0x0b80000, 0x0c00000,
    0x0c80000, 0x0d00000, 0x0d80000, 0x0e00000, 0x0e80000, 0x0f00000, 0x0f80000,
    0x1000000, 0x1100000, 0x1200000, 0x1300000, 0x1400000, 0x1500000, 0x1600000,
    0x1700000, 0x1800000, 0x1900000, 0x1a00000, 0x1b00000, 0x1c00000, 0x1d00000,
    0x1e00000, 0x1f00000, 0x2000000, 0x2200000, 0x2400000, 0x2600000, 0x2800000,
    0x2a00000, 0x2c00000, 0x2e00000, 0x3000000, 0x3200000, 0x3400000, 0x3600000,
    0x3800000, 0x3a00000, 0x3c00000, 0x3e00000};

static constexpr int32_t env_incr_table[] = {
    0x000000, 0x000048, 0x000050, 0x000058, 0x000060, 0x000068, 0x000070,
    0x000078, 0x000080, 0x000090, 0x0000a0, 0x0000b0, 0x0000c0, 0x0000d0,
    0x0000e0, 0x0000f0, 0x000100, 0x000120, 0x000140, 0x000160, 0x000180,
    0x0001a0, 0x0001c0, 0x0001e0, 0x000200, 0x000240, 0x000280, 0x0002c0,
    0x000300, 0x000340, 0x000380, 0x0003c0, 0x000400, 0x000480, 0x000500,
    0x000580, 0x000600, 0x000680, 0x000700, 0x000780, 0x000800, 0x000900,
    0x000a00, 0x000b00, 0x000c00, 0x000d00, 0x000e00, 0x000f00, 0x001000,
    0x001200, 0x001400, 0x001600, 0x001800, 0x001a00, 0x001c00, 0x001e00,
    0x002000, 0x002400, 0x002800, 0x002c00, 0x003000, 0x003400, 0x003800,
    0x003c00, 0x004000, 0x004800, 0x005000, 0x005800, 0x006000, 0x006800,
    0x007000, 0x007800, 0x008000, 0x009000, 0x00a000, 0x00b000, 0x00c000,
    0x00d000, 0x00e000, 0x00f000, 0x010000, 0x012000, 0x014000, 0x016000,
    0x018000, 0x01a000, 0x01c000, 0x01e000, 0x020000, 0x024000, 0x028000,
    0x02c000, 0x030000, 0x034000, 0x038000, 0x03c000, 0x040000, 0x048000,
    0x050000, 0x058000, 0x060000, 0x068000, 0x070000, 0x078000, 0x080000,
    0x090000, 0x0a0000, 0x0b0000, 0x0c0000, 0x0d0000, 0x0e0000, 0x0f0000,
    0x100000, 0x120000, 0x140000, 0x160000, 0x180000, 0x1a0000, 0x1c0000,
    0x1e0000, 0x200000, 0x240000, 0x280000, 0x2c0000, 0x300000, 0x340000,
    0x380000, 0x3c0000, 0x40000,  0x3c000,  0x38000,  0x34000,  0x30000,
    0x2c000,  0x28000,  0x24000,  0x20000,  0x1e000,  0x1c000,  0x1a000,
    0x18000,  0x16000,  0x14000,  0x12000,  0x10000,  0x0f000,  0x0e000,
    0x0d000,  0x0c000,  0x0b000,  0x0a000,  0x09000,  0x08000,  0x07800,
    0x07000,  0x06800,  0x06000,  0x05800,  0x05000,  0x04800,  0x04000,
    0x03c00,  0x03800,  0x03400,  0x03000,  0x02c00,  0x02800,  0x02400,
    0x02000,  0x01e00,  0x01c00,  0x01a00,  0x01800,  0x01600,  0x01400,
    0x01200,  0x01000,  0x00f00,  0x00e00,  0x00d00,  0x00c00,  0x00b00,
    0x00a00,  0x00900,  0x00800,  0x00780,  0x00700,  0x00680,  0x00600,
    0x00580,  0x00500,  0x00480,  0x00400,  0x003c0,  0x00380,  0x00340,
    0x00300,  0x002c0,  0x00280,  0x00240,  0x00200,  0x001e0,  0x001c0,
    0x001a0,  0x00180,  0x00160,  0x00140,  0x00120,  0x00100,  0x000f0,
    0x000e0,  0x000d0,  0x000c0,  0x000b0,  0x000a0,  0x00090,  0x00080,
    0x00078,  0x00070,  0x00068,  0x00060,  0x00058,  0x00050,  0x00048,
    0x00040,  0x0003c,  0x00038,  0x00034,  0x00030,  0x0002c,  0x00028,
    0x00024,  0x00020,  0x0001e,  0x0001c,  0x0001a,  0x00018,  0x00016,
    0x00014,  0x00012,  0x00010,  0x0000f,  0x0000e,  0x0000d,  0x0000c,
    0x0000b,  0x0000a,  0x00009,  0x00008,  0x00007,  0x00006,  0x00005,
    0x00004,  0x00003,  0x00002,  0x00001};

static constexpr int32_t interp_lut[3][128] = {
    {3385, 3401, 3417, 3432, 3448, 3463, 3478, 3492, 3506, 3521, 3534, 3548, 3562, 3575, 3588, 3601,
    3614, 3626, 3638, 3650, 3662, 3673, 3685, 3696, 3707, 3718, 3728, 3739, 3749, 3759, 3768, 3778,
    3787, 3796, 3805, 3814, 3823, 3831, 3839, 3847, 3855, 3863, 3870, 3878, 3885, 3892, 3899, 3905,
    3912, 3918, 3924, 3930, 3936, 3942, 3948, 3953, 3958, 3963, 3968, 3973, 3978, 3983, 3987, 3991,
    3995, 4000, 4004, 4007, 4011, 4015, 4018, 4022, 4025, 4028, 4031, 4034, 4037, 4040, 4042, 4045,
    4047, 4050, 4052, 4054, 4057, 4059, 4061, 4063, 4064, 4066, 4068, 4070, 4071, 4073, 4074, 4076,
    4077, 4078, 4079, 4081, 4082, 4083, 4084, 4085, 4086, 4086, 4087, 4088, 4089, 4089, 4090, 4091,
    4091, 4092, 4092, 4093, 4093, 4094, 4094, 4094, 4094, 4095, 4095, 4095, 4095, 4095, 4095, 4095},

    {710, 726, 742, 758, 775, 792, 809, 826, 844, 861, 879, 897, 915, 933, 952, 971,
    990, 1009, 1028, 1047, 1067, 1087, 1106, 1126, 1147, 1167, 1188, 1208, 1229, 1250, 1271, 1292,
    1314, 1335, 1357, 1379, 1400, 1423, 1445, 1467, 1489, 1512, 1534, 1557, 1580, 1602, 1625, 1648,
    1671, 1695, 1718, 1741, 1764, 1788, 1811, 1835, 1858, 1882, 1906, 1929, 1953, 1977, 2000, 2024,
    2048, 2069, 2095, 2119, 2143, 2166, 2190, 2214, 2237, 2261, 2284, 2308, 2331, 2355, 2378, 2401,
    2425, 2448, 2471, 2494, 2517, 2539, 2562, 2585, 2607, 2630, 2652, 2674, 2696, 2718, 2740, 2762,
    2783, 2805, 2826, 2847, 2868, 2889, 2910, 2931, 2951, 2971, 2991, 3011, 3031, 3051, 3070, 3089,
    3108, 3127, 3146, 3164, 3182, 3200, 3218, 3236, 3253, 3271, 3288, 3304, 3321, 3338, 3354, 3370},

    {0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 8, 8, 9, 10, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 22, 23, 24, 26, 27, 29, 30, 32, 34, 36, 38, 40, 42, 44, 46,
    49, 51, 53, 56, 59, 62, 65, 68, 71, 74, 77, 81, 84, 88, 92, 96,
    100, 104, 109, 113, 118, 122, 127, 132, 137, 143, 148, 154, 160, 165, 171, 178,
    184, 191, 197, 204, 211, 219, 226, 234, 241, 249, 257, 266, 274, 283, 292, 301,
    310, 319, 329, 339, 349, 359, 369, 380, 391, 402, 413, 424, 436, 448, 460, 472,
    484, 497, 510, 523, 536, 549, 563, 577, 591, 605, 619, 634, 648, 663, 679, 694},
};

inline uint32_t addclip20(uint32_t add1, uint32_t add2, uint32_t cin)
{
    uint32_t sum = (add1 + add2 + cin) & 0xfffff;
    if ((add1 & 0x80000) != 0 && (add2 & 0x80000) != 0 && (sum & 0x80000) == 0)
        sum = 0x80000;
    else if ((add1 & 0x80000) == 0 && (add2 & 0x80000) == 0 && (sum & 0x80000) != 0)
        sum = 0x7ffff;
    return sum;
}

inline int32_t multi(int32_t val1, int8_t val2)
{
    if (val1 & 0x80000)
        val1 |= ~0xfffff;
    else
        val1 &= 0x7ffff;

    val1 *= val2;
    if (val1 & 0x8000000)
        val1 |= ~0x1ffffff;
    else
        val1 &= 0x1ffffff;
    return val1;
}

DEFINE_DEVICE_TYPE(MB87419_MB87420, mb87419_mb87420_device, "mb87419_mb87420", "Roland LP MB87419/MB87420 PCM")

mb87419_mb87420_device::mb87419_mb87420_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, MB87419_MB87420, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_rom_interface(mconfig, *this)
	, m_int_callback(*this)
	, m_clock(0)
	, m_rate(0)
	, m_stream(nullptr)
	, m_sel_chn(0)
	, m_irq_queue_read(0)
	, m_irq_queue_write(0)
	, m_irq_queue_count(0)
	, m_irq_current_valid(false)
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void mb87419_mb87420_device::device_start()
{
	m_clock = clock() / 2;
	m_rate = m_clock / 512; // usually 32 KHz

	// The physical DA pin is a time-division stream containing one slot per
	// voice.  Keep those slots separate so a downstream RCC implementation can
	// route and process them before producing the six analogue outputs.
	m_stream = stream_alloc(0, NUM_CHANNELS, m_rate, STREAM_SYNCHRONOUS);

	save_item(STRUCT_MEMBER(m_chns, volume_cur));
	save_item(STRUCT_MEMBER(m_chns, bank_loopmode));
	save_item(STRUCT_MEMBER(m_chns, step));
	save_item(STRUCT_MEMBER(m_chns, volume_incr));
	save_item(STRUCT_MEMBER(m_chns, volume_dest));
	save_item(STRUCT_MEMBER(m_chns, addr));
	save_item(STRUCT_MEMBER(m_chns, end));
	save_item(STRUCT_MEMBER(m_chns, loop));
	save_item(STRUCT_MEMBER(m_chns, enable));
	save_item(STRUCT_MEMBER(m_chns, play_dir));
	save_item(STRUCT_MEMBER(m_chns, irq));
	save_item(STRUCT_MEMBER(m_chns, tempReference));
	save_item(NAME(m_sel_chn));
	save_item(NAME(m_int_channel));
	save_item(NAME(m_irq_queue));
	save_item(NAME(m_irq_queue_read));
	save_item(NAME(m_irq_queue_write));
	save_item(NAME(m_irq_queue_count));
	save_item(NAME(m_irq_current_valid));
	save_item(NAME(m_readback));
	save_item(NAME(m_sound_io_buffer));

	logerror("Roland PCM: Clock %u, Rate %u\n", m_clock, m_rate);
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void mb87419_mb87420_device::device_reset()
{
	for (auto &channel : m_chns)
		channel = pcm_channel();
	m_sel_chn = 0;
	m_int_channel = 0;
	std::fill(std::begin(m_irq_queue), std::end(m_irq_queue), 0);
	m_irq_queue_read = 0;
	m_irq_queue_write = 0;
	m_irq_queue_count = 0;
	m_irq_current_valid = false;
	m_readback = 0;
	std::fill(std::begin(m_sound_io_buffer), std::end(m_sound_io_buffer), 0);
	m_int_callback(CLEAR_LINE);
}

//-------------------------------------------------
//  rom_bank_pre_change - refresh the stream if the
//  ROM banking changes
//-------------------------------------------------

void mb87419_mb87420_device::rom_bank_pre_change()
{
	// unused right now
	m_stream->update();
}


uint8_t mb87419_mb87420_device::read(offs_t offset)
{
	m_stream->update();
	offset &= 0x3;
	switch (offset)
	{
	case 0:
		{
			uint8_t const result = m_int_channel;
			if (!machine().side_effects_disabled() && m_irq_current_valid)
			{
				if (m_irq_queue_count)
				{
					m_int_channel = m_irq_queue[m_irq_queue_read];
					m_irq_queue_read = (m_irq_queue_read + 1) % NUM_CHANNELS;
					m_irq_queue_count--;
					m_int_callback(CLEAR_LINE);
					m_int_callback(ASSERT_LINE);
				}
				else
				{
					m_irq_current_valid = false;
				}
			}
			return result;
		}
	case 1:
		{
			offs_t bank = m_sound_io_buffer[0x03] & 0x3c;
			offs_t addr = get_u24le(&m_sound_io_buffer[0x09]);
			addr = ((addr >> 6) + 2) & 0x3ffff;
			addr |= (bank << 16);
			// printf("LP read rom\n");
			return read_byte(addr);
		}
	case 2:
		// printf("LP read rb lsb %04x\n", m_readback);
		return (m_readback >> 0) & 0xFF; // LSB
	case 3:
		// printf("LP read rb msb %04x\n", m_readback);
		return (m_readback >> 8) & 0xFF; // MSB
	}

	return 0xff;
}

void mb87419_mb87420_device::write(offs_t offset, uint8_t data, offs_t pc)
{
	m_stream->update();
	m_sound_io_buffer[offset] = data;

	if (offset < 0x10)
	{
		// printf("%04x: LP w %02X %02X (ch %02x)\n", pc, offset, data, m_sel_chn);
		pcm_channel& chn = m_chns[m_sel_chn];
		switch(offset)
		{
		case 0x00:
			// The firmware writes each 16-bit LP word high byte first.  The
			// low-byte write commits the complete word to the live register.
			// Applying both writes independently produces torn pitch/address
			// values at audio-clock boundaries.
			chn.volume_cur = (chn.volume_cur & 0x03ff0000) | get_u16le(&m_sound_io_buffer[0x00]);
			LOGMASKED(LOG_REGISTERS, "%04x: LP w volume_cur0 = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		case 0x01:
			LOGMASKED(LOG_REGISTERS, "%04x: LP w volume_cur1 = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		case 0x02:
			chn.volume_cur = (chn.volume_cur & 0x0000ffff)
				| (int32_t(m_sound_io_buffer[0x02] | ((m_sound_io_buffer[0x03] & 0x03) << 8)) << 16);
			chn.bank_loopmode = m_sound_io_buffer[0x03] & 0xfc;
			LOGMASKED(LOG_PITCH, "LPTRACE t=%.6f mode ch=%02x value=%02x\n", machine().time().as_double(), m_sel_chn, chn.bank_loopmode);
			LOGMASKED(LOG_REGISTERS, "%04x: LP w volume_cur2 = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		case 0x03:
			LOGMASKED(LOG_REGISTERS, "%04x: LP w volume_cur3 = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;

		case 0x04:  // sample step LSB
			chn.step = get_u16le(&m_sound_io_buffer[0x04]);
			LOGMASKED(LOG_PITCH, "LPTRACE t=%.9f pc=%04x step ch=%02x value=%04x\n", machine().time().as_double(), pc, m_sel_chn, chn.step);
			LOGMASKED(LOG_REGISTERS, "%04x: LP w step_lsb = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		case 0x05:  // sample step MSB
			LOGMASKED(LOG_REGISTERS, "%04x: LP w step_msb = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;

		case 0x06:  // volume incr
		{
			// The firmware mirrors register writes.  Only a changed envelope
			// program starts a new segment; treating an identical rewrite as a
			// new zero-distance segment causes a self-sustaining IRQ loop.
			bool const changed = chn.volume_incr != m_sound_io_buffer[0x06]
				|| chn.volume_dest != m_sound_io_buffer[0x07];
			chn.volume_incr = m_sound_io_buffer[0x06];
			chn.volume_dest = m_sound_io_buffer[0x07];
			if (chn.enable && changed)
				chn.irq = true;
			LOGMASKED(LOG_REGISTERS, "%04x: LP w vol_incr = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		}
		case 0x07:  // volume dest
			LOGMASKED(LOG_REGISTERS, "%04x: LP w vol_dest = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;

		case 0x08:  // current address, fraction LSB
			chn.addr = (chn.addr & 0xffff0000U) | get_u16le(&m_sound_io_buffer[0x08]);
			LOGMASKED(LOG_REGISTERS, "%04x: LP w addr0 = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		case 0x09:  // current address, fraction MSB
			LOGMASKED(LOG_REGISTERS, "%04x: LP w addr1 = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		case 0x0A:  // current address LSB
			chn.addr = (chn.addr & 0x0000ffffU) | (uint32_t(get_u16le(&m_sound_io_buffer[0x0a])) << 16);
			LOGMASKED(LOG_REGISTERS, "%04x: LP w addr2 = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		case 0x0B:  // current address MSB
			LOGMASKED(LOG_REGISTERS, "%04x: LP w addr3 = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;

		case 0x0C:  // sample end address, LSB
			chn.end = get_u16le(&m_sound_io_buffer[0x0c]);
			LOGMASKED(LOG_REGISTERS, "%04x: LP w end_lsb = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		case 0x0D:  // sample end address MSB
			LOGMASKED(LOG_REGISTERS, "%04x: LP w end_msb = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;

		case 0x0E:  // sample loop address LSB
			chn.loop = get_u16le(&m_sound_io_buffer[0x0e]);
			LOGMASKED(LOG_REGISTERS, "%04x: LP w loop_lsb = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		case 0x0F:  // sample loop address MSB
			LOGMASKED(LOG_REGISTERS, "%04x: LP w loop_msb = %02X (ch %02x)\n", pc, data, m_sel_chn);
			break;
		}
	}
	else
	{
		// printf("%04x: LP w %02X %02X\n", pc, offset, data);
		switch(offset)
		{
		case 0x11:
		case 0x13:
		case 0x15:
		case 0x17:
			{
				// voice mask, reset state
				uint8_t basechn = ((offset >> 1) & 0x03) * 8;
				for (uint8_t ch_id = 0; ch_id < 8; ch_id ++)
				{
					pcm_channel& chn = m_chns[basechn + ch_id];
					bool play = static_cast<bool>((data >> ch_id) & 1);

					if (play && !chn.enable)
					{
						chn.play_dir = 0;
						chn.irq = true;
						chn.tempReference = 0;
					}
					chn.enable = play;
				}
			}
			break;

		case 0x10:
			// read after note off
			m_readback = m_chns[data & 0x1f].volume_cur & 0xffff;
			// m_readback = 0;
			LOGMASKED(LOG_REGISTERS, "read vol lsb v%02x %04x\n", data, m_readback);
			break;
		case 0x12:
			// read after note off
			m_readback = ((m_chns[data & 0x1f].volume_cur >> 16) & 0x03ff) | (m_chns[data & 0x1f].bank_loopmode << 8);
			// m_readback = 0;
			LOGMASKED(LOG_REGISTERS, "read vol msb v%02x %04x\n", data, m_readback);
			break;
		case 0x14:
			m_readback = m_chns[data & 0x1f].step;
			break;
		case 0x16:
			m_readback = (m_chns[data & 0x1f].volume_incr << 0) | (m_chns[data & 0x1f].volume_dest << 8);
			// m_readback = 0;
			LOGMASKED(LOG_REGISTERS, "read env v%02x %04x\n", data, m_readback);
			break;
		case 0x18:
			m_readback = m_chns[data & 0x1f].addr & 0xffff;
			break;
		case 0x1A:
			// used for detecting when to apply loop fine tune
			m_readback = (m_chns[data & 0x1f].addr >> 16) & 0xffff;
			LOGMASKED(LOG_PITCH, "LPTRACE t=%.6f phase ch=%02x value=%04x\n", machine().time().as_double(), data & 0x1f, m_readback);
			// m_readback = 0;
			LOGMASKED(LOG_REGISTERS, "read phase v%02x %04x\n", data, m_readback);
			break;
		case 0x1C:
			m_readback = m_chns[data & 0x1f].end;
			break;
		case 0x1E:
			m_readback = m_chns[data & 0x1f].loop;
			break;

		case 0x19:
		case 0x1B:
		case 0x1D:
			logerror("LP config %02X = %02X\n", offset, data);
			break;
		case 0x1F:
			m_sel_chn = data & 0x1f;
			break;
		default:
			logerror("Writing unknown reg %02X = %02X\n", offset, data);
			break;
		}
	}
}

//-------------------------------------------------
//  sound_stream_update - handle a stream update
//-------------------------------------------------

void mb87419_mb87420_device::sound_stream_update(sound_stream &stream)
{
	for (int smpl = 0; smpl < stream.samples(); smpl++)
	{
		for (int i = 0; i < NUM_CHANNELS; i++)
		{
			auto &chn = m_chns[i];

			if (!chn.enable)
			{
				stream.put(i, smpl, 0.0F);
				continue;
			}

			// Address gen
			uint32_t hiphase = chn.addr >> 14;
			uint32_t sub_phase = chn.addr & 0x3fff;
			[[maybe_unused]] int interp_ratio = (sub_phase >> 7) & 127;
			sub_phase += chn.step;
			int sub_phase_of = (sub_phase >> 14) & 7;
			chn.addr = (hiphase << 14) | (sub_phase & 0x3fff);

			uint32_t hiaddr = (chn.bank_loopmode & 0x3C) >> 2 << 18;
			bool altLoopState = chn.play_dir != 0;
			bool altLoop = (chn.bank_loopmode & 0x80) != 0;
			bool backwardsPlay = (chn.bank_loopmode & 0x40) != 0;
			bool kon = false;
			uint32_t address_loop = ((uint32_t)chn.loop << 16) >> 14;
			uint32_t address_end = ((uint32_t)chn.end << 16) >> 14;

			// address 0
			uint32_t address_cnt = hiphase;
			int samp0 = decode_sample((int8_t)read_byte(hiaddr | address_cnt));

			uint32_t cmp1 = hiphase;
			uint32_t cmp2 = address_cnt;
			int nibble_cmp2 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 8
			cmp1 = altLoopState ? address_loop : address_end;
			cmp2 = address_cnt;
			int address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 9

			[[maybe_unused]] uint32_t next_address = address_cnt; // 11
			[[maybe_unused]] int usenew = !nibble_cmp2;
			int next_altLoopState = altLoopState;

			cmp1 = (!altLoop && address_cmp) ? address_loop : address_cnt;
			cmp2 = address_cnt;
			uint32_t address_cnt2 = (kon || (!altLoop && address_cmp)) ? cmp1 : cmp2;

			int address_add = (!address_cmp && altLoop && !altLoopState) || (!address_cmp && !altLoop);
			int address_sub = !address_cmp && altLoop && altLoopState;
			if (backwardsPlay)
					address_cnt2 -= address_add - address_sub;
			else
					address_cnt2 += address_add - address_sub;
			address_cnt = address_cnt2 & 0x3ffff; // 11
			altLoopState = altLoop && (altLoopState ^ address_cmp); // 11

			[[maybe_unused]] int samp1 = decode_sample((int8_t)read_byte(hiaddr | address_cnt));

			cmp1 = hiphase;
			cmp2 = address_cnt;
			int nibble_cmp3 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 12
			cmp1 = altLoopState ? address_loop : address_end;
			cmp2 = address_cnt;
			address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 13

			if (sub_phase_of >= 1)
			{
					next_address = address_cnt; // 13
					usenew = !nibble_cmp3;
					next_altLoopState = altLoopState;
			}

			cmp1 = (!altLoop && address_cmp) ? address_loop : address_cnt;
			cmp2 = address_cnt;
			address_cnt2 = (kon || (!altLoop && address_cmp)) ? cmp1 : cmp2;

			address_add = (!address_cmp && altLoop && !altLoopState) || (!address_cmp && !altLoop);
			address_sub = !address_cmp && altLoop && altLoopState;
			if (backwardsPlay)
					address_cnt2 -= address_add - address_sub;
			else
					address_cnt2 += address_add - address_sub;
			address_cnt = address_cnt2 & 0x3ffff; // 15
			altLoopState = altLoop && (altLoopState ^ address_cmp); // 15

			[[maybe_unused]] int samp2 = decode_sample((int8_t)read_byte(hiaddr | address_cnt));

			cmp1 = hiphase;
			cmp2 = address_cnt;
			int nibble_cmp4 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 16
			cmp1 = altLoopState ? address_loop : address_end;
			cmp2 = address_cnt;
			address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 17

			if (sub_phase_of >= 2)
			{
					next_address = address_cnt; // 17
					usenew = !nibble_cmp4;
					next_altLoopState = altLoopState;
			}

			cmp1 = (!altLoop && address_cmp) ? address_loop : address_cnt;
			cmp2 = address_cnt;
			address_cnt2 = (kon || (!altLoop && address_cmp)) ? cmp1 : cmp2;

			address_add = (!address_cmp && altLoop && !altLoopState) || (!address_cmp && !altLoop);
			address_sub = !address_cmp && altLoop && altLoopState;
			if (backwardsPlay)
					address_cnt2 -= address_add - address_sub;
			else
					address_cnt2 += address_add - address_sub;
			address_cnt = address_cnt2 & 0x3ffff; // 19
			altLoopState = altLoop && (altLoopState ^ address_cmp); // 19

			[[maybe_unused]] int samp3 = decode_sample((int8_t)read_byte(hiaddr | address_cnt));

			cmp1 = hiphase;
			cmp2 = address_cnt;
			int nibble_cmp5 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 20
			cmp1 = altLoopState ? address_loop : address_end;
			cmp2 = address_cnt;
			address_cmp = (cmp1 & 0xfffff) == (cmp2 & 0xfffff); // 21

			if (sub_phase_of >= 3)
			{
					next_address = address_cnt; // 21
					usenew = !nibble_cmp5;
					next_altLoopState = altLoopState;
			}

			cmp1 = (!altLoop && address_cmp) ? address_loop : address_cnt;
			cmp2 = address_cnt;
			address_cnt2 = (kon || (!altLoop && address_cmp)) ? cmp1 : cmp2;

			address_add = (!address_cmp && altLoop && !altLoopState) || (!address_cmp && !altLoop);
			address_sub = !address_cmp && altLoop && altLoopState;
			if (backwardsPlay)
					address_cnt2 -= address_add - address_sub;
			else
					address_cnt2 += address_add - address_sub;
			address_cnt = address_cnt2 & 0x3ffff; // 23
			// altLoopState = altLoop && (altLoopState ^ address_cmp); // 23

			cmp1 = hiphase;
			cmp2 = address_cnt;
			int nibble_cmp6 = (cmp1 & 0xffff0) == (cmp2 & 0xffff0); // 24

			if (sub_phase_of >= 4)
			{
					next_address = address_cnt; // 1
					usenew = !nibble_cmp6;
					// altLoopState is not updated?
			}

			chn.addr = (next_address << 14) | (sub_phase & 0x3fff);
			chn.play_dir = next_altLoopState;

			// dpcm

			// 18
			int reference = chn.tempReference;

			int32_t limit_pos = 262143; // 18 bit
			int32_t limit_neg = -262144;

			// 19
			if (sub_phase_of >= 1)
				reference = std::clamp(reference + samp0, limit_neg, limit_pos);
			if (sub_phase_of >= 2)
				reference = std::clamp(reference + samp1, limit_neg, limit_pos);
			if (sub_phase_of >= 3)
				reference = std::clamp(reference + samp2, limit_neg, limit_pos);
			if (sub_phase_of >= 4)
				reference = std::clamp(reference + samp3, limit_neg, limit_pos);


			// reference >>= 4;

			// interpolation

			int test = chn.tempReference;

			int step0 = ((interp_lut[0][interp_ratio] << 0) * samp0) >> 12;
			test = std::clamp(test + step0, limit_neg, limit_pos);
			int step1 = ((interp_lut[1][interp_ratio] << 0) * samp1) >> 12;
			test = std::clamp(test + step1, limit_neg, limit_pos);
			int step2 = ((interp_lut[2][interp_ratio] << 0) * samp2) >> 12;
			test = std::clamp(test + step2, limit_neg, limit_pos);

			chn.tempReference = reference;


			// Envelope
			int32_t vol_dest_norm = env_limit_table[chn.volume_dest];
			int32_t vol_incr = env_incr_table[chn.volume_incr];
			bool incrDown = chn.volume_incr & 0x80;
			bool incrUp = !incrDown;
			if (incrDown) vol_incr = -vol_incr;

			bool volIncrement = chn.volume_cur < vol_dest_norm && incrUp;
			bool volDecrement = chn.volume_cur > vol_dest_norm && incrDown;
			bool const segment_active = chn.irq;
			if (volIncrement || volDecrement)
			{
				chn.volume_cur = chn.volume_cur + vol_incr;
				bool overshoot = (chn.volume_cur < vol_dest_norm && incrDown)
					|| (chn.volume_cur > vol_dest_norm && incrUp);
				if (overshoot)
				{
					chn.volume_cur = vol_dest_norm;
					chn.irq = false;
					signal_envelope_complete(i);
				}
				else
				{
					chn.irq = true;
				}
			}
			else
			{
				chn.irq = false;
				if (segment_active)
					signal_envelope_complete(i);
			}
			// volume
			s32 smp_data = (s64(test) * (chn.volume_cur >> 10)) >> 12;
			stream.put(i, smpl, float(smp_data) / (1 << 18));
		}
	}

	return;
}

void mb87419_mb87420_device::signal_envelope_complete(uint8_t channel)
{
	if (!m_irq_current_valid)
	{
		m_int_channel = channel;
		m_irq_current_valid = true;
		// /INT is active-low and the 8098 EXTINT input detects rising edges.
		m_int_callback(CLEAR_LINE);
		m_int_callback(ASSERT_LINE);
	}
	else if (m_irq_queue_count < NUM_CHANNELS)
	{
		m_irq_queue[m_irq_queue_write] = channel;
		m_irq_queue_write = (m_irq_queue_write + 1) % NUM_CHANNELS;
		m_irq_queue_count++;
	}
	else
	{
		logerror("Roland PCM: envelope completion queue overflow\n");
	}
}

int16_t mb87419_mb87420_device::decode_sample(int8_t data)
{
	int16_t val;
	int16_t sign;
	uint8_t shift;
	int16_t result;

	if (data < 0)
	{
		sign = -1;
		val = -data;
	}
	else
	{
		sign = +1;
		val = data;
	}

	// thanks to Sarayan for figuring out the decoding formula
	shift = val >> 4;
	val &= 0x0F;
	if (!shift)
		result = val;
	else
		result = (0x10 + val) << (shift - 1);
	return result * sign;
}
