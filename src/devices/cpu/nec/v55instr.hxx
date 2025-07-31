// license:BSD-3-Clause
// copyright-holders:Bryan McPhail, Alex W. Jackson, giulioz
#define GetRB   \
	ModRM = fetch();    \
	if (ModRM >= 0xc0)  \
		tmp = Wreg(Mod_RM.RM.w[ModRM]) & 0xf;   \
	else {                          \
		logerror("%06x: Invalid MODRM for register banking instruction\n",PC());   \
		tmp = 0;    \
	}

#define RETRBI  \
	tmp = (Wreg(PSW_SAVE) & 0xf000) >> 12;  \
	m_ip = Wreg(PC_SAVE);  \
	ExpandFlags(Wreg(PSW_SAVE));    \
	SetRB(tmp); \
	CHANGE_PC

#define TSKSW   \
	Wreg(PSW_SAVE) = CompressFlags();   \
	Wreg(PC_SAVE) = m_ip;  \
	SetRB(tmp); \
	m_ip = Wreg(PC_SAVE);  \
	ExpandFlags(Wreg(PSW_SAVE));    \
	CHANGE_PC

#define MOVSPA  \
	tmp = (Wreg(PSW_SAVE) & 0xf000) >> 8;   \
	Sreg(SS) = m_internal_ram[tmp+SS];    \
	Wreg(SP) = m_internal_ram[tmp+SP]

#define MOVSPB  \
	tmp <<= 4;  \
	m_internal_ram[tmp+SS] = Sreg(SS);    \
	m_internal_ram[tmp+SP] = Wreg(SP)

#define FINT    \
	for(tmp = 1; tmp < 0x100; tmp <<= 1) {  \
		if(m_ISPR & tmp) {     \
			m_ISPR &= ~tmp;    \
			break;  \
		}   \
	}

OP( 0x0f, i_pre_v55  ) { uint32_t ModRM, tmp, tmp2;
	switch (fetch()) {
		case 0x10 : BITOP_BYTE; CLKS(3,3,4); tmp2 = Breg(CL) & 0x7; m_ZeroVal = (tmp & (1<<tmp2)) ? 1 : 0; m_CarryVal=m_OverVal=0; break; /* Test */
		case 0x11 : BITOP_WORD; CLKS(3,3,4); tmp2 = Breg(CL) & 0xf; m_ZeroVal = (tmp & (1<<tmp2)) ? 1 : 0; m_CarryVal=m_OverVal=0; break; /* Test */
		case 0x12 : BITOP_BYTE; CLKS(5,5,4); tmp2 = Breg(CL) & 0x7; tmp &= ~(1<<tmp2);  PutbackRMByte(ModRM,tmp);   break; /* Clr */
		case 0x13 : BITOP_WORD; CLKS(5,5,4); tmp2 = Breg(CL) & 0xf; tmp &= ~(1<<tmp2);  PutbackRMWord(ModRM,tmp);   break; /* Clr */
		case 0x14 : BITOP_BYTE; CLKS(4,4,4); tmp2 = Breg(CL) & 0x7; tmp |= (1<<tmp2);   PutbackRMByte(ModRM,tmp);   break; /* Set */
		case 0x15 : BITOP_WORD; CLKS(4,4,4); tmp2 = Breg(CL) & 0xf; tmp |= (1<<tmp2);   PutbackRMWord(ModRM,tmp);   break; /* Set */
		case 0x16 : BITOP_BYTE; CLKS(4,4,4); tmp2 = Breg(CL) & 0x7; BIT_NOT;            PutbackRMByte(ModRM,tmp);   break; /* Not */
		case 0x17 : BITOP_WORD; CLKS(4,4,4); tmp2 = Breg(CL) & 0xf; BIT_NOT;            PutbackRMWord(ModRM,tmp);   break; /* Not */

		case 0x18 : BITOP_BYTE; CLKS(4,4,4); tmp2 = (fetch()) & 0x7;    m_ZeroVal = (tmp & (1<<tmp2)) ? 1 : 0; m_CarryVal=m_OverVal=0; break; /* Test */
		case 0x19 : BITOP_WORD; CLKS(4,4,4); tmp2 = (fetch()) & 0xf;    m_ZeroVal = (tmp & (1<<tmp2)) ? 1 : 0; m_CarryVal=m_OverVal=0; break; /* Test */
		case 0x1a : BITOP_BYTE; CLKS(6,6,4); tmp2 = (fetch()) & 0x7;    tmp &= ~(1<<tmp2);      PutbackRMByte(ModRM,tmp);   break; /* Clr */
		case 0x1b : BITOP_WORD; CLKS(6,6,4); tmp2 = (fetch()) & 0xf;    tmp &= ~(1<<tmp2);      PutbackRMWord(ModRM,tmp);   break; /* Clr */
		case 0x1c : BITOP_BYTE; CLKS(5,5,4); tmp2 = (fetch()) & 0x7;    tmp |= (1<<tmp2);       PutbackRMByte(ModRM,tmp);   break; /* Set */
		case 0x1d : BITOP_WORD; CLKS(5,5,4); tmp2 = (fetch()) & 0xf;    tmp |= (1<<tmp2);       PutbackRMWord(ModRM,tmp);   break; /* Set */
		case 0x1e : BITOP_BYTE; CLKS(5,5,4); tmp2 = (fetch()) & 0x7;    BIT_NOT;                PutbackRMByte(ModRM,tmp);   break; /* Not */
		case 0x1f : BITOP_WORD; CLKS(5,5,4); tmp2 = (fetch()) & 0xf;    BIT_NOT;                PutbackRMWord(ModRM,tmp);   break; /* Not */

		case 0x20 : ADD4S; CLKS(7,7,2); break;
		case 0x22 : SUB4S; CLKS(7,7,2); break;
		case 0x25 : MOVSPA; CLK(16); break;
		case 0x26 : CMP4S; CLKS(7,7,2); break;
		case 0x28 : ModRM = fetch(); tmp = GetRMByte(ModRM); tmp <<= 4; tmp |= Breg(AL) & 0xf; Breg(AL) = (Breg(AL) & 0xf0) | ((tmp>>8)&0xf); tmp &= 0xff; PutbackRMByte(ModRM,tmp); CLKM(13,13,9,28,28,15); break;
		case 0x2a : ModRM = fetch(); tmp = GetRMByte(ModRM); tmp2 = (Breg(AL) & 0xf)<<4; Breg(AL) = (Breg(AL) & 0xf0) | (tmp&0xf); tmp = tmp2 | (tmp>>4);   PutbackRMByte(ModRM,tmp); CLKM(17,17,13,32,32,19); break;
		case 0x2d : GetRB; nec_bankswitch(tmp); CLK(15); break;
		case 0x31 : ModRM = fetch(); ModRM=0; logerror("%06x: Unimplemented bitfield INS\n",PC()); break;
		case 0x33 : ModRM = fetch(); ModRM=0; logerror("%06x: Unimplemented bitfield EXT\n",PC()); break;
		case 0x91 : RETRBI; CLK(12); break;
		case 0x92 : FINT; CLK(2); m_no_interrupt = 1; break;
		case 0x94 : GetRB; TSKSW; CLK(20); break;
		case 0x95 : GetRB; MOVSPB; CLK(11); break;
		case 0x9e : logerror("%06x: STOP\n",PC()); m_icount=0; break;
		
		case 0x3c : logerror("%06x: Unknown V55 instruction\n",PC()); break; // BSCH
		case 0x3d : logerror("%06x: Unknown V55 instruction\n",PC()); break; // BSCH
		
		case 0x36 : logerror("%06x: Unknown V55 instruction\n",PC()); break; // DS3/VPC prefix/push
		// also mov if prefix?
		case 0x37 : logerror("%06x: Unknown V55 instruction\n",PC()); break; // DS3/VPC pop
		
		case 0x3e : logerror("%06x: Unknown V55 instruction\n",PC()); break; // DS2 prefix/push
		// also mov if prefix?
		case 0x3f : logerror("%06x: Unknown V55 instruction\n",PC()); break; // DS2 pop
		
		case 0x70 : logerror("%06x: Unknown V55 instruction\n",PC()); break; // QHOUT
		case 0x71 : logerror("%06x: Unknown V55 instruction\n",PC()); break; // QOUT
		case 0x72 : logerror("%06x: Unknown V55 instruction\n",PC()); break; // QTIN
		
		case 0x9c : logerror("%06x: Unknown V55 instruction\n",PC()); break; // BTCLR
		case 0x9d : logerror("%06x: Unknown V55 instruction\n",PC()); break; // BTCLRL
		
		default:    logerror("%06x: Unknown V55 instruction\n",PC()); break;
	}
}

OP( 0x63, i_v55_ds2    ) { logerror("%06x: Unimplemented DS2\n",PC()); }
OP( 0xd6, i_v55_ds3    ) { logerror("%06x: Unimplemented DS3\n",PC()); }

uint32_t v55_device::EAI_000() { m_EO=Wreg(BW)+Wreg(IX); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_001() { m_EO=Wreg(BW)+Wreg(IY); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_002() { m_EO=Wreg(BP)+Wreg(IX); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_003() { m_EO=Wreg(BP)+Wreg(IY); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_004() { m_EO=Wreg(IX); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_005() { m_EO=Wreg(IY); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_006() { m_EO=fetch(); m_EO+=fetch()<<8; m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_007() { m_EO=Wreg(BW); m_EA=m_EO; return m_EA; }

uint32_t v55_device::EAI_100() { m_EO=(Wreg(BW)+Wreg(IX)+(int8_t)fetch()); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_101() { m_EO=(Wreg(BW)+Wreg(IY)+(int8_t)fetch()); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_102() { m_EO=(Wreg(BP)+Wreg(IX)+(int8_t)fetch()); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_103() { m_EO=(Wreg(BP)+Wreg(IY)+(int8_t)fetch()); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_104() { m_EO=(Wreg(IX)+(int8_t)fetch()); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_105() { m_EO=(Wreg(IY)+(int8_t)fetch()); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_106() { m_EO=(Wreg(BP)+(int8_t)fetch()); m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_107() { m_EO=(Wreg(BW)+(int8_t)fetch()); m_EA=m_EO; return m_EA; }

uint32_t v55_device::EAI_200() { m_E16=fetch(); m_E16+=fetch()<<8; m_EO=Wreg(BW)+Wreg(IX)+(int16_t)m_E16; m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_201() { m_E16=fetch(); m_E16+=fetch()<<8; m_EO=Wreg(BW)+Wreg(IY)+(int16_t)m_E16; m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_202() { m_E16=fetch(); m_E16+=fetch()<<8; m_EO=Wreg(BP)+Wreg(IX)+(int16_t)m_E16; m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_203() { m_E16=fetch(); m_E16+=fetch()<<8; m_EO=Wreg(BP)+Wreg(IY)+(int16_t)m_E16; m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_204() { m_E16=fetch(); m_E16+=fetch()<<8; m_EO=Wreg(IX)+(int16_t)m_E16; m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_205() { m_E16=fetch(); m_E16+=fetch()<<8; m_EO=Wreg(IY)+(int16_t)m_E16; m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_206() { m_E16=fetch(); m_E16+=fetch()<<8; m_EO=Wreg(BP)+(int16_t)m_E16; m_EA=m_EO; return m_EA; }
uint32_t v55_device::EAI_207() { m_E16=fetch(); m_E16+=fetch()<<8; m_EO=Wreg(BW)+(int16_t)m_E16; m_EA=m_EO; return m_EA; }

const v55_device::nec_eahandler v55_device::s_GetEA_IRAM[192]=
{
	&v55_device::EAI_000, &v55_device::EAI_001, &v55_device::EAI_002, &v55_device::EAI_003, &v55_device::EAI_004, &v55_device::EAI_005, &v55_device::EAI_006, &v55_device::EAI_007,
	&v55_device::EAI_000, &v55_device::EAI_001, &v55_device::EAI_002, &v55_device::EAI_003, &v55_device::EAI_004, &v55_device::EAI_005, &v55_device::EAI_006, &v55_device::EAI_007,
	&v55_device::EAI_000, &v55_device::EAI_001, &v55_device::EAI_002, &v55_device::EAI_003, &v55_device::EAI_004, &v55_device::EAI_005, &v55_device::EAI_006, &v55_device::EAI_007,
	&v55_device::EAI_000, &v55_device::EAI_001, &v55_device::EAI_002, &v55_device::EAI_003, &v55_device::EAI_004, &v55_device::EAI_005, &v55_device::EAI_006, &v55_device::EAI_007,
	&v55_device::EAI_000, &v55_device::EAI_001, &v55_device::EAI_002, &v55_device::EAI_003, &v55_device::EAI_004, &v55_device::EAI_005, &v55_device::EAI_006, &v55_device::EAI_007,
	&v55_device::EAI_000, &v55_device::EAI_001, &v55_device::EAI_002, &v55_device::EAI_003, &v55_device::EAI_004, &v55_device::EAI_005, &v55_device::EAI_006, &v55_device::EAI_007,
	&v55_device::EAI_000, &v55_device::EAI_001, &v55_device::EAI_002, &v55_device::EAI_003, &v55_device::EAI_004, &v55_device::EAI_005, &v55_device::EAI_006, &v55_device::EAI_007,
	&v55_device::EAI_000, &v55_device::EAI_001, &v55_device::EAI_002, &v55_device::EAI_003, &v55_device::EAI_004, &v55_device::EAI_005, &v55_device::EAI_006, &v55_device::EAI_007,

	&v55_device::EAI_100, &v55_device::EAI_101, &v55_device::EAI_102, &v55_device::EAI_103, &v55_device::EAI_104, &v55_device::EAI_105, &v55_device::EAI_106, &v55_device::EAI_107,
	&v55_device::EAI_100, &v55_device::EAI_101, &v55_device::EAI_102, &v55_device::EAI_103, &v55_device::EAI_104, &v55_device::EAI_105, &v55_device::EAI_106, &v55_device::EAI_107,
	&v55_device::EAI_100, &v55_device::EAI_101, &v55_device::EAI_102, &v55_device::EAI_103, &v55_device::EAI_104, &v55_device::EAI_105, &v55_device::EAI_106, &v55_device::EAI_107,
	&v55_device::EAI_100, &v55_device::EAI_101, &v55_device::EAI_102, &v55_device::EAI_103, &v55_device::EAI_104, &v55_device::EAI_105, &v55_device::EAI_106, &v55_device::EAI_107,
	&v55_device::EAI_100, &v55_device::EAI_101, &v55_device::EAI_102, &v55_device::EAI_103, &v55_device::EAI_104, &v55_device::EAI_105, &v55_device::EAI_106, &v55_device::EAI_107,
	&v55_device::EAI_100, &v55_device::EAI_101, &v55_device::EAI_102, &v55_device::EAI_103, &v55_device::EAI_104, &v55_device::EAI_105, &v55_device::EAI_106, &v55_device::EAI_107,
	&v55_device::EAI_100, &v55_device::EAI_101, &v55_device::EAI_102, &v55_device::EAI_103, &v55_device::EAI_104, &v55_device::EAI_105, &v55_device::EAI_106, &v55_device::EAI_107,
	&v55_device::EAI_100, &v55_device::EAI_101, &v55_device::EAI_102, &v55_device::EAI_103, &v55_device::EAI_104, &v55_device::EAI_105, &v55_device::EAI_106, &v55_device::EAI_107,

	&v55_device::EAI_200, &v55_device::EAI_201, &v55_device::EAI_202, &v55_device::EAI_203, &v55_device::EAI_204, &v55_device::EAI_205, &v55_device::EAI_206, &v55_device::EAI_207,
	&v55_device::EAI_200, &v55_device::EAI_201, &v55_device::EAI_202, &v55_device::EAI_203, &v55_device::EAI_204, &v55_device::EAI_205, &v55_device::EAI_206, &v55_device::EAI_207,
	&v55_device::EAI_200, &v55_device::EAI_201, &v55_device::EAI_202, &v55_device::EAI_203, &v55_device::EAI_204, &v55_device::EAI_205, &v55_device::EAI_206, &v55_device::EAI_207,
	&v55_device::EAI_200, &v55_device::EAI_201, &v55_device::EAI_202, &v55_device::EAI_203, &v55_device::EAI_204, &v55_device::EAI_205, &v55_device::EAI_206, &v55_device::EAI_207,
	&v55_device::EAI_200, &v55_device::EAI_201, &v55_device::EAI_202, &v55_device::EAI_203, &v55_device::EAI_204, &v55_device::EAI_205, &v55_device::EAI_206, &v55_device::EAI_207,
	&v55_device::EAI_200, &v55_device::EAI_201, &v55_device::EAI_202, &v55_device::EAI_203, &v55_device::EAI_204, &v55_device::EAI_205, &v55_device::EAI_206, &v55_device::EAI_207,
	&v55_device::EAI_200, &v55_device::EAI_201, &v55_device::EAI_202, &v55_device::EAI_203, &v55_device::EAI_204, &v55_device::EAI_205, &v55_device::EAI_206, &v55_device::EAI_207,
	&v55_device::EAI_200, &v55_device::EAI_201, &v55_device::EAI_202, &v55_device::EAI_203, &v55_device::EAI_204, &v55_device::EAI_205, &v55_device::EAI_206, &v55_device::EAI_207
};

#define PutRMWord_IRAM(ModRM,val)                \
{                           \
	if (ModRM >= 0xc0)              \
		Wreg(Mod_RM.RM.w[ModRM])=val;   \
	else {                      \
		(this->*s_GetEA_IRAM[ModRM])();         \
		m_internal_ram[m_EA >> 1] = val;           \
	}                       \
}

#define GetRMWord_IRAM(ModRM) \
	((ModRM) >= 0xc0 ? Wreg(Mod_RM.RM.w[ModRM]) : ( (this->*s_GetEA_IRAM[ModRM])(), m_internal_ram[m_EA >> 1] ))

#define PutImmRMWord_IRAM(ModRM)                 \
{                           \
	WORD val;                   \
	if (ModRM >= 0xc0)              \
		Wreg(Mod_RM.RM.w[ModRM]) = fetchword(); \
	else {                      \
		(this->*s_GetEA_IRAM[ModRM])();         \
		val = fetchword();              \
		m_internal_ram[m_EA >> 1] = val;          \
	}                       \
}


OP( 0xF1, i_v55_iram   ) {
	uint8_t op = fetch();

	if (op == 0x89) { // mov_wr16
		uint16_t src;
		GetModRM;
		src = RegWord(ModRM);
		PutRMWord_IRAM(ModRM,src);
	}

	else if (op == 0x8b) { // mov_r16w
		uint16_t src;
		GetModRM;
		src = GetRMWord_IRAM(ModRM);
		RegWord(ModRM) = src;
	}

	else if (op == 0xa1) { // mov_axdisp
		uint32_t addr = fetchword();
		Wreg(AW) = m_internal_ram[addr >> 1];
	}
	
	else if (op == 0xa3) { // mov_dispax
		uint32_t addr = fetchword();
		m_internal_ram[addr >> 1] = Wreg(AW);
	}
	
	else if (op == 0xc7) { // mov_wd16
		GetModRM;
		PutImmRMWord_IRAM(ModRM);
	}
	
	else {
		printf("%06x: Unimplemented IRAM op: %02x\n", PC(), op);
	}
}
