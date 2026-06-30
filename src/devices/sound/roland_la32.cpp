// license:BSD-3-Clause
// copyright-holders:nukeykt

#include "emu.h"
#include "roland_la32.h"

DEFINE_DEVICE_TYPE(LA32, la32_device, "la32", "Roland LA32")

static u16 powrom[512] = {
	0, 6, 11, 17, 22, 28, 33, 39, 45, 50, 56, 61, 67, 73, 78, 84,
	90, 95, 101, 107, 112, 118, 124, 130, 135, 141, 147, 152, 158, 164, 170, 176,
	181, 187, 193, 199, 205, 210, 216, 222, 228, 234, 240, 246, 251, 257, 263, 269,
	275, 281, 287, 293, 299, 305, 311, 317, 323, 329, 335, 341, 347, 353, 359, 365,
	371, 377, 383, 389, 395, 401, 407, 413, 419, 425, 432, 438, 444, 450, 456, 462,
	469, 475, 481, 487, 493, 500, 506, 512, 518, 524, 531, 537, 543, 550, 556, 562,
	568, 575, 581, 587, 594, 600, 607, 613, 619, 626, 632, 638, 645, 651, 658, 664,
	671, 677, 684, 690, 696, 703, 709, 716, 723, 729, 736, 742, 749, 755, 762, 768,
	775, 782, 788, 795, 801, 808, 815, 821, 828, 835, 841, 848, 855, 861, 868, 875,
	882, 888, 895, 902, 909, 915, 922, 929, 936, 943, 949, 956, 963, 970, 977, 984,
	991, 998, 1004, 1011, 1018, 1025, 1032, 1039, 1046, 1053, 1060, 1067, 1074, 1081, 1088, 1095,
	1102, 1109, 1116, 1123, 1130, 1137, 1144, 1152, 1159, 1166, 1173, 1180, 1187, 1194, 1201, 1209,
	1216, 1223, 1230, 1237, 1245, 1252, 1259, 1266, 1274, 1281, 1288, 1296, 1303, 1310, 1317, 1325,
	1332, 1340, 1347, 1354, 1362, 1369, 1376, 1384, 1391, 1399, 1406, 1414, 1421, 1429, 1436, 1444,
	1451, 1459, 1466, 1474, 1481, 1489, 1496, 1504, 1511, 1519, 1527, 1534, 1542, 1550, 1557, 1565,
	1572, 1580, 1588, 1596, 1603, 1611, 1619, 1626, 1634, 1642, 1650, 1658, 1665, 1673, 1681, 1689,
	1697, 1704, 1712, 1720, 1728, 1736, 1744, 1752, 1760, 1768, 1776, 1784, 1791, 1799, 1807, 1815,
	1823, 1831, 1840, 1848, 1856, 1864, 1872, 1880, 1888, 1896, 1904, 1912, 1920, 1929, 1937, 1945,
	1953, 1961, 1969, 1978, 1986, 1994, 2002, 2011, 2019, 2027, 2036, 2044, 2052, 2060, 2069, 2077,
	2086, 2094, 2102, 2111, 2119, 2128, 2136, 2144, 2153, 2161, 2170, 2178, 2187, 2195, 2204, 2212,
	2221, 2229, 2238, 2247, 2255, 2264, 2272, 2281, 2290, 2298, 2307, 2316, 2324, 2333, 2342, 2350,
	2359, 2368, 2377, 2385, 2394, 2403, 2412, 2421, 2430, 2438, 2447, 2456, 2465, 2474, 2483, 2492,
	2501, 2510, 2518, 2527, 2536, 2545, 2554, 2563, 2572, 2581, 2590, 2600, 2609, 2618, 2627, 2636,
	2645, 2654, 2663, 2672, 2682, 2691, 2700, 2709, 2718, 2728, 2737, 2746, 2755, 2765, 2774, 2783,
	2793, 2802, 2811, 2821, 2830, 2839, 2849, 2858, 2868, 2877, 2887, 2896, 2905, 2915, 2924, 2934,
	2943, 2953, 2963, 2972, 2982, 2991, 3001, 3010, 3020, 3030, 3039, 3049, 3059, 3068, 3078, 3088,
	3098, 3107, 3117, 3127, 3137, 3146, 3156, 3166, 3176, 3186, 3196, 3206, 3215, 3225, 3235, 3245,
	3255, 3265, 3275, 3285, 3295, 3305, 3315, 3325, 3335, 3345, 3355, 3365, 3376, 3386, 3396, 3406,
	3416, 3426, 3436, 3447, 3457, 3467, 3477, 3488, 3498, 3508, 3518, 3529, 3539, 3549, 3560, 3570,
	3581, 3591, 3601, 3612, 3622, 3633, 3643, 3654, 3664, 3675, 3685, 3696, 3706, 3717, 3727, 3738,
	3749, 3759, 3770, 3781, 3791, 3802, 3813, 3823, 3834, 3845, 3856, 3866, 3877, 3888, 3899, 3910,
	3920, 3931, 3942, 3953, 3964, 3975, 3986, 3997, 4008, 4019, 4030, 4041, 4052, 4063, 4074, 4085,
};

static u8 powdrom[512] = {
	6, 5, 6, 5, 6, 5, 6, 6, 5, 6, 5, 6, 6, 5, 6, 6,
	5, 6, 6, 5, 6, 6, 6, 5, 6, 6, 5, 6, 6, 6, 6, 5,
	6, 6, 6, 6, 5, 6, 6, 6, 6, 6, 6, 5, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 6, 6, 6, 6, 6, 7,
	6, 6, 6, 6, 7, 6, 6, 6, 6, 7, 6, 6, 7, 6, 6, 6,
	7, 6, 6, 7, 6, 7, 6, 6, 7, 6, 6, 7, 6, 7, 6, 7,
	6, 7, 6, 6, 7, 6, 7, 7, 6, 7, 6, 7, 6, 7, 6, 7,
	7, 6, 7, 6, 7, 7, 6, 7, 7, 6, 7, 7, 6, 7, 7, 7,
	6, 7, 7, 7, 6, 7, 7, 7, 7, 6, 7, 7, 7, 7, 7, 7,
	7, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 8, 7, 7, 7, 7, 7, 7, 7, 8, 7,
	7, 7, 7, 8, 7, 7, 7, 8, 7, 7, 8, 7, 7, 7, 8, 7,
	8, 7, 7, 8, 7, 7, 8, 7, 8, 7, 8, 7, 8, 7, 8, 7,
	8, 7, 8, 7, 8, 7, 8, 7, 8, 8, 7, 8, 8, 7, 8, 7,
	8, 8, 8, 7, 8, 8, 7, 8, 8, 8, 8, 7, 8, 8, 8, 8,
	7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 8, 8, 8, 8,
	8, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 8, 8, 8,
	8, 8, 9, 8, 8, 8, 9, 8, 8, 9, 8, 8, 8, 9, 8, 9,
	8, 8, 9, 8, 9, 8, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9,
	8, 9, 9, 8, 9, 8, 9, 9, 8, 9, 9, 8, 9, 9, 8, 9,
	9, 9, 8, 9, 9, 9, 9, 9, 8, 9, 9, 9, 9, 9, 9, 9,
	9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 10, 9, 9, 9, 9, 9,
	9, 9, 9, 10, 9, 9, 9, 9, 10, 9, 9, 9, 10, 9, 9, 10,
	9, 9, 10, 9, 9, 10, 9, 10, 9, 10, 9, 9, 10, 9, 10, 9,
	10, 10, 9, 10, 9, 10, 9, 10, 10, 9, 10, 10, 9, 10, 10, 10,
	9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10,
	10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 10, 10, 10, 10,
	10, 10, 11, 10, 10, 10, 11, 10, 10, 10, 11, 10, 10, 11, 10, 11,
	10, 10, 11, 10, 11, 10, 11, 10, 11, 10, 11, 10, 11, 10, 11, 11,
	10, 11, 11, 10, 11, 11, 10, 11, 11, 11, 10, 11, 11, 11, 11, 10,
	11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
};

static u16 logsin[512] = {
	8191, 7950, 7195, 6698, 6327, 6030, 5784, 5572, 5387, 5223, 5075, 4941, 4818, 4704, 4599, 4500,
	4408, 4321, 4239, 4161, 4088, 4017, 3950, 3886, 3825, 3766, 3709, 3655, 3602, 3551, 3502, 3454,
	3408, 3364, 3320, 3278, 3238, 3198, 3159, 3121, 3085, 3049, 3014, 2980, 2946, 2914, 2882, 2851,
	2820, 2790, 2761, 2732, 2704, 2676, 2649, 2623, 2596, 2571, 2546, 2521, 2496, 2473, 2449, 2426,
	2403, 2381, 2359, 2337, 2315, 2294, 2274, 2253, 2233, 2213, 2193, 2174, 2155, 2136, 2117, 2099,
	2081, 2063, 2046, 2028, 2011, 1994, 1977, 1961, 1944, 1928, 1912, 1896, 1881, 1865, 1850, 1835,
	1820, 1805, 1790, 1776, 1762, 1748, 1734, 1720, 1706, 1692, 1679, 1666, 1652, 1639, 1626, 1614,
	1601, 1588, 1576, 1564, 1552, 1539, 1527, 1516, 1504, 1492, 1481, 1469, 1458, 1447, 1436, 1425,
	1414, 1403, 1392, 1381, 1371, 1360, 1350, 1340, 1329, 1319, 1309, 1299, 1289, 1279, 1270, 1260,
	1250, 1241, 1232, 1222, 1213, 1204, 1195, 1185, 1176, 1167, 1159, 1150, 1141, 1132, 1124, 1115,
	1107, 1098, 1090, 1082, 1073, 1065, 1057, 1049, 1041, 1033, 1025, 1017, 1010, 1002, 994, 987,
	979, 972, 964, 957, 949, 942, 935, 928, 920, 913, 906, 899, 892, 885, 879, 872,
	865, 858, 851, 845, 838, 832, 825, 819, 812, 806, 799, 793, 787, 781, 774, 768,
	762, 756, 750, 744, 738, 732, 726, 721, 715, 709, 703, 698, 692, 686, 681, 675,
	670, 664, 659, 653, 648, 642, 637, 632, 627, 621, 616, 611, 606, 601, 596, 591,
	586, 581, 576, 571, 566, 561, 556, 552, 547, 542, 537, 533, 528, 523, 519, 514,
	510, 505, 501, 496, 492, 487, 483, 479, 474, 470, 466, 462, 457, 453, 449, 445,
	441, 437, 433, 429, 425, 421, 417, 413, 409, 405, 401, 397, 394, 390, 386, 382,
	378, 375, 371, 367, 364, 360, 357, 353, 350, 346, 343, 339, 336, 332, 329, 325,
	322, 319, 315, 312, 309, 306, 302, 299, 296, 293, 290, 286, 283, 280, 277, 274,
	271, 268, 265, 262, 259, 256, 253, 251, 248, 245, 242, 239, 236, 234, 231, 228,
	225, 223, 220, 217, 215, 212, 209, 207, 204, 202, 199, 197, 194, 192, 189, 187,
	184, 182, 180, 177, 175, 173, 170, 168, 166, 163, 161, 159, 157, 155, 152, 150,
	148, 146, 144, 142, 140, 138, 136, 134, 132, 130, 128, 126, 124, 122, 120, 118,
	116, 114, 112, 110, 109, 107, 105, 103, 102, 100, 98, 96, 95, 93, 91, 90,
	88, 87, 85, 83, 82, 80, 79, 77, 76, 74, 73, 71, 70, 69, 67, 66,
	64, 63, 62, 60, 59, 58, 56, 55, 54, 53, 51, 50, 49, 48, 47, 46,
	44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29,
	28, 27, 26, 26, 25, 24, 23, 22, 22, 21, 20, 19, 19, 18, 17, 16,
	16, 15, 14, 14, 13, 13, 12, 11, 11, 10, 10, 9, 9, 8, 8, 7,
	7, 6, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2,
	2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0
};

la32_device::la32_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, LA32, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_rom_interface(mconfig, *this)
	, m_update_timer(nullptr)
	, m_stream(nullptr)
	, m_int_callback(*this)
	, m_rom_address_xor(0)
	, m_cycle(0)
	, m_reg_1c0(0)
	, m_reg_1c1(0)
	, m_reg_1c2(0)
	, m_reg_1c3(0)
	, m_reg_data_l(0)
	, m_inactive(0)
	, m_prev(0)
	, m_w186(0)
	, m_w187(0)
	, m_w188(0)
	, m_int_state(false)
	, m_int_status(0)
{
}

void la32_device::device_start()
{
	m_update_timer = timer_alloc(FUNC(la32_device::update), this);

	attotime rate = attotime::from_hz(clock() / 16);
	m_update_timer->adjust(rate, 0, rate);

	memset(m_reg_file, 0, sizeof(m_reg_file));
	memset(m_counters, 0, sizeof(m_counters));
	memset(m_pcm_end, 0, sizeof(m_pcm_end));
	memset(m_accum, 0, sizeof(m_accum));

	m_stream = stream_alloc(0, 8, 32000, STREAM_SYNCHRONOUS);

	save_item(NAME(m_cycle));
	save_item(NAME(m_reg_1c0));
	save_item(NAME(m_reg_1c1));
	save_item(NAME(m_reg_1c2));
	save_item(NAME(m_reg_1c3));
	save_item(NAME(m_reg_data_l));
	save_item(NAME(m_reg_file));
	save_item(NAME(m_counters));
	save_item(NAME(m_pcm_end));
	save_item(NAME(m_accum));
	save_item(NAME(m_inactive));
	save_item(NAME(m_prev));
	save_item(NAME(m_w186));
	save_item(NAME(m_w187));
	save_item(NAME(m_w188));
	save_item(NAME(m_int_state));
	save_item(NAME(m_int_status));
}

void la32_device::device_reset()
{
	m_cycle = 0;
	m_reg_1c0 = 0;
	m_reg_1c1 = 0;
	m_reg_1c2 = 0;
	m_reg_1c3 = 0;
	m_reg_data_l = 0;
	memset(m_reg_file, 0, sizeof(m_reg_file));
	memset(m_counters, 0, sizeof(m_counters));
	memset(m_pcm_end, 0, sizeof(m_pcm_end));
	memset(m_accum, 0, sizeof(m_accum));
	m_inactive = 0;
	m_prev = 0;
	m_w186 = 0;
	m_w187 = 0;
	m_w188 = 0;
	m_int_state = false;
	m_int_status = 0;
	m_int_callback(CLEAR_LINE);
}

void la32_device::sound_stream_update(sound_stream &stream)
{
	for (int sample = 0; sample < stream.samples(); sample++)
	{
		for (u32 output = 0; output < 8; output++)
			stream.put_int(output, sample, std::clamp(m_accum[0][output], -32768, 32767), 32768);
	}
}

void la32_device::write(offs_t offset, u8 data)
{
	if ((offset & 0x1c0) == 0x1c0)
	{
		switch (offset & 3)
		{
			case 0:
				m_reg_1c0 = data;
				break;
			case 1:
				m_reg_1c1 = data;
				break;
			case 2:
				m_reg_1c2 = data;
				break;
			case 3:
				m_reg_1c3 = data & 0xf3;
				break;
		}
	}
	else
	{
		if ((offset & 1) == 0)
			m_reg_data_l = data;
		else
		{
			u32 dest_r = (offset >> 6) & 7;
			u32 dest_v = (offset >> 1) & 31;
			if (dest_r < 6)
				m_reg_file[dest_r][dest_v] = (data << 8) | m_reg_data_l;

			if (m_int_state)
			{
				m_int_state = false;
				m_int_callback(CLEAR_LINE);
			}
		}
	}
}

u8 la32_device::read(offs_t offset)
{
	if ((offset & 0x1c0) != 0x1c0)
		return m_int_status;
	return 0xff;
}

static s32 mul(s32 a, s32 b)
{
	a = (s16)(a << 2) >> 2;
	b = (s8)b;
	return a * b;
}

static u32 calc_pow(u32 v)
{
	u32 ix = (v >> 3) & 511;
	u32 hi = (v >> 12) & 15;
	u32 low = v & 7;

	u32 pow_o = powrom[ix];
	u32 pow_d_o = powdrom[ix];

	u32 p = pow_o + ((pow_d_o * low) >> 3);
	p |= 0x1000;

	return (p << hi) >> 9;
}

void la32_device::update_inactive()
{
	bool w49 = false;
	bool w50 = false;

	switch ((m_cycle >> 3) & 3)
	{
		case 0:
			w50 = (m_reg_1c0 & 1) != 0;
			w49 = (m_reg_1c0 & 2) != 0;
			break;
		case 1:
			w50 = (m_reg_1c0 & 4) != 0;
			w49 = (m_reg_1c0 & 8) != 0;
			break;
		case 2:
			w50 = (m_reg_1c0 & 16) != 0;
			w49 = (m_reg_1c0 & 32) != 0;
			break;
		case 3:
			w50 = (m_reg_1c0 & 64) != 0;
			w49 = (m_reg_1c0 & 128) != 0;
			break;
	}

	bool w51 = true;
	bool w52 = true;
	bool w53 = true;

	switch ((m_reg_1c1 >> 4) & 3)
	{
		case 0:
			w51 = !w50;
			w52 = !w49;
			w53 = true;
			break;
		case 1:
			w51 = (m_reg_1c1 & 4) == 0;
			w52 = (m_reg_1c1 & 8) == 0;
			w53 = (m_cycle & 24) == 0;
			break;
		case 2:
			w51 = (m_reg_1c1 & 4) == 0;
			w52 = (m_reg_1c1 & 8) == 0;
			w53 = (m_cycle & 8) == 0;
			break;
		case 3:
			w51 = (m_reg_1c1 & 4) != 0;
			w52 = !(((m_reg_1c1 & 4) != 0) ^ ((m_reg_1c1 & 8) != 0));
			w53 = (m_cycle & 16) == 0;
			break;
	}

	u32 w47 = (m_reg_1c1 >> 2) & 3;
	if (w53)
		w47 = 3;

	u32 w48 = w47 ^ 3;
	if (w49)
		w48 = 0;

	bool w65 = (m_cycle & 2) != 0 || (m_cycle & 1) != 0;
	bool w66 = (m_cycle & 4) != 0 || (m_cycle & 2) != 0 || (m_cycle & 1) != 0;
	bool w67 = w47 == 2;
	bool w68 = w47 == 3;
	bool w69 = w47 == 1;
	bool w64 = !((w65 && w67) || w68 || ((m_cycle & 1) != 0 && w69));
	bool w71 = w51 && !w52;
	bool w72 = !w51 && w52;
	bool w73 = !w51 && !w52;
	bool w70 = !((w72 && (m_cycle & 1) != 0) || (w71 && w65) || (w73 && w66));
	bool w63 = !((w64 && !w53) || (w70 && w53));

	if (!w63)
	{
		u8 bit = 0;
		if (w48 == 0)
			bit |= (m_reg_file[3][m_cycle] & 0xf000) == 0xf000;
		if (w48 == 1)
			bit |= (m_inactive & 2) != 0;
		if (w48 == 2)
			bit |= (m_inactive & 8) != 0;
		if (w48 == 3)
			bit |= (m_inactive & 128) != 0;
		m_inactive = (m_inactive << 1) | bit;
	}
}

TIMER_CALLBACK_MEMBER(la32_device::update)
{
	update_inactive();

	if (m_cycle == 0)
	{
		memcpy(m_accum[0], m_accum[1], sizeof(m_accum[0]));
		memset(m_accum[1], 0, sizeof(m_accum[1]));
	}

	// tva/tvf ramp
	u32 tv_value[2];

	for (u32 i = 0; i < 2; i++)
	{
		u32 dest = (m_reg_file[i * 2][m_cycle] >> 8) & 0xff;
		u32 step = calc_pow((m_reg_file[i * 2][m_cycle] << 9) & 0xffff);
		bool sign = (m_reg_file[i * 2][m_cycle] & 128) != 0;
		bool zero = (m_reg_file[i * 2][m_cycle] & 127) == 0;
		if (zero)
			step &= ~8;
		else if (sign)
			step ^= 0x3ffffff;

		u32 v = m_counters[i][m_cycle];
		u32 sum = v + step;

		u32 sum_cmp = (sum >> 18) & 0xff;
		bool cmp = sum_cmp >= dest;

		bool stop;
		if ((step & 0x2000000) != 0)
			stop = (sum & 0x4000000) == 0 || !cmp;
		else
			stop = (sum & 0x4000000) != 0 || cmp;

		bool status = (m_inactive & 1) == 0 && !zero && stop;

		u32 result = sum & 0xffff;
		if (!status && (m_inactive & 1) == 0)
			result |= sum & 0x3ff0000;
		if (i == 1 && (m_inactive & 1) != 0)
			result |= (m_reg_1c3 & 0xf0) << 18;
		if (status)
			result |= dest << 18;

		tv_value[i] = m_counters[i][m_cycle] = result & 0x3ffffff;
		if (status)
			tv_value[i] &= ~0xffff;
		tv_value[i] >>= 12;

		if (status && !m_int_state)
		{
			m_int_state = true;

			m_int_status = 0x80 | m_cycle | (i << 5);

			m_int_callback(ASSERT_LINE);
		}
	}

	u16 ctrl = m_reg_file[4][m_cycle];

	bool wave = (ctrl & 128) != 0;
	bool mode = (ctrl & 64) != 0;
	bool ring = (ctrl & 32) != 0;
	u32 outch = (ctrl >> 3) & 3;
	s32 pan = ((ctrl & 7) * 73) >> 2;

	// wave gen

	u32 p = calc_pow(m_reg_file[3][m_cycle]);
	u32 phase = m_counters[2][m_cycle];

	phase += p;

	if ((m_inactive & 1) != 0)
	{
		phase = 0;
		m_pcm_end[m_cycle] = 0;
	}

	m_counters[2][m_cycle] = phase & 0x3ffffff;

	bool sign_flip;
	{
		u32 slot = (m_cycle + 2) & 31;
		bool w60 = (slot & 30) != 0;
		bool w61 = (slot & 16) == 0 || (slot & 14) == 0;
		bool w59 = !((w60 && w61) || (m_reg_1c1 & 32) == 0 || (m_reg_1c1 & 16) == 0);
		bool w62 = ((slot & 4) != 0) ^ ((slot & 2) != 0);
		sign_flip = !((w59 || w62) && (!w59 || (slot & 2) != 0));
	}

	s32 value = 0;

	if (wave)
	{
		u32 t13 = tv_value[0];
		u32 t16 = (0x7ffe >> 1) & ~1;

		u32 t14 = t13 + t16 + 1;
		u32 t14c = (t14 >> 14) & 1;
		t14 &= 0x3fff;
		u32 t27 = t14 & 0x1fff;
		if (!t14c)
			t27 = 0;
		if (t14c && (t14 & 0x2000) != 0)
			t27 |= 0x2000;

		u32 t18 = (0x7ffe >> 1) & ~1;

		u32 t19 = tv_value[1] + t18 + 1;
		u32 t19c = (t19 >> 14) & 1;
		t19 &= 0x3fff;
		u32 t22 = t19 & 0x1fff;
		if (!t19c)
			t22 = 0;
		if (t19c && (t19 & 0x2000) != 0)
			t22 |= 0x2000;

		u32 ph = (phase >> 8) & 0x3ffff;

		// Bits 2:0 encode log2(length in 0x800-sample pages); bit 3
		// suppresses the boundary event and makes the page counter wrap.
		u32 wave_cfg = (ctrl >> 12) & 15;

		u32 w271 = (127 << (wave_cfg & 7)) & 127;

		bool const loop1 = BIT(wave_cfg, 3);
		bool const boundary1 = ((ph >> 11) & (w271 & 127)) != 0 && !loop1;

		if (boundary1 && !BIT(m_pcm_end[m_cycle], 0))
			m_pcm_end[m_cycle] |= 0x05; // ended + boundary notification pending
		bool const end = BIT(m_pcm_end[m_cycle], 0);

		u32 base = m_reg_file[1][m_cycle];
		u32 base_l = base & 255;
		u32 base_h = (base >> 8) & 255;

		// The base is a 4 KiB ROM-page code.  Masking its low size bits
		// aligns the power-of-two sample window; phase supplies those bits.
		u32 wa1 = (ph & 0x7ff) << 1;
		wa1 |= (w271 & base_h) << 12;
		wa1 |= ((w271 ^ 127) & (ph >> 11)) << 12;
		if (base_h & 0x80)
			wa1 |= 0x80000;

		bool loop2 = loop1;
		if (mode)
			ph = (ph + 1) & 0x3ffff;
		else
		{
			wave_cfg = (ctrl >> 8) & 15;

			w271 = (127 << (wave_cfg & 7)) & 127;
			loop2 = BIT(wave_cfg, 3);
		}

		// In dual-wave mode each read has its own size and loop descriptor.
		// Reusing read 1's loop bit here made a looping second PCM terminate
		// (or a one-shot second PCM repeat) when the descriptors differed.
		bool const boundary2 = ((ph >> 11) & (w271 & 127)) != 0 && !loop2;

		if (boundary2 && !BIT(m_pcm_end[m_cycle], 1))
			m_pcm_end[m_cycle] |= 0x0a; // ended + boundary notification pending
		bool const end2 = BIT(m_pcm_end[m_cycle], 1);

		// A completed one-shot remains silent even after the firmware sets the
		// control loop bit to acknowledge its boundary.  Keep notifications
		// pending while another LA32 event owns the shared interrupt latch.
		if (!m_int_state && (m_pcm_end[m_cycle] & 0x0c))
		{
			m_int_state = true;
			if (BIT(m_pcm_end[m_cycle], 2))
			{
				m_pcm_end[m_cycle] &= ~0x04;
				m_int_status = (m_cycle + 1) & 31;
			}
			else
			{
				m_pcm_end[m_cycle] &= ~0x08;
				m_int_status = ((m_cycle + 1) & 31) | 0x20;
			}
			m_int_callback(ASSERT_LINE);
		}

		u32 wa2 = (ph & 0x7ff) << 1;
		wa2 |= ((w271 ^ 127) & (ph >> 11)) << 12;
		if (mode)
		{
			wa2 |= (w271 & base_h) << 12;
			if (base_h & 0x80)
				wa2 |= 0x80000;
		}
		else
		{
			wa2 |= (w271 & base_l) << 12;
			if (base_l & 0x80)
				wa2 |= 0x80000;
		}

		u8 s1l = pcm_rom_r(wa1);
		u8 s1h = pcm_rom_r(wa1 | 1);

		u32 s1d = 0;

		bool sign1 = (s1l & 0x80) != 0;
		if (!end)
		{
			if (m_reg_1c1 & 0x40)
				s1d = (s1h & 0x3f) | ((s1l & 0x7f) << 6) | ((s1h & 0x40) << 7);
			else if (s1l & 0x3f)
				s1d |= 0x2000;
		}

		u32 t23 = s1d + t27;
		u32 t23c = (t23 >> 14) & 1;
		t23 &= 0x3fff;

		u32 t24 = t23c ? t23 : 0;

		bool zero1 = ((t23 & 0x3800) == 0 || !t23c) && mode;

		s32 w1 = calc_pow(t24 << 2);

		if (!zero1 && (sign_flip ^ sign1))
			w1 = ~w1;

		u8 s2l = pcm_rom_r(wa2);
		u8 s2h = pcm_rom_r(wa2 | 1);

		u32 s2d = 0;

		bool sign2 = (s2l & 0x80) != 0;
		if (!end2)
		{
			if (m_reg_1c1 & 0x40)
				s2d = (s2h & 0x3f) | ((s2l & 0x7f) << 6) | ((s2h & 0x40) << 7);
			else if (s2l & 0x3f)
				s2d |= 0x2000;
		}

		u32 t28 = mode ? t27 : t22;
		u32 t25 = s2d + t28;
		u32 t25c = (t25 >> 14) & 1;
		t25 &= 0x3fff;

		u32 t26 = t25c ? t25 : 0;

		bool zero2 = (t25 & 0x3800) == 0 || !t25c;

		s32 w2 = calc_pow(t26 << 2);

		if (!zero2 && (sign_flip ^ sign2))
			w2 = ~w2;

		u32 interp = (phase >> 1) & 127;


		if (mode)
		{
			s32 o1 = mul(w1 >> 6, interp ^ 127) >> 7;
			s32 o2 = mul(w2 >> 6, interp) >> 7;

			s32 o3 = o1 + o2;
			m_prev = o3;
			value = o3;
		}
		else
		{
			s32 o1 = w1 >> 5;
			s32 o2 = w2 >> 5;

			s32 ot4 = (o1 << 17) >> 17;
			s32 ot3 = (o2 << 17) >> 17;
			s32 c = (ot4 & 1) != 0;
			value = (ot3 >> 1) + (ot4 >> 1) + c;

			if (ring)
			{
				c = (ot4 & 1) != 0;
				s32 ot7 = (ot4 >> 1) + ot3 + c;
				s32 ot8 = (ot7 >> 7) & 127; if (ot7 & 0x2000) ot8 |= 128;
				s32 ot5 = mul(m_prev, ot8) >> 6;
				s32 ot6 = mul(m_prev, ot7 & 127) >> 13;
				m_prev = value;
				value = ot5 + ot6;
			}
			else
				m_prev = value;
		}
	}
	else
	{
		u32 t1 = (m_reg_file[1][m_cycle] << 6) & 0x3fc0;
		u32 t2 = (m_w186 >> 1) & 0x3ffe; // 2
		u32 t3 = t1 + t2;
		u32 t3c = (t3 >> 14) & 1;
		t3 &= 0x3fff; // 3

		u32 t4 = t3 & 0x1fff;
		if (t3c && (t3 & 0x2000) != 0)
			t4 |= 0x1fff;
		if (!t3c && (t3 & 0x2000) == 0)
			t4 = 0;
		if (t3c)
			t4 |= 0x2000;

		u32 t5 = (t4 >> 2) | 0x2000;

		u32 t6 = (m_reg_file[1][m_cycle] >> 8) << 6;

		u32 t7 = (m_w187 >> 1) & 0x3ffe; // 3

		u32 t8 = t6 + t7;
		u32 t8c = (t8 >> 14) & 1;
		t8 &= 0x3fff; // 4

		u32 t9 = t8 & 0x1fff;
		if (t8c && (t8 & 0x2000) != 0)
			t9 |= 0x1fff;
		if (!t8c && (t8 & 0x2000) == 0)
			t9 = 0;
		if (t8c)
			t9 |= 0x2000;

		u32 t11 = t9 + (tv_value[0] & 0x3fff);
		u32 t11c = (t11 >> 14) & 1;
		t11 &= 0x3fff; // 5

		u32 t31 = t11 & 0x1fff;
		if ((t11 & 0x2000) != 0 || t11c)
			t31 = 0x1fff;

		u32 t12 = t11 & 0x1fff;
		if (t11c || (t11 & 0x3c00) == 0x3c00)
			t12 = 0x1bff;
		if (!t11c && (t11 & 0x2000) == 0)
			t12 = 0;
		t12 |= 0x2000;

		u32 e1 = t12 & 0x1fff;
		e1 <<= 2;
		e1 |= 0x8000;
		u32 p1 = calc_pow(e1);

		u32 t13 = t5 ^ 0x3fff;
		u32 t16 = t12;

		u32 t14 = t13 + t16 + 1;
		u32 t14c = (t14 >> 14) & 1;
		t14 &= 0x3fff; // 6

		u32 e2 = t14c ? (~t5 & 0xfff) : (~t12 & 0xfff);
		e2 |= 0x1000;
		e2 <<= 2;
		e2 |= 0x8003;
		u32 p2 = calc_pow(e2);

		u32 t21 = (p2 >> 4) & 0x7fff;

		u32 t18 = (ring && (m_cycle & 1) != 0) ? 0x7ffe : m_w188; // 6
		t18 = (t18 >> 1) & 0x3ffe;

		u32 t19 = (tv_value[1] & 0x3fff) + t18 + 1;
		u32 t19c = (t19 >> 14) & 1;
		t19 &= 0x3fff; // 7

		u32 t30 = t19 & 0x1fff;
		if (!t19c)
			t30 = 0;
		if (t19c && (t19 & 0x2000) != 0)
			t30 |= 0x2000;

		s32 m1 = (phase >> 6) & 0xfff;
		if (phase & 0x40000)
			m1 |= 0x3000;

		u32 t29 = t30 + (t31 << 1) + 1; // 0
		u32 t29c = (t29 >> 14) & 1;
		t29 &= 0x3fff;

		u32 t32 = t29 & 0x1fff;
		if (!t29c)
			t32 = 0;
		if (t29c && (t29 & 0x2000) != 0)
			t32 |= 0x2000;


		u32 l1 = (~phase >> 3) & 0xffff;

		u32 l2 = l1 + t21 + 0x10001;
		l2 &= 0x1ffff;



		s32 m2 = mul(m1, (p1 >> 4) & 127);
		s32 m3 = mul(m1, (p1 >> 11) & 127);
		s32 m4 = mul(l2 >> 3, (p1 >> 4) & 127);
		s32 m5 = mul(l2 >> 3, (p1 >> 11) & 127);

		u32 l3 = ((m2 >> 8) & 0xffff) + ((m3 >> 1) & 0xffff);
		u32 l3c = (l3 >> 16) & 1;
		l3 &= 0xffff;
		u32 l3_ = ((m2 >> 8) & 0x3ffff) + ((m3 >> 1) & 0x3ffff);
		u32 l3c_ = (l3_ >> 18) & 1;
		l3_ &= 0x3ffff;
		u32 l4 = ((m4 >> 8) & 0xffff) + ((m5 >> 1) & 0xffff);
		u32 l4c = (l4 >> 16) & 1;
		l4 &= 0xffff;
		u32 l4_ = ((m4 >> 8) & 0x3ffff) + ((m5 >> 1) & 0x3ffff);
		u32 l4c_ = (l4_ >> 18) & 1;
		l4_ &= 0x3ffff;

		bool w403 = !(l3c || (l3 & 0xfc00) != 0 || ((m3 >> 8) & 0x1e00) != 0); // 3
		bool w408 = !(!l3c && !((((m3 >> 8) & 0x1000) != 0) ^ (((m2 >> 8) & 0x8000) != 0))) && ((m3 >> 8) & 0x1e00) == 0x1e00 && (l3 & 0xfc00) == 0xfc00;
		bool w404 = !(l4c || (l4 & 0xfc00) != 0 || ((m5 >> 8) & 0x1e00) != 0);
		bool w409 = !(!l4c && !((((m5 >> 8) & 0x1000) != 0) ^ (((m4 >> 8) & 0x8000) != 0))) && ((m5 >> 8) & 0x1e00) == 0x1e00 && (l4 & 0xfc00) == 0xfc00;

		bool w326 = (l2 & 0x10000) != 0;

		bool w417 = !(w326 || w403);
		bool w402 = !(!w326 && w403);

		bool w419 = !(w408 || !w326);
		bool w407 = !(w326 && w408);

		bool w418 = !(w326 || w404);
		bool w429 = !(!w326 && w404);

		bool w413 = !(!w326 || w409);
		bool w430 = !(w326 && w409);

		u32 ix1 = (l1 >> 5) & 0x7ff;
		if ((ix1 & 0x200) != 0)
			ix1 ^= 0x400;
		ix1 ^= 0x200;
		if (!mode)
			ix1 &= ~0x400;

		bool w467 = (ix1 & 0x400) != 0;

		u32 ix1q = ix1 & 0x1ff;
		if (ix1 & 0x200)
			ix1q ^= 0x1ff;

		u32 ls1 = logsin[ix1q]; // 0

		u32 l5 = ~t12 & 0x1fff;
		u32 l6 = 0x1ff;
		if ((l5 & 0x1c00) == 0x1c00)
			l6 = (~l5 >> 1) & 0x1ff;
		u32 ix2 = l6 & 0x1ff;
		u32 ls2 = logsin[ix2 & 0x1ff]; // 1

		u32 ix3 = (l3 >> 1) & 0x7ff;
		if (ix3 & 0x200)
			ix3 ^= 0x1ff;
		u32 ls3 = logsin[ix3 & 0x1ff]; // 3

		u32 ix4 = (l4 >> 1) & 0x7ff;
		if (ix4 & 0x200)
			ix4 ^= 0x1ff;
		u32 ls4 = logsin[ix4 & 0x1ff]; // 5

		bool w464 = false;
		if (w326)
			w464 = w467 ^ ((ix4 & 0x400) == 0);
		else
			w464 = w467 ^ ((ix3 & 0x400) == 0);

		u32 l7 = (ls1 ^ 0x3fff);
		u32 l8 = mode ? l7 : 0x3fff;

		u32 t36 = (ctrl >> 8) & 31;
		u32 t37 = t36 << 8;
		if (t36)
			t37 |= 0x2000;
		u32 t38 = ls2 ^ 0x3fff;
		u32 t35 = t37 + t38 + 1; // 3
		u32 t35c = (t35 >> 14) & 1;
		t35 &= 0x3fff;
		u32 t50 = t35c ? t35 : 0;

		u32 t33 = l8 + t32 + 1; // 4
		u32 t33c = (t33 >> 14) & 1;
		t33 &= 0x3fff;
		u32 t34 = t33c ? t33 : 0;

		u32 t51 = t50 + t34 + 1; // 5
		u32 t51c = (t51 >> 14) & 1;
		t51 &= 0x3fff;
		u32 t52 = t51c ? t51 : 0;

		u32 t40 = 0;
		if (!w407 || !w402)
			t40 = (ls3 ^ 0x1fff) << 1;
		if (w417)
			t40 = ls3 ^ 0x3fff;
		if (w419)
			t40 = 0x3fff;

		u32 t39 = t52 + t40 + 1; // 6
		u32 t39c = (t39 >> 14) & 1;
		t39 &= 0x3fff;
		u32 t53 = t39c ? t39 : 0;

		u32 t55 = 0;
		if (!(w429 && w430))
			t55 |= ls4 ^ 0x3fff;
		if (!(w402 && w407))
			t55 |= ls3 ^ 0x3fff;
		if ((w417 && w418) || (w419 && w413))
			t55 |= 0x3fff;

		u32 t54 = t34 + t55 + 1; // 7
		u32 t54c = (t54 >> 14) & 1;
		t54 &= 0x3fff;
		u32 t56 = t54c ? t54 : 0;

		static u32 rom[] = { 127, 64, 48, 32, 20, 12, 8, 4 };
		u32 t44 = rom[(ctrl >> 13) & 7];
		if (w326)
			t44 ^= 255;

		u32 t45 = 0;
		if (w326)
		{
			t45 |= (l4_ >> 5) & 0x1ff;

			int w348 = (m5 >> 8) & 0xffff;
			int w352 = !(((w348 & 0x1000) != 0) ^ (((m4 >> 8) & 0x8000) != 0));
			int w353 = !(w352 && !l4c_);
			int w354 = w353 && (w348 & 0x800) != 0;
			int w518 = w354 ? (l4_ >> 14) & 15 : 0;
			t45 |= w518 << 9;
		}
		else
		{
			t45 |= (l3_ >> 5) & 0x1ff;

			int w348 = (m3 >> 8) & 0xffff;
			int w355 = !(l3c_ != 0 || (w348 & 0x800) != 0);
			int w518 = w355 ? ((l3_ >> 14) & 15) : 15;
			t45 |= w518 << 9;
		}

		u32 t42 = mul(t45, t44);
		bool w437 = !((t42 >> 18) != 0 || (t42 >> 19) != 0);
		bool w438 = !((t42 >> 15) != 0 || (t42 >> 16) != 0 || (t42 >> 17) != 0);
		bool w439 = w437 && w438;
		u32 t43 = (~t42 >> 1) & 0x3ff;
		if (w439)
			t43 |= (~t42 >> 1) & 0x3c00;

		u32 t41 = t53 + t43 + 1; // 0
		u32 t41c = (t41 >> 14) & 1;
		t41 &= 0x3fff;
		u32 t46 = t41c ? t41 : 0;

		u32 t48 = 0;
		if (!(w430 && w429))
			t48 = (ls4 ^ 0x1fff) << 1;
		if (w418)
			t48 |= 0x3fff;
		if (w413)
			t48 = ls4 ^ 0x3fff;

		u32 t47 = t46 + t48 + 1; // 1
		u32 t47c = (t47 >> 14) & 1;
		t47 &= 0x3fff;
		u32 t49 = t47c ? t47 : 0;


		bool w469 = !(w467 ^ w326);
		bool sign1 = !w469;

		u32 w1 = calc_pow(t56 << 2);

		if (sign_flip ^ sign1)
			w1 = ~w1;

		u32 w2 = calc_pow(t49 << 2);

		bool sign2 = !w464;

		bool zero = (t47 & 0x3800) == 0 || !t47c;

		if (!zero && (sign_flip ^ sign2))
			w2 = ~w2;

		//w1 = 0;

		int32_t o1 = w1 >> 5;
		int32_t o2 = w2 >> 5;

		int32_t ot4 = (o1 << 17) >> 17;
		int32_t ot3 = (o2 << 17) >> 17;
		int32_t c = (ot4 & 1) != 0;
		value = (ot3 >> 1) + (ot4 >> 1) + c;

		if (ring)
		{
			c = (ot4 & 1) != 0;
			int32_t ot7 = (ot4 >> 1) + ot3 + c;
			int32_t ot8 = (ot7 >> 7) & 127; if (ot7 & 0x2000) ot8 |= 128;
			int32_t ot5 = mul(m_prev, ot8) >> 6;
			int32_t ot6 = mul(m_prev, ot7 & 127) >> 13;
			m_prev = value;
			value = ot5 + ot6;
		}
		else
			m_prev = value;
	}

	s32 value_l = mul(value, pan);
	m_accum[1][outch] += value_l >> 7;

	if ((ctrl & 7) != 7)
	{
		s32 c = (~value_l >> 6) & 1;
		s32 value_r = value + (~value_l >> 7) + c;
		m_accum[1][outch | 4] += value_r;
	}

	if ((m_cycle & 7) == 1 || (m_cycle & 7) == 2 || (m_cycle & 7) == 3)
	{
		u32 c1 = 0;
		if ((m_cycle & 7) == 1)
			c1 |= m_w186;
		if ((m_cycle & 7) == 2)
			c1 |= m_w187;
		if ((m_cycle & 7) == 3)
			c1 |= m_w188;

		u32 t1 = ~(c1 >> 1) & 0x3fff;

		u32 prev = (m_cycle - 1) & 31;
		u32 addr = 0;
		if (prev & 16)
			addr |= 0x10;
		if (prev & 8)
			addr |= 0x8;
		if (prev & 2)
			addr |= 0x2;
		if (prev & 1)
			addr |= 0x1;

		u32 t2 = (m_reg_file[5][addr] >> 2) & 0x3ffe;
		u32 t3 = t1 + t2 + 1;

		u32 t4 = (t3 >> 6) & 0xff;
		if ((t3 & 0x4000) == 0)
			t4 |= 0xff00;

		u32 t5 = c1 + t4;
		if ((t3 & 0x4000) != 0)
			t5 += 1;

		addr = 4;
		addr |= m_cycle & 0x1b;

		m_reg_file[5][addr] = t5 & 0x7fff;
	}

	if ((m_cycle & 7) == 7)
	{
		u32 addr = 4;
		if (((m_cycle >> 4) ^ (m_cycle >> 3)) & 1)
			addr |= 0x10;
		if ((m_cycle & 8) == 0)
			addr |= 0x8;

		m_w186 = m_reg_file[5][addr + 1] & 0x7fff;
		m_w187 = m_reg_file[5][addr + 2] & 0x7fff;
		m_w188 = m_reg_file[5][addr + 3] & 0x7fff;
	}

	m_cycle = (m_cycle + 1) & 31;
}
