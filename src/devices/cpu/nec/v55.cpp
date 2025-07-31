// license:BSD-3-Clause
// copyright-holders:Bryan McPhail, Alex W. Jackson, giulioz
/****************************************************************************

    NEC V55PI emulator

****************************************************************************/

#include "emu.h"
#include "v55.h"
#include "necdasm.h"

#define LOG_BUSLOCK (1 << 1)
//#define VERBOSE (...)

#include "logmacro.h"

typedef uint8_t BOOLEAN;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;

#include "v55priv.ipp"


DEFINE_DEVICE_TYPE(V55, v55_device, "v55", "NEC V55PI")


v55_device::v55_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, bool is_16bit, uint8_t prefetch_size, uint8_t prefetch_cycles, uint32_t chip_type)
	: cpu_device(mconfig, type, tag, owner, clock)
	, m_program_config("program", ENDIANNESS_LITTLE, is_16bit ? 16 : 8, 20, 0)
	, m_data_config("data", ENDIANNESS_LITTLE, 16, 9, 0, address_map_constructor(FUNC(v55_device::ida_sfr_map), this))
	, m_io_config("io", ENDIANNESS_LITTLE, is_16bit ? 16 : 8, 16, 0)
	, m_PCK(8)
	, m_pt_in(*this, 0xff)
	, m_p0_in(*this, 0xff)
	, m_p1_in(*this, 0xff)
	, m_p2_in(*this, 0xff)
	, m_p0_out(*this)
	, m_p1_out(*this)
	, m_p2_out(*this)
	, m_dma_read(*this, 0xffff)
	, m_dma_write(*this)
	, m_prefetch_size(prefetch_size)
	, m_prefetch_cycles(prefetch_cycles)
	, m_chip_type(chip_type)
{
}

v55_device::v55_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: v55_device(mconfig, V55, tag, owner, clock, false, 4, 4, V20_TYPE)
{
}


device_memory_interface::space_config_vector v55_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_PROGRAM, &m_program_config),
		std::make_pair(AS_DATA,    &m_data_config),
		std::make_pair(AS_IO,      &m_io_config)
	};
}

TIMER_CALLBACK_MEMBER(v55_device::v55_timer_callback)
{
	m_pending_irq |= param;
}

void v55_device::prefetch()
{
	m_prefetch_count--;
}

void v55_device::do_prefetch(int previous_ICount)
{
	int diff = previous_ICount - (int) m_icount;

	/* The implementation is not accurate, but comes close.
	 * It does not respect that the V30 will fetch two bytes
	 * at once directly, but instead uses only 2 cycles instead
	 * of 4. There are however only very few sources publicly
	 * available and they are vague.
	 */
	while (m_prefetch_count<0)
	{
		m_prefetch_count++;
		if (diff>m_prefetch_cycles)
			diff -= m_prefetch_cycles;
		else
			m_icount -= m_prefetch_cycles;
	}

	if (m_prefetch_reset)
	{
		m_prefetch_count = 0;
		m_prefetch_reset = 0;
		return;
	}

	while (diff>=m_prefetch_cycles && m_prefetch_count < m_prefetch_size)
	{
		diff -= m_prefetch_cycles;
		m_prefetch_count++;
	}

}

uint8_t v55_device::fetch()
{
	prefetch();
	return m_dr8((Sreg(PS)<<4)+m_ip++);
}

uint16_t v55_device::fetchword()
{
	uint16_t r = fetch();
	r |= (fetch()<<8);
	return r;
}

#define nec_common_device v55_device

#include "v55instr.h"
#include "necmacro.h"
#include "necea.h"
#include "necmodrm.h"

static uint8_t parity_table[256];

uint8_t v55_device::fetchop()
{
	uint8_t ret;

	prefetch();
	ret = m_dr8((Sreg(PS)<<4)+m_ip++);

	return ret;
}



/***************************************************************************/

void v55_device::device_reset()
{
	m_ip = 0;
	m_prev_ip = 0;
	m_IBRK = 1;
	m_TF = 0;
	m_IF = 0;
	m_DF = 0;
	m_SignVal = 0;
	m_AuxVal = 0;
	m_OverVal = 0;
	m_ZeroVal = 1;
	m_CarryVal = 0;
	m_ParityVal = 1;
	m_pending_irq = 0;
	m_unmasked_irq = INT_IRQ | NMI_IRQ;
	m_macro_service = 0;
	m_bankswitch_irq = 0;
	m_priority_inttu = 7;
	m_priority_intd = 7;
	m_priority_intp = 7;
	m_priority_ints0 = 7;
	m_priority_ints1 = 7;
	m_IRQS = m_ISPR = 0;
	m_nmi_state = 0;
	m_irq_state = 0;
	m_poll_state = 1;
	m_intm = 0;
	m_halted = 0;
	m_imc = 0x80;
	m_imc_base = 0; // TODO: actually 0x80
	m_intm0 = 0; // TODO: actually 0x80
	for (size_t i = 0; i < 64; i++)
		m_ic[i] = 0x43;
	

	m_TMC0 = m_TMC1 = 0;

	for (int i = 0; i < 2; i++)
	{
		m_scm[i] = 0;
		m_scc[i] = 0;
		m_brg[i] = 0;
		m_sce[i] = 0;
	}

	m_TB = 20;
	m_PCK = 8;
	m_RFM = 0xfc;
	m_WTC = 0xffff;

	dmam0 = 0xe0;
	dmac0 = 0x00;
	dmas = 0x00;

	// unsigned tmp = m_PCK << m_TB;
	// attotime time = clocks_to_attotime(tmp);
	// m_timers[3]->adjust(time, INTTB, time);

	m_timers[0]->adjust(attotime::never);
	m_timers[1]->adjust(attotime::never);
	m_timers[2]->adjust(attotime::never);

	SetRB(15);
	Sreg(PS) = 0xffff;
	Sreg(SS) = 0;
	Sreg(DS0) = 0;
	Sreg(DS1) = 0;
	Sreg(DS2) = 0;
	Sreg(DS3) = 0;

	CHANGE_PC;
}


void v55_device::nec_interrupt(unsigned int_num, int /*INTSOURCES*/ source)
{
	uint32_t dest_seg, dest_off;

	// logerror("nec_interrupt: int_num=%d, source=%d\n", int_num, source);
	// printf("nec_interrupt: int_num=%d\n", int_num);

	i_pushf();
	m_TF = m_IF = 0;

	standard_irq_callback(int_num, PC());

	debugger_exception_hook(int_num);

	dest_off = read_mem_word(int_num*4);
	dest_seg = read_mem_word(int_num*4+2);
	if (int_num >= NEC_WDT_VECTOR) {
		// dest_off = read_mem_word(m_imc_base + int_num*4);
		// dest_seg = read_mem_word(m_imc_base + int_num*4+2);

		// printf("nec_interrupt with imc: int_num=%d, imc_base=%04x, dest_off=%04x, dest_seg=%04x\n", int_num, m_imc_base, dest_off, dest_seg);
	}

	PUSH(Sreg(PS));
	PUSH(m_ip);
	m_prev_ip = m_ip = (WORD)dest_off;
	Sreg(PS) = (WORD)dest_seg;
	CHANGE_PC;
}

void v55_device::nec_bankswitch(unsigned bank_num)
{
	int tmp = CompressFlags();

	m_TF = m_IF = 0;

	SetRB(bank_num);

	Wreg(PSW_SAVE) = tmp;
	Wreg(PC_SAVE) = m_ip;
	m_prev_ip = m_ip = Wreg(VECTOR_PC);
	CHANGE_PC;
}

void v55_device::nec_trap()
{
	(this->*s_nec_instruction[fetchop()])();
	nec_interrupt(NEC_TRAP_VECTOR, BRK);
}

void v55_device::do_int(unsigned int_num, int source, bool can_bankswitch)
{
	if (m_pending_irq & source)
	{
		uint8_t ic = m_ic[int_num];
		uint8_t priority = ic & 0xf;
		bool bank_switch = (ic >> 4) & 0x1;
		bool macro_srvc = (ic >> 5) & 0x1;
		bool masked = (ic >> 6) & 0x1;
		// bool response = (ic >> 7) & 0x1;

		// printf("pending int_num=%d, ic=%x\n", int_num, ic);

		if (masked)
			return;

		if (macro_srvc)
		{
			printf("unsupported macro service for int_num=%d, source=%d\n", int_num, source);
			return;
		}

		if (bank_switch && can_bankswitch)
		{
			// printf("int bankswitch int_num=%d, source=%d, bank=%x\n", int_num, source, priority + 0x3);
			nec_bankswitch(priority + 0x3); // ??
		}
		else
		{
			// uint32_t dest_off = read_mem_word(int_num*4);
			// uint32_t dest_seg = read_mem_word(int_num*4+2);
			// printf("int vector int_num=%d, source=%d, addr=%06x\n", int_num, source, dest_off + (dest_seg << 4));
			nec_interrupt(int_num, source);
		}

		m_pending_irq &= ~source;
	}
}

void v55_device::external_int()
{
	if (m_pending_irq & NMI_IRQ)
	{
		do_int(NEC_NMI_VECTOR, NMI_IRQ, false);
		m_pending_irq &= ~NMI_IRQ;
	}
	else if (m_pending_irq & INT_CM00)
	{
		do_int(NEC_CM00_VECTOR, INT_CM00, true);
		m_pending_irq &= ~INT_CM00;
	}
	else if (m_pending_irq & INT_CM01)
	{
		do_int(NEC_CM01_VECTOR, INT_CM01, true);
		m_pending_irq &= ~INT_CM01;
	}
	else if (m_pending_irq & INT_CM10)
	{
		do_int(NEC_CM10_VECTOR, INT_CM10, true);
		m_pending_irq &= ~INT_CM10;
	}
	else if (m_pending_irq & INT_CM11)
	{
		do_int(NEC_CM11_VECTOR, INT_CM11, true);
		m_pending_irq &= ~INT_CM11;
	}
	else if (m_pending_irq & INT_CM21)
	{
		do_int(NEC_CM21_VECTOR, INT_CM21, true);
		m_pending_irq &= ~INT_CM21;
	}
	else if (m_pending_irq & INT_CM31)
	{
		do_int(NEC_CM31_VECTOR, INT_CM31, true);
		m_pending_irq &= ~INT_CM31;
	}
	else if (m_pending_irq)
	{
		printf("Unhandled interrupt pending: %08x\n", m_pending_irq);
	}

	// else if (pending & SOURCES)
	// {
	// 	int i = -1;
	// 	uint32_t source = 0;
	// 	uint8_t vector = 0;
	// 	uint8_t ms = 0;
	// 	while (++i < 8)
	// 	{
	// 		if (m_ISPR & (1 << i)) break;

	// 		if (m_priority_inttu == i)
	// 		{
	// 			if (pending & INTTU0)
	// 			{
	// 				source = INTTU0;
	// 				vector = NEC_INTTU0_VECTOR;
	// 				ms = m_tmms[0];
	// 				break;
	// 			}
	// 			if (pending & INTTU1)
	// 			{
	// 				source = INTTU1;
	// 				vector = NEC_INTTU1_VECTOR;
	// 				ms = m_tmms[1];
	// 				break;
	// 			}
	// 			if (pending & INTTU2)
	// 			{
	// 				source = INTTU2;
	// 				vector = NEC_INTTU2_VECTOR;
	// 				ms = m_tmms[2];
	// 				break;
	// 			}
	// 		}

	// 		if (m_priority_intd == i)
	// 		{
	// 			if (pending & INTD0)
	// 			{
	// 				source = INTD0;
	// 				vector = NEC_INTD0_VECTOR;
	// 				break;
	// 			}
	// 			if (pending & INTD1)
	// 			{
	// 				source = INTD1;
	// 				vector = NEC_INTD1_VECTOR;
	// 				break;
	// 			}
	// 		}

	// 		if (m_priority_intp == i)
	// 		{
	// 			if (pending & INTP0)
	// 			{
	// 				source = INTP0;
	// 				vector = NEC_INTP0_VECTOR;
	// 				break;
	// 			}
	// 			if (pending & INTP1)
	// 			{
	// 				source = INTP1;
	// 				vector = NEC_INTP1_VECTOR;
	// 				break;
	// 			}
	// 			if (pending & INTP2)
	// 			{
	// 				source = INTP2;
	// 				vector = NEC_INTP2_VECTOR;
	// 				break;
	// 			}
	// 		}

	// 		if (m_priority_ints0 == i)
	// 		{
	// 			if (pending & INTSER0)
	// 			{
	// 				source = INTSER0;
	// 				vector = NEC_INTSER0_VECTOR;
	// 				break;
	// 			}
	// 			if (pending & INTSR0)
	// 			{
	// 				source = INTSR0;
	// 				vector = NEC_INTSR0_VECTOR;
	// 				ms = m_srms[0];
	// 				break;
	// 			}
	// 			if (pending & INTST0)
	// 			{
	// 				source = INTST0;
	// 				vector = NEC_INTST0_VECTOR;
	// 				ms = m_stms[0];
	// 				break;
	// 			}
	// 		}

	// 		if (m_priority_ints1 == i)
	// 		{
	// 			if (pending & INTSER1)
	// 			{
	// 				source = INTSER1;
	// 				vector = NEC_INTSER1_VECTOR;
	// 				break;
	// 			}
	// 			if (pending & INTSR1)
	// 			{
	// 				source = INTSR1;
	// 				vector = NEC_INTSR1_VECTOR;
	// 				ms = m_srms[1];
	// 				break;
	// 			}
	// 			if (pending & INTST1)
	// 			{
	// 				source = INTST1;
	// 				vector = NEC_INTST1_VECTOR;
	// 				ms = m_stms[1];
	// 				break;
	// 			}
	// 		}

	// 		if (i == 7 && (pending & INTTB))
	// 		{
	// 			source = INTTB;
	// 			vector = NEC_INTTB_VECTOR;
	// 			break;
	// 		}
	// 	}

	// 	if (source != 0)
	// 	{
	// 		m_pending_irq &= ~source;
	// 		if (m_macro_service & source)
	// 		{
	// 			logerror("Unhandled macro service %02x\n", ms);
	// 		}
	// 		else
	// 		{
	// 			m_IRQS = vector;
	// 			m_ISPR |= (1 << i);
	// 			if (m_bankswitch_irq & source)
	// 			{
	// 				debugger_exception_hook(vector);
	// 				nec_bankswitch(i);
	// 			}
	// 			else
	// 				nec_interrupt(vector, source);
	// 		}
	// 	}
	// }
	// else if (pending & INT_IRQ)
	// {
	// 	/* the actual vector is retrieved after pushing flags */
	// 	/* and clearing the IF */
	// 	nec_interrupt((uint32_t)-1, INT_IRQ);
	// 	m_irq_state = CLEAR_LINE;
	// 	m_pending_irq &= ~INT_IRQ;
	// }
}


/****************************************************************************/
/*                             OPCODES                                      */
/****************************************************************************/

#include "necinstr.hxx"
#include "v55instr.hxx"

/*****************************************************************************/

void v55_device::execute_set_input(int irqline, int state)
{
	if (state == CLEAR_LINE)
		m_pending_irq &= ~irqline;
	else
	{
		m_pending_irq |= irqline;
		m_halted = 0;
	}
}

std::unique_ptr<util::disasm_interface> v55_device::create_disassembler()
{
	return std::make_unique<nec_disassembler>(this);
}

void v55_device::device_start()
{
	unsigned int i, j, c;

	static const WREGS wreg_name[8]={ AW, CW, DW, BW, SP, BP, IX, IY };
	static const BREGS breg_name[8]={ AL, CL, DL, BL, AH, CH, DH, BH };

	for (i = 0; i < 256; i++)
	{
		for (j = i, c = 0; j > 0; j >>= 1)
			if (j & 1) c++;
		parity_table[i] = !(c & 1);
	}

	for (i = 0; i < 256; i++)
	{
		Mod_RM.reg.b[i] = breg_name[(i & 0x38) >> 3];
		Mod_RM.reg.w[i] = wreg_name[(i & 0x38) >> 3];
	}

	for (i = 0xc0; i < 0x100; i++)
	{
		Mod_RM.RM.w[i] = wreg_name[i & 7];
		Mod_RM.RM.b[i] = breg_name[i & 7];
	}

	m_no_interrupt = 0;
	m_prefetch_count = 0;
	m_prefetch_reset = 0;
	m_prefix_base = 0;
	m_seg_prefix = 0;
	m_EA = 0;
	m_EO = 0;
	m_E16 = 0;

	m_TM0 = m_MD0 = m_TM1 = m_MD1 = 0;

	for (i = 0; i < 4; i++)
		m_timers[i] = timer_alloc(FUNC(v55_device::v55_timer_callback), this);

	std::fill_n(&m_intp_state[0], 3, 0);
	std::fill_n(&m_ems[0], 3, 0);
	std::fill_n(&m_srms[0], 2, 0);
	std::fill_n(&m_stms[0], 2, 0);
	std::fill_n(&m_tmms[0], 3, 0);

	save_item(NAME(m_internal_ram));

	save_item(NAME(m_intp_state));

	save_item(NAME(m_ip));
	save_item(NAME(m_prev_ip));
	save_item(NAME(m_IBRK));
	save_item(NAME(m_TF));
	save_item(NAME(m_IF));
	save_item(NAME(m_DF));
	save_item(NAME(m_RBW));
	save_item(NAME(m_RBB));
	save_item(NAME(m_SignVal));
	save_item(NAME(m_AuxVal));
	save_item(NAME(m_OverVal));
	save_item(NAME(m_ZeroVal));
	save_item(NAME(m_CarryVal));
	save_item(NAME(m_ParityVal));
	save_item(NAME(m_pending_irq));
	save_item(NAME(m_unmasked_irq));
	save_item(NAME(m_macro_service));
	save_item(NAME(m_bankswitch_irq));
	save_item(NAME(m_priority_inttu));
	save_item(NAME(m_priority_intd));
	save_item(NAME(m_priority_intp));
	save_item(NAME(m_priority_ints0));
	save_item(NAME(m_priority_ints1));
	save_item(NAME(m_ems));
	save_item(NAME(m_srms));
	save_item(NAME(m_stms));
	save_item(NAME(m_tmms));
	save_item(NAME(m_IRQS));
	save_item(NAME(m_ISPR));
	save_item(NAME(m_nmi_state));
	save_item(NAME(m_irq_state));
	save_item(NAME(m_poll_state));
	save_item(NAME(m_no_interrupt));
	save_item(NAME(m_intm));
	save_item(NAME(m_halted));
	save_item(NAME(m_imc));
	save_item(NAME(m_imc_base));
	save_item(NAME(m_intm0));
	save_item(NAME(m_ic));
	save_item(NAME(m_TM0));
	save_item(NAME(m_MD0));
	save_item(NAME(m_TM1));
	save_item(NAME(m_MD1));
	save_item(NAME(m_TMC0));
	save_item(NAME(m_TMC1));
	save_item(NAME(m_scm));
	save_item(NAME(m_scc));
	save_item(NAME(m_brg));
	save_item(NAME(m_sce));
	save_item(NAME(tc0));
	save_item(NAME(udc0));
	save_item(NAME(dcm0));
	save_item(NAME(mar0));
	save_item(NAME(dptc0));
	save_item(NAME(dmam0));
	save_item(NAME(dmac0));
	save_item(NAME(dmas));
	save_item(NAME(m_TB));
	save_item(NAME(m_PCK));
	save_item(NAME(m_RFM));
	save_item(NAME(m_WTC));
	save_item(NAME(m_prefetch_count));
	save_item(NAME(m_prefetch_reset));

	m_program = &space(AS_PROGRAM);
	if(m_program->data_width() == 8) {
		m_program->cache(m_cache8);
		m_dr8 = [this](offs_t address) -> u8 { return m_cache8.read_byte(address); };
	} else {
		m_program->cache(m_cache16);
		m_dr8 = [this](offs_t address) -> u8 { return m_cache16.read_byte(address); };
	}
	space(AS_DATA).specific(m_data);
	m_io = &space(AS_IO);

	state_add( V55_PC,  "PC", m_ip).formatstr("%04X");
	state_add<uint16_t>( V55_PSW, "PSW", [this]() { return CompressFlags(); }, [this](uint16_t data) { ExpandFlags(data); });

	state_add<uint16_t>( V55_AW,  "AW",  [this]() { return Wreg(AW); }, [this](uint16_t data) { Wreg(AW) = data; });
	state_add<uint16_t>( V55_CW,  "CW",  [this]() { return Wreg(CW); }, [this](uint16_t data) { Wreg(CW) = data; });
	state_add<uint16_t>( V55_DW,  "DW",  [this]() { return Wreg(DW); }, [this](uint16_t data) { Wreg(DW) = data; });
	state_add<uint16_t>( V55_BW,  "BW",  [this]() { return Wreg(BW); }, [this](uint16_t data) { Wreg(BW) = data; });
	state_add<uint16_t>( V55_SP,  "SP",  [this]() { return Wreg(SP); }, [this](uint16_t data) { Wreg(SP) = data; });
	state_add<uint16_t>( V55_BP,  "BP",  [this]() { return Wreg(BP); }, [this](uint16_t data) { Wreg(BP) = data; });
	state_add<uint16_t>( V55_IX,  "IX",  [this]() { return Wreg(IX); }, [this](uint16_t data) { Wreg(IX) = data; });
	state_add<uint16_t>( V55_IY,  "IY",  [this]() { return Wreg(IY); }, [this](uint16_t data) { Wreg(IY) = data; });
	state_add<uint16_t>( V55_DS1, "DS1", [this]() { return Sreg(DS1); }, [this](uint16_t data) { Sreg(DS1) = data; });
	state_add<uint16_t>( V55_PS,  "PS",  [this]() { return Sreg(PS); }, [this](uint16_t data) { Sreg(PS) = data; });
	state_add<uint16_t>( V55_SS,  "SS",  [this]() { return Sreg(SS); }, [this](uint16_t data) { Sreg(SS) = data; });
	state_add<uint16_t>( V55_DS0, "DS0", [this]() { return Sreg(DS0); }, [this](uint16_t data) { Sreg(DS0) = data; });
	state_add<uint16_t>( V55_DS2, "DS2", [this]() { return Sreg(DS2); }, [this](uint16_t data) { Sreg(DS2) = data; });
	state_add<uint16_t>( V55_DS3, "DS3", [this]() { return Sreg(DS3); }, [this](uint16_t data) { Sreg(DS3) = data; });

	state_add<uint8_t>( V55_AL, "AL", [this]() { return Breg(AL); }, [this](uint8_t data) { Breg(AL) = data; }).noshow();
	state_add<uint8_t>( V55_AH, "AH", [this]() { return Breg(AH); }, [this](uint8_t data) { Breg(AH) = data; }).noshow();
	state_add<uint8_t>( V55_CL, "CL", [this]() { return Breg(CL); }, [this](uint8_t data) { Breg(CL) = data; }).noshow();
	state_add<uint8_t>( V55_CH, "CH", [this]() { return Breg(CH); }, [this](uint8_t data) { Breg(CH) = data; }).noshow();
	state_add<uint8_t>( V55_DL, "DL", [this]() { return Breg(DL); }, [this](uint8_t data) { Breg(DL) = data; }).noshow();
	state_add<uint8_t>( V55_DH, "DH", [this]() { return Breg(DH); }, [this](uint8_t data) { Breg(DH) = data; }).noshow();
	state_add<uint8_t>( V55_BL, "BL", [this]() { return Breg(BL); }, [this](uint8_t data) { Breg(BL) = data; }).noshow();
	state_add<uint8_t>( V55_BH, "BH", [this]() { return Breg(BH); }, [this](uint8_t data) { Breg(BH) = data; }).noshow();

	state_add( STATE_GENPC, "GENPC", m_debugger_temp).callexport().noshow();
	state_add( STATE_GENPCBASE, "CURPC", m_debugger_temp).callexport().noshow();
	state_add( STATE_GENFLAGS, "GENFLAGS", m_debugger_temp).formatstr("%16s").noshow();

	set_icountptr(m_icount);
}


void v55_device::state_string_export(const device_state_entry &entry, std::string &str) const
{
	uint16_t flags = CompressFlags();

	switch (entry.index())
	{
		case STATE_GENFLAGS:
			str = string_format("%c %d %c%c%c%c%c%c%c%c%c%c%c%c",
				flags & 0x8000 ? 'N':'S',
				(flags & 0x7000) >> 12,
				flags & 0x0800 ? 'O':'.',
				flags & 0x0400 ? 'D':'.',
				flags & 0x0200 ? 'I':'.',
				flags & 0x0100 ? 'T':'.',
				flags & 0x0080 ? 'S':'.',
				flags & 0x0040 ? 'Z':'.',
				flags & 0x0020 ? '1':'.',
				flags & 0x0010 ? 'A':'.',
				flags & 0x0008 ? '0':'.',
				flags & 0x0004 ? 'P':'.',
				flags & 0x0002 ? '.':'I',
				flags & 0x0001 ? 'C':'.');
			break;
	}
}

void v55_device::state_import(const device_state_entry &entry)
{
	switch (entry.index())
	{
		case STATE_GENPC:
			if( m_debugger_temp - (Sreg(PS)<<4) < 0x10000 )
			{
				m_ip = m_debugger_temp - (Sreg(PS)<<4);
			}
			else
			{
				Sreg(PS) = m_debugger_temp >> 4;
				m_ip = m_debugger_temp & 0x0000f;
			}
			m_prev_ip = m_ip;
			break;
	}
}


void v55_device::state_export(const device_state_entry &entry)
{
	switch (entry.index())
	{
		case STATE_GENPC:
			m_debugger_temp = (Sreg(PS)<<4) + m_ip;
			break;

		case STATE_GENPCBASE:
			m_debugger_temp = (Sreg(PS)<<4) + m_prev_ip;
			break;
	}
}


void v55_device::execute_run()
{
	int prev_ICount;

	int pending = m_pending_irq & m_unmasked_irq;

	if (m_halted && pending)
	{
		// TODO
	}

	if (m_halted)
	{
		debugger_wait_hook();
		m_icount = 0;
		return;
	}

	while(m_icount>0) {
		/* Dispatch IRQ */
		m_prev_ip = m_ip;
		if (m_no_interrupt==0 && m_pending_irq)
		{
			if (m_pending_irq & NMI_IRQ)
				external_int();
			else if (m_IF)
				external_int();
		}

		/* No interrupt allowed between last instruction and this one */
		if (m_no_interrupt)
			m_no_interrupt--;

		debugger_instruction_hook((Sreg(PS)<<4) + m_ip);
		prev_ICount = m_icount;
		(this->*s_nec_instruction[fetchop()])();
		do_prefetch(prev_ICount);
	}
}
