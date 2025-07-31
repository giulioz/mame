// license:BSD-3-Clause
// copyright-holders:Bryan McPhail, Alex W. Jackson,giulioz
#ifndef MAME_CPU_NEC_V55_H
#define MAME_CPU_NEC_V55_H

#pragma once

#include "necdasm.h"

#define NEC_INPUT_LINE_INTP0 10
#define NEC_INPUT_LINE_INTP1 11
#define NEC_INPUT_LINE_INTP2 12
#define NEC_INPUT_LINE_POLL 20

enum
{
	V55_PC=0,
	V55_AW, V55_CW, V55_DW, V55_BW, V55_SP, V55_BP, V55_IX, V55_IY,
	V55_DS1, V55_PS, V55_SS, V55_DS0,
	V55_DS2, V55_DS3,
	V55_AL, V55_AH, V55_CL, V55_CH, V55_DL, V55_DH, V55_BL, V55_BH,
	V55_PSW,
	V55_PENDING
};

/* interrupt sources */
enum INTSOURCES
{
	BRK        = 0,
	INT_IRQ    = 1,
	NMI_IRQ    = 1 << 1,
	INT_WDT    = 1 << 2,
	INT_INTP0  = 1 << 3,
	INT_INTP1  = 1 << 4,
	INT_INTP2  = 1 << 5,
	INT_INTP3  = 1 << 6,
	INT_INTP4  = 1 << 7,
	INT_INTP5  = 1 << 8,
	INT_CM00   = 1 << 9,
	INT_CM01   = 1 << 10,
	INT_CM10   = 1 << 11,
	INT_CM11   = 1 << 12,
	INT_CM21   = 1 << 13,
	INT_CM31   = 1 << 14,
	INT_D0     = 1 << 15,
	INT_D0S    = 1 << 16,
	INT_D1     = 1 << 17,
	INT_D1S    = 1 << 18,
	INT_SER0   = 1 << 19,
	INT_SER1   = 1 << 20,
	INT_SR0    = 1 << 21,
	INT_SR1    = 1 << 22,
	INT_ST0    = 1 << 23,
	INT_ST1    = 1 << 24,
	INT_SIT    = 1 << 25,
	INT_PAI    = 1 << 26,
	INT_AD     = 1 << 27,
};

class v55_device : public cpu_device, public nec_disassembler::config
{
public:
	auto pt_in_cb() { return m_pt_in.bind(); }
	auto p0_in_cb() { return m_p0_in.bind(); }
	auto p1_in_cb() { return m_p1_in.bind(); }
	auto p2_in_cb() { return m_p2_in.bind(); }

	auto p0_out_cb() { return m_p0_out.bind(); }
	auto p1_out_cb() { return m_p1_out.bind(); }
	auto p2_out_cb() { return m_p2_out.bind(); }

	auto dma0_read_cb() { return m_dma_read[0].bind(); }
	auto dma1_read_cb() { return m_dma_read[1].bind(); }

	auto dma0_write_cb() { return m_dma_write[0].bind(); }
	auto dma1_write_cb() { return m_dma_write[1].bind(); }

	TIMER_CALLBACK_MEMBER(v55_timer_callback);

	v55_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

// protected:
	// construction/destruction
	v55_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, bool is_16bit, uint8_t prefetch_size, uint8_t prefetch_cycles, uint32_t chip_type);
	
	// device-level overrides
	void device_start() override ATTR_COLD;
	void device_reset() override ATTR_COLD;
	void device_post_load() override { notify_clock_changed(); }

	// device_execute_interface overrides
	uint64_t execute_clocks_to_cycles(uint64_t clocks) const noexcept override { return clocks / m_PCK; }
	uint64_t execute_cycles_to_clocks(uint64_t cycles) const noexcept override { return cycles * m_PCK; }
	uint32_t execute_min_cycles() const noexcept override { return 1; }
	uint32_t execute_max_cycles() const noexcept override { return 80; }
	uint32_t execute_default_irq_vector(int inputnum) const noexcept override { return 0xff; }
	bool execute_input_edge_triggered(int inputnum) const noexcept override { return inputnum == INPUT_LINE_NMI || (inputnum >= NEC_INPUT_LINE_INTP0 && inputnum <= NEC_INPUT_LINE_INTP2); }
	void execute_run() override;
	void execute_set_input(int inputnum, int state) override;

	// device_memory_interface overrides
	space_config_vector memory_space_config() const override;
	bool memory_translate(int spacenum, int intention, offs_t &address, address_space *&target_space) override;

	// device_state_interface overrides
	void state_string_export(const device_state_entry &entry, std::string &str) const override;
	void state_import(const device_state_entry &entry) override;
	void state_export(const device_state_entry &entry) override;

	// device_disasm_interface overrides
	std::unique_ptr<util::disasm_interface> create_disassembler() override;
	int get_mode() const override { return 1; }

// private:
	address_space_config m_program_config;
	address_space_config m_data_config;
	address_space_config m_io_config;

	memory_access<20, 0, 0, ENDIANNESS_LITTLE>::cache m_cache8;
	memory_access<20, 1, 0, ENDIANNESS_LITTLE>::cache m_cache16;

	/* internal RAM and register banks */
	// required_shared_ptr<uint16_t> m_internal_ram;
	std::array<uint16_t, 0x200> m_internal_ram;

	uint16_t  m_ip;
	uint16_t  m_prev_ip;

	/* PSW flags */
	int32_t   m_SignVal;
	uint32_t  m_AuxVal, m_OverVal, m_ZeroVal, m_CarryVal, m_ParityVal;  /* 0 or non-0 valued flags */
	uint8_t   m_IBRK, m_TF, m_IF, m_DF;   /* 0 or 1 valued flags */
	uint16_t   m_RBW, m_RBB;   /* current register bank base, preshifted for word and byte registers */

	/* interrupt related */
	uint32_t  m_pending_irq;
	uint32_t  m_unmasked_irq;
	uint32_t  m_macro_service;
	uint32_t  m_bankswitch_irq;
	uint8_t   m_priority_inttu, m_priority_intd, m_priority_intp, m_priority_ints0, m_priority_ints1;
	uint8_t   m_ems[3];
	uint8_t   m_srms[2];
	uint8_t   m_stms[2];
	uint8_t   m_tmms[3];
	uint8_t   m_IRQS, m_ISPR;
	uint32_t  m_nmi_state;
	uint32_t  m_irq_state;
	uint32_t  m_poll_state;
	uint32_t  m_intp_state[3];
	uint8_t   m_intm;
	uint8_t   m_no_interrupt;
	uint8_t   m_halted;
	uint8_t   m_imc;
	uint16_t  m_imc_base;
	uint8_t   m_intm0;
	uint8_t   m_ic[64];

	// timer related
	uint16_t  m_TM0, m_MD0, m_TM1, m_MD1;
	uint8_t   m_TMC0, m_TMC1;
	emu_timer *m_timers[4];

	// serial interface related
	uint8_t   m_scm[2];
	uint8_t   m_scc[2];
	uint8_t   m_brg[2];
	uint8_t   m_sce[2];

	// DMA related
	uint32_t tc0;
	uint32_t udc0;
	uint32_t dcm0;
	uint32_t mar0;
	uint32_t dptc0;
	uint8_t dmam0;
	uint8_t dmac0;
	uint8_t dmas;

	// system control
	uint8_t   m_TB, m_PCK; // PRC register
	uint8_t   m_RFM;
	uint16_t  m_WTC;

	address_space *m_program;
	std::function<u8 (offs_t address)> m_dr8;
	memory_access<9, 1, 0, ENDIANNESS_LITTLE>::specific m_data;
	address_space *m_io;
	int     m_icount;

	/* callbacks */
	devcb_read8 m_pt_in;
	devcb_read8 m_p0_in;
	devcb_read8 m_p1_in;
	devcb_read8 m_p2_in;

	devcb_write8 m_p0_out;
	devcb_write8 m_p1_out;
	devcb_write8 m_p2_out;

	devcb_read16::array<2> m_dma_read;
	devcb_write16::array<2> m_dma_write;

	uint8_t   m_prefetch_size;
	uint8_t   m_prefetch_cycles;
	int8_t    m_prefetch_count;
	uint8_t   m_prefetch_reset;
	uint32_t  m_chip_type;

	uint32_t  m_prefix_base;    /* base address of the latest prefix segment */
	uint8_t   m_seg_prefix;     /* prefix segment indicator */

	uint32_t m_EA;
	uint16_t m_EO;
	uint16_t m_E16;

	uint32_t m_debugger_temp;

	typedef void (v55_device::*nec_ophandler)();
	typedef uint32_t (v55_device::*nec_eahandler)();
	static const nec_ophandler s_nec_instruction[256];
	static const nec_eahandler s_GetEA[192];

	inline void prefetch();
	void do_prefetch(int previous_ICount);
	inline uint8_t fetch();
	inline uint16_t fetchword();
	inline uint8_t fetchop();
	void nec_interrupt(unsigned int_num, int /*INTSOURCES*/ source);
	void nec_bankswitch(unsigned bank_num);
	void nec_trap();
	void external_int();
	void do_int(unsigned int_num, int source, bool can_bankswitch);

	void ida_sfr_map(address_map &map) ATTR_COLD;
	uint8_t adm_r();
	void adm_w(uint8_t data);
	uint8_t ispr_r();
	uint8_t imc_r();
	void imc_w(uint8_t data);
	uint8_t ic_r(offs_t pos);
	void ic_w(offs_t pos, uint8_t data);
	uint8_t p0_r();
	void p0_w(uint8_t data);
	uint8_t p2_r();
	void p2_w(uint8_t data);
	uint8_t p4_r();
	void p4_w(uint8_t data);
	uint8_t pm0_r();
	void pm0_w(uint8_t data);
	uint8_t pm2_r();
	void pm2_w(uint8_t data);
	uint8_t pm4_r();
	void pm4_w(uint8_t data);
	uint8_t pm8_r();
	void pm8_w(uint8_t data);
	uint8_t pmc2_r();
	void pmc2_w(uint8_t data);
	uint8_t pmc4_r();
	void pmc4_w(uint8_t data);
	uint8_t pmc8_r();
	void pmc8_w(uint8_t data);
	uint8_t tmc0_r();
	void tmc0_w(uint8_t data);
	uint8_t intm0_r();
	void intm0_w(uint8_t data);
	uint8_t tm3_r();
	void tm3_w(uint8_t data);
	uint8_t cm00_r();
	void cm00_w(uint8_t data);
	uint8_t cm10_r();
	void cm10_w(uint8_t data);
	uint8_t cm31_r();
	void cm31_w(uint8_t data);
	uint8_t pwm_r();
	void pwm_w(uint8_t data);
	uint8_t pwc0_r();
	void pwc0_w(uint8_t data);
	uint8_t mbc_r();
	void mbc_w(uint8_t data);
	uint8_t rfm_r();
	void rfm_w(uint8_t data);
	uint8_t stbc_r();
	void stbc_w(uint8_t data);

	uint8_t adcr0_r();
	void adcr0_w(uint8_t data);
	uint8_t adcr3_r();
	void adcr3_w(uint8_t data);
	uint8_t cm21_r();
	void cm21_w(uint8_t data);
	uint8_t txbrg0_r();
	void txbrg0_w(uint8_t data);
	uint8_t rxbrg0_r();
	void rxbrg0_w(uint8_t data);
	uint8_t prs0_r();
	void prs0_w(uint8_t data);
	uint8_t uartm0_r();
	void uartm0_w(uint8_t data);
	uint8_t txbrg1_r();
	void txbrg1_w(uint8_t data);
	uint8_t rxbrg1_r();
	void rxbrg1_w(uint8_t data);
	uint8_t prs1_r();
	void prs1_w(uint8_t data);
	uint8_t txb1_r();
	void txb1_w(uint8_t data);
	uint8_t asp_r();
	void asp_w(uint8_t data);

	uint8_t v55_read_byte(unsigned a);
	uint16_t v55_read_word(unsigned a);
	void v55_write_byte(unsigned a, uint8_t d);
	void v55_write_word(unsigned a, uint16_t d);
	
	uint16_t tc0l_r();
	void tc0l_w(uint16_t data);
	uint16_t tc0h_r();
	void tc0h_w(uint16_t data);
	uint16_t udc0l_r();
	void udc0l_w(uint16_t data);
	uint16_t dcm0l_r();
	void dcm0l_w(uint16_t data);
	uint16_t mar0l_r();
	void mar0l_w(uint16_t data);
	uint16_t dptc0l_r();
	void dptc0l_w(uint16_t data);
	uint16_t udc0h_r();
	void udc0h_w(uint16_t data);
	uint16_t dcm0h_r();
	void dcm0h_w(uint16_t data);
	uint16_t mar0h_r();
	void mar0h_w(uint16_t data);
	uint16_t dptc0h_r();
	void dptc0h_w(uint16_t data);
	uint8_t dmam0_r();
	void dmam0_w(uint8_t data);
	uint8_t dmac0_r();
	void dmac0_w(uint8_t data);
	uint8_t dmas_r();
	void dmas_w(uint8_t data);

	void i_add_br8();
	void i_add_wr16();
	void i_add_r8b();
	void i_add_r16w();
	void i_add_ald8();
	void i_add_axd16();
	void i_push_es();
	void i_pop_es();
	void i_or_br8();
	void i_or_r8b();
	void i_or_wr16();
	void i_or_r16w();
	void i_or_ald8();
	void i_or_axd16();
	void i_push_cs();
	void i_pre_nec();
	void i_pre_v55();
	void i_adc_br8();
	void i_adc_wr16();
	void i_adc_r8b();
	void i_adc_r16w();
	void i_adc_ald8();
	void i_adc_axd16();
	void i_push_ss();
	void i_pop_ss();
	void i_sbb_br8();
	void i_sbb_wr16();
	void i_sbb_r8b();
	void i_sbb_r16w();
	void i_sbb_ald8();
	void i_sbb_axd16();
	void i_push_ds();
	void i_pop_ds();
	void i_and_br8();
	void i_and_r8b();
	void i_and_wr16();
	void i_and_r16w();
	void i_and_ald8();
	void i_and_axd16();
	void i_es();
	void i_daa();
	void i_sub_br8();
	void i_sub_wr16();
	void i_sub_r8b();
	void i_sub_r16w();
	void i_sub_ald8();
	void i_sub_axd16();
	void i_cs();
	void i_das();
	void i_xor_br8();
	void i_xor_r8b();
	void i_xor_wr16();
	void i_xor_r16w();
	void i_xor_ald8();
	void i_xor_axd16();
	void i_ss();
	void i_aaa();
	void i_cmp_br8();
	void i_cmp_wr16();
	void i_cmp_r8b();
	void i_cmp_r16w();
	void i_cmp_ald8();
	void i_cmp_axd16();
	void i_ds();
	void i_aas();
	void i_inc_ax();
	void i_inc_cx();
	void i_inc_dx();
	void i_inc_bx();
	void i_inc_sp();
	void i_inc_bp();
	void i_inc_si();
	void i_inc_di();
	void i_dec_ax();
	void i_dec_cx();
	void i_dec_dx();
	void i_dec_bx();
	void i_dec_sp();
	void i_dec_bp();
	void i_dec_si();
	void i_dec_di();
	void i_push_ax();
	void i_push_cx();
	void i_push_dx();
	void i_push_bx();
	void i_push_sp();
	void i_push_bp();
	void i_push_si();
	void i_push_di();
	void i_pop_ax();
	void i_pop_cx();
	void i_pop_dx();
	void i_pop_bx();
	void i_pop_sp();
	void i_pop_bp();
	void i_pop_si();
	void i_pop_di();
	void i_pusha();
	void i_popa();
	void i_chkind();
	void i_repnc();
	void i_repc();
	void i_push_d16();
	void i_imul_d16();
	void i_push_d8();
	void i_imul_d8();
	void i_insb();
	void i_insw();
	void i_outsb();
	void i_outsw();
	void i_jo();
	void i_jno();
	void i_jc();
	void i_jnc();
	void i_jz();
	void i_jnz();
	void i_jce();
	void i_jnce();
	void i_js();
	void i_jns();
	void i_jp();
	void i_jnp();
	void i_jl();
	void i_jnl();
	void i_jle();
	void i_jnle();
	void i_80pre();
	void i_82pre();
	void i_81pre();
	void i_83pre();
	void i_test_br8();
	void i_test_wr16();
	void i_xchg_br8();
	void i_xchg_wr16();
	void i_mov_br8();
	void i_mov_r8b();
	void i_mov_wr16();
	void i_mov_r16w();
	void i_mov_wsreg();
	void i_lea();
	void i_mov_sregw();
	void i_invalid();
	void i_popw();
	void i_nop();
	void i_xchg_axcx();
	void i_xchg_axdx();
	void i_xchg_axbx();
	void i_xchg_axsp();
	void i_xchg_axbp();
	void i_xchg_axsi();
	void i_xchg_axdi();
	void i_cbw();
	void i_cwd();
	void i_call_far();
	void i_pushf();
	void i_popf();
	void i_sahf();
	void i_lahf();
	void i_mov_aldisp();
	void i_mov_axdisp();
	void i_mov_dispal();
	void i_mov_dispax();
	void i_movsb();
	void i_movsw();
	void i_cmpsb();
	void i_cmpsw();
	void i_test_ald8();
	void i_test_axd16();
	void i_stosb();
	void i_stosw();
	void i_lodsb();
	void i_lodsw();
	void i_scasb();
	void i_scasw();
	void i_mov_ald8();
	void i_mov_cld8();
	void i_mov_dld8();
	void i_mov_bld8();
	void i_mov_ahd8();
	void i_mov_chd8();
	void i_mov_dhd8();
	void i_mov_bhd8();
	void i_mov_axd16();
	void i_mov_cxd16();
	void i_mov_dxd16();
	void i_mov_bxd16();
	void i_mov_spd16();
	void i_mov_bpd16();
	void i_mov_sid16();
	void i_mov_did16();
	void i_rotshft_bd8();
	void i_rotshft_wd8();
	void i_ret_d16();
	void i_ret();
	void i_les_dw();
	void i_lds_dw();
	void i_mov_bd8();
	void i_mov_wd16();
	void i_enter();
	void i_leave();
	void i_retf_d16();
	void i_retf();
	void i_int3();
	void i_int();
	void i_into();
	void i_iret();
	void i_rotshft_b();
	void i_rotshft_w();
	void i_rotshft_bcl();
	void i_rotshft_wcl();
	void i_aam();
	void i_aad();
	void i_setalc();
	void i_trans();
	void i_fpo();
	void i_loopne();
	void i_loope();
	void i_loop();
	void i_jcxz();
	void i_inal();
	void i_inax();
	void i_outal();
	void i_outax();
	void i_call_d16();
	void i_jmp_d16();
	void i_jmp_far();
	void i_jmp_d8();
	void i_inaldx();
	void i_inaxdx();
	void i_outdxal();
	void i_outdxax();
	void i_lock();
	void i_repne();
	void i_repe();
	void i_hlt();
	void i_cmc();
	void i_f6pre();
	void i_f7pre();
	void i_clc();
	void i_stc();
	void i_di();
	void i_ei();
	void i_cld();
	void i_std();
	void i_fepre();
	void i_ffpre();
	void i_wait();
	void i_v55_ds2();
	void i_v55_ds3();
	void i_v55_iram();

	uint32_t EA_000();
	uint32_t EA_001();
	uint32_t EA_002();
	uint32_t EA_003();
	uint32_t EA_004();
	uint32_t EA_005();
	uint32_t EA_006();
	uint32_t EA_007();
	uint32_t EA_100();
	uint32_t EA_101();
	uint32_t EA_102();
	uint32_t EA_103();
	uint32_t EA_104();
	uint32_t EA_105();
	uint32_t EA_106();
	uint32_t EA_107();
	uint32_t EA_200();
	uint32_t EA_201();
	uint32_t EA_202();
	uint32_t EA_203();
	uint32_t EA_204();
	uint32_t EA_205();
	uint32_t EA_206();
	uint32_t EA_207();

	uint32_t EAI_000();
	uint32_t EAI_001();
	uint32_t EAI_002();
	uint32_t EAI_003();
	uint32_t EAI_004();
	uint32_t EAI_005();
	uint32_t EAI_006();
	uint32_t EAI_007();
	uint32_t EAI_100();
	uint32_t EAI_101();
	uint32_t EAI_102();
	uint32_t EAI_103();
	uint32_t EAI_104();
	uint32_t EAI_105();
	uint32_t EAI_106();
	uint32_t EAI_107();
	uint32_t EAI_200();
	uint32_t EAI_201();
	uint32_t EAI_202();
	uint32_t EAI_203();
	uint32_t EAI_204();
	uint32_t EAI_205();
	uint32_t EAI_206();
	uint32_t EAI_207();

	static const nec_eahandler s_GetEA_IRAM[192];
};


DECLARE_DEVICE_TYPE(V55, v55_device)


#endif // MAME_CPU_NEC_V55_H
