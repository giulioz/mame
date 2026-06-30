// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
/*****************************************************************************
 *
 *   sh7021.cpp
 *   Portable Hitachi SH-1 (model SH7021) emulator
 *
 *****************************************************************************/

#include "emu.h"
#include "sh7021.h"
#include "sh_dasm.h"

#define LOG_INTC_RD (1u << 1)
#define LOG_INTC_WR (1u << 2)
#define LOG_UBC_RD  (1u << 3)
#define LOG_UBC_WR  (1u << 4)
#define LOG_BSC_RD  (1u << 5)
#define LOG_BSC_WR  (1u << 6)
#define LOG_DMA_RD  (1u << 7)
#define LOG_DMA_WR  (1u << 8)
#define LOG_ITU_RD  (1u << 9)
#define LOG_ITU_WR  (1u << 10)
#define LOG_TPC_RD  (1u << 11)
#define LOG_TPC_WR  (1u << 12)
#define LOG_WDT_RD  (1u << 13)
#define LOG_WDT_WR  (1u << 14)
#define LOG_SCI_RD  (1u << 15)
#define LOG_SCI_WR  (1u << 16)
#define LOG_PFC_RD  (1u << 17)
#define LOG_PFC_WR  (1u << 18)
#define LOG_INTC    (LOG_INTC_RD | LOG_INTC_WR)
#define LOG_UBC     (LOG_UBC_RD | LOG_UBC_WR)
#define LOG_BSC     (LOG_BSC_RD | LOG_BSC_WR)
#define LOG_DMA     (LOG_DMA_RD | LOG_DMA_WR)
#define LOG_ITU     (LOG_ITU_RD | LOG_ITU_WR)
#define LOG_TPC     (LOG_TPC_RD | LOG_TPC_WR)
#define LOG_WDT     (LOG_WDT_RD | LOG_WDT_WR)
#define LOG_SCI     (LOG_SCI_RD | LOG_SCI_WR)
#define LOG_PFC     (LOG_PFC_RD | LOG_PFC_WR)
#define LOG_ALL     (LOG_INTC | LOG_UBC | LOG_BSC | LOG_DMA | LOG_ITU | LOG_TPC | LOG_WDT | LOG_SCI | LOG_PFC)

#define VERBOSE (0)
#include "logmacro.h"


DEFINE_DEVICE_TYPE(SH7021, sh7021_device, "sh7021", "Hitachi SH7021")

/*-------------------------------------------------
    internal_map - maps SH7021 built-ins
-------------------------------------------------*/

void sh7021_device::internal_map(address_map &map)
{
	map(0x00000000, 0x00007fff).rom().region(DEVICE_SELF, 0).mirror(0x00ff8000); // 32KB internal ROM

	map(0x05fffec0, 0x05fffec0).rw(FUNC(sh7021_device::sci_smr_r<0>), FUNC(sh7021_device::sci_smr_w<0>));
	map(0x05fffec1, 0x05fffec1).rw(FUNC(sh7021_device::sci_brr_r<0>), FUNC(sh7021_device::sci_brr_w<0>));
	map(0x05fffec2, 0x05fffec2).rw(FUNC(sh7021_device::sci_scr_r<0>), FUNC(sh7021_device::sci_scr_w<0>));
	map(0x05fffec3, 0x05fffec3).rw(FUNC(sh7021_device::sci_tdr_r<0>), FUNC(sh7021_device::sci_tdr_w<0>));
	map(0x05fffec4, 0x05fffec4).rw(FUNC(sh7021_device::sci_ssr_r<0>), FUNC(sh7021_device::sci_ssr_w<0>));
	map(0x05fffec5, 0x05fffec5).r(FUNC(sh7021_device::sci_rdr_r<0>));
	map(0x05fffec8, 0x05fffec8).rw(FUNC(sh7021_device::sci_smr_r<1>), FUNC(sh7021_device::sci_smr_w<1>));
	map(0x05fffec9, 0x05fffec9).rw(FUNC(sh7021_device::sci_brr_r<1>), FUNC(sh7021_device::sci_brr_w<1>));
	map(0x05fffeca, 0x05fffeca).rw(FUNC(sh7021_device::sci_scr_r<1>), FUNC(sh7021_device::sci_scr_w<1>));
	map(0x05fffecb, 0x05fffecb).rw(FUNC(sh7021_device::sci_tdr_r<1>), FUNC(sh7021_device::sci_tdr_w<1>));
	map(0x05fffecc, 0x05fffecc).rw(FUNC(sh7021_device::sci_ssr_r<1>), FUNC(sh7021_device::sci_ssr_w<1>));
	map(0x05fffecd, 0x05fffecd).r(FUNC(sh7021_device::sci_rdr_r<1>));

	map(0x05ffff00, 0x05ffff00).rw(FUNC(sh7021_device::itu_tstr_r), FUNC(sh7021_device::itu_tstr_w));
	map(0x05ffff01, 0x05ffff01).rw(FUNC(sh7021_device::itu_tsnc_r), FUNC(sh7021_device::itu_tsnc_w));
	map(0x05ffff02, 0x05ffff02).rw(FUNC(sh7021_device::itu_tmdr_r), FUNC(sh7021_device::itu_tmdr_w));
	map(0x05ffff03, 0x05ffff03).rw(FUNC(sh7021_device::itu_tfcr_r), FUNC(sh7021_device::itu_tfcr_w));
	map(0x05ffff31, 0x05ffff31).rw(FUNC(sh7021_device::itu_tocr_r), FUNC(sh7021_device::itu_tocr_w));

	map(0x05ffff04, 0x05ffff04).rw(FUNC(sh7021_device::itu_tcr_r<0>), FUNC(sh7021_device::itu_tcr_w<0>));
	map(0x05ffff05, 0x05ffff05).rw(FUNC(sh7021_device::itu_tior_r<0>), FUNC(sh7021_device::itu_tior_w<0>));
	map(0x05ffff06, 0x05ffff06).rw(FUNC(sh7021_device::itu_tier_r<0>), FUNC(sh7021_device::itu_tier_w<0>));
	map(0x05ffff07, 0x05ffff07).rw(FUNC(sh7021_device::itu_tsr_r<0>), FUNC(sh7021_device::itu_tsr_w<0>));
	map(0x05ffff08, 0x05ffff09).rw(FUNC(sh7021_device::itu_tcnt_r<0>), FUNC(sh7021_device::itu_tcnt_w<0>));
	map(0x05ffff0a, 0x05ffff0b).rw(FUNC(sh7021_device::itu_gra_r<0>), FUNC(sh7021_device::itu_gra_w<0>));
	map(0x05ffff0c, 0x05ffff0d).rw(FUNC(sh7021_device::itu_grb_r<0>), FUNC(sh7021_device::itu_grb_w<0>));

	map(0x05ffff0e, 0x05ffff0e).rw(FUNC(sh7021_device::itu_tcr_r<1>), FUNC(sh7021_device::itu_tcr_w<1>));
	map(0x05ffff0f, 0x05ffff0f).rw(FUNC(sh7021_device::itu_tior_r<1>), FUNC(sh7021_device::itu_tior_w<1>));
	map(0x05ffff10, 0x05ffff10).rw(FUNC(sh7021_device::itu_tier_r<1>), FUNC(sh7021_device::itu_tier_w<1>));
	map(0x05ffff11, 0x05ffff11).rw(FUNC(sh7021_device::itu_tsr_r<1>), FUNC(sh7021_device::itu_tsr_w<1>));
	map(0x05ffff12, 0x05ffff13).rw(FUNC(sh7021_device::itu_tcnt_r<1>), FUNC(sh7021_device::itu_tcnt_w<1>));
	map(0x05ffff14, 0x05ffff15).rw(FUNC(sh7021_device::itu_gra_r<1>), FUNC(sh7021_device::itu_gra_w<1>));
	map(0x05ffff16, 0x05ffff17).rw(FUNC(sh7021_device::itu_grb_r<1>), FUNC(sh7021_device::itu_grb_w<1>));

	map(0x05ffff18, 0x05ffff18).rw(FUNC(sh7021_device::itu_tcr_r<2>), FUNC(sh7021_device::itu_tcr_w<2>));
	map(0x05ffff19, 0x05ffff19).rw(FUNC(sh7021_device::itu_tior_r<2>), FUNC(sh7021_device::itu_tior_w<2>));
	map(0x05ffff1a, 0x05ffff1a).rw(FUNC(sh7021_device::itu_tier_r<2>), FUNC(sh7021_device::itu_tier_w<2>));
	map(0x05ffff1b, 0x05ffff1b).rw(FUNC(sh7021_device::itu_tsr_r<2>), FUNC(sh7021_device::itu_tsr_w<2>));
	map(0x05ffff1c, 0x05ffff1d).rw(FUNC(sh7021_device::itu_tcnt_r<2>), FUNC(sh7021_device::itu_tcnt_w<2>));
	map(0x05ffff1e, 0x05ffff1f).rw(FUNC(sh7021_device::itu_gra_r<2>), FUNC(sh7021_device::itu_gra_w<2>));
	map(0x05ffff20, 0x05ffff21).rw(FUNC(sh7021_device::itu_grb_r<2>), FUNC(sh7021_device::itu_grb_w<2>));

	map(0x05ffff22, 0x05ffff22).rw(FUNC(sh7021_device::itu_tcr_r<3>), FUNC(sh7021_device::itu_tcr_w<3>));
	map(0x05ffff23, 0x05ffff23).rw(FUNC(sh7021_device::itu_tior_r<3>), FUNC(sh7021_device::itu_tior_w<3>));
	map(0x05ffff24, 0x05ffff24).rw(FUNC(sh7021_device::itu_tier_r<3>), FUNC(sh7021_device::itu_tier_w<3>));
	map(0x05ffff25, 0x05ffff25).rw(FUNC(sh7021_device::itu_tsr_r<3>), FUNC(sh7021_device::itu_tsr_w<3>));
	map(0x05ffff26, 0x05ffff27).rw(FUNC(sh7021_device::itu_tcnt_r<3>), FUNC(sh7021_device::itu_tcnt_w<3>));
	map(0x05ffff28, 0x05ffff29).rw(FUNC(sh7021_device::itu_gra_r<3>), FUNC(sh7021_device::itu_gra_w<3>));
	map(0x05ffff2a, 0x05ffff2b).rw(FUNC(sh7021_device::itu_grb_r<3>), FUNC(sh7021_device::itu_grb_w<3>));
	map(0x05ffff2c, 0x05ffff2d).rw(FUNC(sh7021_device::itu_bra_r<3>), FUNC(sh7021_device::itu_bra_w<3>));
	map(0x05ffff2e, 0x05ffff2f).rw(FUNC(sh7021_device::itu_brb_r<3>), FUNC(sh7021_device::itu_brb_w<3>));

	map(0x05ffff32, 0x05ffff32).rw(FUNC(sh7021_device::itu_tcr_r<4>), FUNC(sh7021_device::itu_tcr_w<4>));
	map(0x05ffff33, 0x05ffff33).rw(FUNC(sh7021_device::itu_tior_r<4>), FUNC(sh7021_device::itu_tior_w<4>));
	map(0x05ffff34, 0x05ffff34).rw(FUNC(sh7021_device::itu_tier_r<4>), FUNC(sh7021_device::itu_tier_w<4>));
	map(0x05ffff35, 0x05ffff35).rw(FUNC(sh7021_device::itu_tsr_r<4>), FUNC(sh7021_device::itu_tsr_w<4>));
	map(0x05ffff36, 0x05ffff37).rw(FUNC(sh7021_device::itu_tcnt_r<4>), FUNC(sh7021_device::itu_tcnt_w<4>));
	map(0x05ffff38, 0x05ffff39).rw(FUNC(sh7021_device::itu_gra_r<4>), FUNC(sh7021_device::itu_gra_w<4>));
	map(0x05ffff3a, 0x05ffff3b).rw(FUNC(sh7021_device::itu_grb_r<4>), FUNC(sh7021_device::itu_grb_w<4>));
	map(0x05ffff3c, 0x05ffff3d).rw(FUNC(sh7021_device::itu_bra_r<4>), FUNC(sh7021_device::itu_bra_w<4>));
	map(0x05ffff3e, 0x05ffff3f).rw(FUNC(sh7021_device::itu_brb_r<4>), FUNC(sh7021_device::itu_brb_w<4>));

	map(0x05ffff40, 0x05ffff43).rw(FUNC(sh7021_device::dma_sar_r<0>), FUNC(sh7021_device::dma_sar_w<0>));
	map(0x05ffff44, 0x05ffff47).rw(FUNC(sh7021_device::dma_dar_r<0>), FUNC(sh7021_device::dma_dar_w<0>));
	map(0x05ffff48, 0x05ffff49).rw(FUNC(sh7021_device::dmaor_r), FUNC(sh7021_device::dmaor_w));
	map(0x05ffff4a, 0x05ffff4b).rw(FUNC(sh7021_device::dma_tcr_r<0>), FUNC(sh7021_device::dma_tcr_w<0>));
	map(0x05ffff4e, 0x05ffff4f).rw(FUNC(sh7021_device::dma_chcr_r<0>), FUNC(sh7021_device::dma_chcr_w<0>));

	map(0x05ffff50, 0x05ffff53).rw(FUNC(sh7021_device::dma_sar_r<1>), FUNC(sh7021_device::dma_sar_w<1>));
	map(0x05ffff54, 0x05ffff57).rw(FUNC(sh7021_device::dma_dar_r<1>), FUNC(sh7021_device::dma_dar_w<1>));
	map(0x05ffff5a, 0x05ffff5b).rw(FUNC(sh7021_device::dma_tcr_r<1>), FUNC(sh7021_device::dma_tcr_w<1>));
	map(0x05ffff5e, 0x05ffff5f).rw(FUNC(sh7021_device::dma_chcr_r<1>), FUNC(sh7021_device::dma_chcr_w<1>));

	map(0x05ffff60, 0x05ffff63).rw(FUNC(sh7021_device::dma_sar_r<2>), FUNC(sh7021_device::dma_sar_w<2>));
	map(0x05ffff64, 0x05ffff67).rw(FUNC(sh7021_device::dma_dar_r<2>), FUNC(sh7021_device::dma_dar_w<2>));
	map(0x05ffff6a, 0x05ffff6b).rw(FUNC(sh7021_device::dma_tcr_r<2>), FUNC(sh7021_device::dma_tcr_w<2>));
	map(0x05ffff6e, 0x05ffff6f).rw(FUNC(sh7021_device::dma_chcr_r<2>), FUNC(sh7021_device::dma_chcr_w<2>));

	map(0x05ffff70, 0x05ffff73).rw(FUNC(sh7021_device::dma_sar_r<3>), FUNC(sh7021_device::dma_sar_w<3>));
	map(0x05ffff74, 0x05ffff77).rw(FUNC(sh7021_device::dma_dar_r<3>), FUNC(sh7021_device::dma_dar_w<3>));
	map(0x05ffff7a, 0x05ffff7b).rw(FUNC(sh7021_device::dma_tcr_r<3>), FUNC(sh7021_device::dma_tcr_w<3>));
	map(0x05ffff7e, 0x05ffff7f).rw(FUNC(sh7021_device::dma_chcr_r<3>), FUNC(sh7021_device::dma_chcr_w<3>));

	map(0x05ffff84, 0x05ffff85).rw(FUNC(sh7021_device::intc_ipra_r), FUNC(sh7021_device::intc_ipra_w));
	map(0x05ffff86, 0x05ffff87).rw(FUNC(sh7021_device::intc_iprb_r), FUNC(sh7021_device::intc_iprb_w));
	map(0x05ffff88, 0x05ffff89).rw(FUNC(sh7021_device::intc_iprc_r), FUNC(sh7021_device::intc_iprc_w));
	map(0x05ffff8a, 0x05ffff8b).rw(FUNC(sh7021_device::intc_iprd_r), FUNC(sh7021_device::intc_iprd_w));
	map(0x05ffff8c, 0x05ffff8d).rw(FUNC(sh7021_device::intc_ipre_r), FUNC(sh7021_device::intc_ipre_w));
	map(0x05ffff8e, 0x05ffff8f).rw(FUNC(sh7021_device::intc_icr_r), FUNC(sh7021_device::intc_icr_w));

	map(0x05ffff90, 0x05ffff91).rw(FUNC(sh7021_device::ubc_barh_r), FUNC(sh7021_device::ubc_barh_w));
	map(0x05ffff92, 0x05ffff93).rw(FUNC(sh7021_device::ubc_barl_r), FUNC(sh7021_device::ubc_barl_w));
	map(0x05ffff94, 0x05ffff95).rw(FUNC(sh7021_device::ubc_bamrh_r), FUNC(sh7021_device::ubc_bamrh_w));
	map(0x05ffff96, 0x05ffff97).rw(FUNC(sh7021_device::ubc_bamrl_r), FUNC(sh7021_device::ubc_bamrl_w));
	map(0x05ffff98, 0x05ffff99).rw(FUNC(sh7021_device::ubc_bbr_r), FUNC(sh7021_device::ubc_bbr_w));

	map(0x05ffffa0, 0x05ffffa1).rw(FUNC(sh7021_device::bsc_bcr_r), FUNC(sh7021_device::bsc_bcr_w));
	map(0x05ffffa2, 0x05ffffa3).rw(FUNC(sh7021_device::bsc_wcr1_r), FUNC(sh7021_device::bsc_wcr1_w));
	map(0x05ffffa4, 0x05ffffa5).rw(FUNC(sh7021_device::bsc_wcr2_r), FUNC(sh7021_device::bsc_wcr2_w));
	map(0x05ffffa6, 0x05ffffa7).rw(FUNC(sh7021_device::bsc_wcr3_r), FUNC(sh7021_device::bsc_wcr3_w));
	map(0x05ffffa8, 0x05ffffa9).rw(FUNC(sh7021_device::bsc_dcr_r), FUNC(sh7021_device::bsc_dcr_w));
	map(0x05ffffaa, 0x05ffffab).rw(FUNC(sh7021_device::bsc_pcr_r), FUNC(sh7021_device::bsc_pcr_w));
	map(0x05ffffac, 0x05ffffad).rw(FUNC(sh7021_device::bsc_rcr_r), FUNC(sh7021_device::bsc_rcr_w));
	map(0x05ffffae, 0x05ffffaf).rw(FUNC(sh7021_device::bsc_rtcsr_r), FUNC(sh7021_device::bsc_rtcsr_w));
	map(0x05ffffb0, 0x05ffffb1).rw(FUNC(sh7021_device::bsc_rtcnt_r), FUNC(sh7021_device::bsc_rtcnt_w));
	map(0x05ffffb2, 0x05ffffb3).rw(FUNC(sh7021_device::bsc_rtcor_r), FUNC(sh7021_device::bsc_rtcor_w));

	map(0x05ffffb8, 0x05ffffb8).r(FUNC(sh7021_device::wdt_tcsr_r));
	map(0x05ffffb9, 0x05ffffb9).r(FUNC(sh7021_device::wdt_tcnt_r));
	map(0x05ffffb8, 0x05ffffb9).w(FUNC(sh7021_device::wdt_tcsr_tcnt_w));
	map(0x05ffffbb, 0x05ffffbb).r(FUNC(sh7021_device::wdt_rstcsr_r));
	map(0x05ffffba, 0x05ffffbb).w(FUNC(sh7021_device::wdt_rstcsr_w));

	map(0x05ffffc0, 0x05ffffc1).rw(FUNC(sh7021_device::pfc_padr_r), FUNC(sh7021_device::pfc_padr_w));
	map(0x05ffffc2, 0x05ffffc3).rw(FUNC(sh7021_device::pfc_pbdr_r), FUNC(sh7021_device::pfc_pbdr_w));
	map(0x05ffffc4, 0x05ffffc5).rw(FUNC(sh7021_device::pfc_paior_r), FUNC(sh7021_device::pfc_paior_w));
	map(0x05ffffc6, 0x05ffffc7).rw(FUNC(sh7021_device::pfc_pbior_r), FUNC(sh7021_device::pfc_pbior_w));
	map(0x05ffffc8, 0x05ffffc9).rw(FUNC(sh7021_device::pfc_pacr1_r), FUNC(sh7021_device::pfc_pacr1_w));
	map(0x05ffffca, 0x05ffffcb).rw(FUNC(sh7021_device::pfc_pacr2_r), FUNC(sh7021_device::pfc_pacr2_w));
	map(0x05ffffcc, 0x05ffffcd).rw(FUNC(sh7021_device::pfc_pbcr1_r), FUNC(sh7021_device::pfc_pbcr1_w));
	map(0x05ffffce, 0x05ffffcf).rw(FUNC(sh7021_device::pfc_pbcr2_r), FUNC(sh7021_device::pfc_pbcr2_w));

	map(0x05fffff0, 0x05fffff0).rw(FUNC(sh7021_device::tpc_tpmr_r), FUNC(sh7021_device::tpc_tpmr_w));
	map(0x05fffff1, 0x05fffff1).rw(FUNC(sh7021_device::tpc_tpcr_r), FUNC(sh7021_device::tpc_tpcr_w));
	map(0x05fffff2, 0x05fffff2).rw(FUNC(sh7021_device::tpc_ndera_r), FUNC(sh7021_device::tpc_ndera_w));
	map(0x05fffff3, 0x05fffff3).rw(FUNC(sh7021_device::tpc_nderb_r), FUNC(sh7021_device::tpc_nderb_w));
	map(0x05fffff4, 0x05fffff4).rw(FUNC(sh7021_device::tpc_ndrb_r), FUNC(sh7021_device::tpc_ndrb_w));
	map(0x05fffff5, 0x05fffff5).rw(FUNC(sh7021_device::tpc_ndra_r), FUNC(sh7021_device::tpc_ndra_w));
	map(0x05fffff6, 0x05fffff6).rw(FUNC(sh7021_device::tpc_ndrb_alt_r), FUNC(sh7021_device::tpc_ndrb_alt_w));
	map(0x05fffff7, 0x05fffff7).rw(FUNC(sh7021_device::tpc_ndra_alt_r), FUNC(sh7021_device::tpc_ndra_alt_w));

	map(0x07000000, 0x070003ff).ram().mirror(0x00fffc00); // 1KB internal RAM, actually at 0xf000000
}

sh7021_device::sh7021_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: sh2_device(mconfig, SH7021, tag, owner, clock, CPU_TYPE_SH2, address_map_constructor(FUNC(sh7021_device::internal_map), this), 28, 0xc7ffffff)
	, m_tpc_out(*this)
	, m_wdtovf(*this)
	, m_sci_tx(*this)
	, m_an_in(*this, 0)
	, m_pa_out(*this)
	, m_pb_out(*this)
	, m_pa_bit_out(*this)
	, m_pb_bit_out(*this)
{
	m_isdrc = false; // FIXME
}

sh7021_device::sh7021_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, address_map_constructor internal_map)
	: sh2_device(mconfig, type, tag, owner, clock, CPU_TYPE_SH2, internal_map.isnull() ? address_map_constructor(FUNC(sh7021_device::internal_map), this) : internal_map, 28, 0xc7ffffff)
	, m_tpc_out(*this)
	, m_wdtovf(*this)
	, m_sci_tx(*this)
	, m_an_in(*this, 0)
	, m_pa_out(*this)
	, m_pb_out(*this)
	, m_pa_bit_out(*this)
	, m_pb_bit_out(*this)
{
	m_isdrc = false; // FIXME
}

void sh7021_device::execute_run()
{
	int consumed_cycles = 0;
	do
	{
		int icount_before = m_sh2_state->icount;
		debugger_instruction_hook(m_sh2_state->pc);

		const uint16_t opcode = decrypted_read_word(m_sh2_state->pc >= 0x40000000 ? m_sh2_state->pc : m_sh2_state->pc & m_am);
		if (m_ubc.pending && ((m_sh2_state->sr >> 4) & 15) < 15)
		{
			check_pending_irq("SH703x user break");
			m_test_irq = 0;
			consumed_cycles = icount_before - m_sh2_state->icount;
			continue;
		}

		if (m_sh2_state->m_delay)
		{
			m_sh2_state->pc = m_sh2_state->m_delay;
			m_sh2_state->m_delay = 0;
		}
		else
			m_sh2_state->pc += 2;

		execute_peripherals(consumed_cycles);
		execute_one(opcode);

		if (m_test_irq && !m_sh2_state->m_delay)
		{
			check_pending_irq("mame_sh2_execute");
			m_test_irq = 0;
		}
		m_sh2_state->icount--;
		consumed_cycles = icount_before - m_sh2_state->icount;
	} while (m_sh2_state->icount > 0);
}

void sh7021_device::device_start()
{
	sh2_device::device_start();

	m_itu.timer[0].et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::sh7021_timer_callback<0>), this));
	m_itu.timer[1].et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::sh7021_timer_callback<1>), this));
	m_itu.timer[2].et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::sh7021_timer_callback<2>), this));
	m_itu.timer[3].et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::sh7021_timer_callback<3>), this));
	m_itu.timer[4].et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::sh7021_timer_callback<4>), this));

	for (uint32_t i = 0; i < 5; ++i)
	{
		m_itu.timer[i].et->adjust(attotime::never);
	}

	m_sci[0].et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::sh7021_sci_callback<0>), this));
	m_sci[1].et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::sh7021_sci_callback<1>), this));

	for (uint32_t i = 0; i < 2; ++i)
	{
		m_sci[i].et->adjust(attotime::never);
	}

	m_adc.et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::adc_conversion_complete), this));
	m_adc.et->adjust(attotime::never);
	m_wdt.et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::wdt_overflow), this));
	m_wdt.et->adjust(attotime::never);
	m_bsc.et = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(sh7021_device::bsc_refresh_timer), this));
	m_bsc.et->adjust(attotime::never);

	// Interrupt Controller (INTC)
	save_item(NAME(m_ext_irq_pending));
	save_item(NAME(m_ext_irq_state));
	save_item(NAME(m_nmi_input_state));
	save_item(NAME(m_ipra));
	save_item(NAME(m_iprb));
	save_item(NAME(m_iprc));
	save_item(NAME(m_iprd));
	save_item(NAME(m_ipre));
	save_item(NAME(m_icr));

	// User Break Controller (UBC)
	save_item(STRUCT_MEMBER(m_ubc, barh));
	save_item(STRUCT_MEMBER(m_ubc, barl));
	save_item(STRUCT_MEMBER(m_ubc, bamrh));
	save_item(STRUCT_MEMBER(m_ubc, bamrl));
	save_item(STRUCT_MEMBER(m_ubc, bbr));
	save_item(STRUCT_MEMBER(m_ubc, pending));

	// Bus State Controller (BSC)
	save_item(STRUCT_MEMBER(m_bsc, bcr));
	save_item(STRUCT_MEMBER(m_bsc, wcr1));
	save_item(STRUCT_MEMBER(m_bsc, wcr2));
	save_item(STRUCT_MEMBER(m_bsc, wcr3));
	save_item(STRUCT_MEMBER(m_bsc, dcr));
	save_item(STRUCT_MEMBER(m_bsc, pcr));
	save_item(STRUCT_MEMBER(m_bsc, rcr));
	save_item(STRUCT_MEMBER(m_bsc, rtcsr));
	save_item(STRUCT_MEMBER(m_bsc, rtcsr_read));
	save_item(STRUCT_MEMBER(m_bsc, rtcnt));
	save_item(STRUCT_MEMBER(m_bsc, rtcor));

	// DMA Controller (DMAC)
	save_item(STRUCT_MEMBER(m_dma, sar));
	save_item(STRUCT_MEMBER(m_dma, dar));
	save_item(STRUCT_MEMBER(m_dma, tcr));
	save_item(STRUCT_MEMBER(m_dma, chcr));
	save_item(STRUCT_MEMBER(m_dma, te_read));
	save_item(NAME(m_dmaor));
	save_item(NAME(m_dma_cycles));

	// 16-Bit Integrated-Timer Pulse Unit (ITU)
	save_item(STRUCT_MEMBER(m_itu, tstr));
	save_item(STRUCT_MEMBER(m_itu, tsnc));
	save_item(STRUCT_MEMBER(m_itu, tmdr));
	save_item(STRUCT_MEMBER(m_itu, tfcr));
	save_item(STRUCT_MEMBER(m_itu, tocr));
	save_item(STRUCT_MEMBER(m_itu.timer, tcr));
	save_item(STRUCT_MEMBER(m_itu.timer, tior));
	save_item(STRUCT_MEMBER(m_itu.timer, tier));
	save_item(STRUCT_MEMBER(m_itu.timer, tsr));
	save_item(STRUCT_MEMBER(m_itu.timer, tsr_read));
	save_item(STRUCT_MEMBER(m_itu.timer, tcnt));
	save_item(STRUCT_MEMBER(m_itu.timer, gra));
	save_item(STRUCT_MEMBER(m_itu.timer, grb));
	save_item(STRUCT_MEMBER(m_itu.timer, bra));
	save_item(STRUCT_MEMBER(m_itu.timer, brb));

	// Programmable Timing Pattern Controller (TPC)
	save_item(STRUCT_MEMBER(m_tpc, tpmr));
	save_item(STRUCT_MEMBER(m_tpc, tpcr));
	save_item(STRUCT_MEMBER(m_tpc, ndera));
	save_item(STRUCT_MEMBER(m_tpc, nderb));
	save_item(STRUCT_MEMBER(m_tpc, ndra));
	save_item(STRUCT_MEMBER(m_tpc, ndrb));
	save_item(STRUCT_MEMBER(m_tpc, output));

	// Watchdog Timer (WDT)
	save_item(STRUCT_MEMBER(m_wdt, tcsr));
	save_item(STRUCT_MEMBER(m_wdt, tcnt));
	save_item(STRUCT_MEMBER(m_wdt, rstcsr));
	save_item(STRUCT_MEMBER(m_wdt, ovf_read));
	save_item(STRUCT_MEMBER(m_wdt, wovf_read));

	// Serial Communication Interface (SCI)
	save_item(STRUCT_MEMBER(m_sci, smr));
	save_item(STRUCT_MEMBER(m_sci, brr));
	save_item(STRUCT_MEMBER(m_sci, scr));
	save_item(STRUCT_MEMBER(m_sci, tsr));
	save_item(STRUCT_MEMBER(m_sci, tdr));
	save_item(STRUCT_MEMBER(m_sci, ssr));
	save_item(STRUCT_MEMBER(m_sci, ssr_read));
	save_item(STRUCT_MEMBER(m_sci, rsr));
	save_item(STRUCT_MEMBER(m_sci, rdr));
	save_item(STRUCT_MEMBER(m_sci, tx_busy));

	// A/D Converter
	save_item(STRUCT_MEMBER(m_adc, addr));
	save_item(STRUCT_MEMBER(m_adc, adcsr));
	save_item(STRUCT_MEMBER(m_adc, adcr));
	save_item(STRUCT_MEMBER(m_adc, channel));
	save_item(STRUCT_MEMBER(m_adc, adf_read));

	// Pin Function Controller (PFC)
	save_item(STRUCT_MEMBER(m_pfc, paior));
	save_item(STRUCT_MEMBER(m_pfc, pacr1));
	save_item(STRUCT_MEMBER(m_pfc, pacr2));
	save_item(STRUCT_MEMBER(m_pfc, pbior));
	save_item(STRUCT_MEMBER(m_pfc, pbcr1));
	save_item(STRUCT_MEMBER(m_pfc, pbcr2));
	save_item(STRUCT_MEMBER(m_pfc, padr));
	save_item(STRUCT_MEMBER(m_pfc, pbdr));
	save_item(STRUCT_MEMBER(m_pfc, padr_in));
	save_item(STRUCT_MEMBER(m_pfc, pbdr_in));
	save_item(STRUCT_MEMBER(m_pfc, cascr));
	save_item(STRUCT_MEMBER(m_pfc, pafunc));
	save_item(STRUCT_MEMBER(m_pfc, pbfunc));
	save_item(STRUCT_MEMBER(m_pfc, pa_gpio_mask));
	save_item(STRUCT_MEMBER(m_pfc, pb_gpio_mask));
	save_item(STRUCT_MEMBER(m_pfc, pcdr_in));
	save_item(NAME(m_sbycr));
	save_item(NAME(m_has_internal_rom));
}

void sh7021_device::device_reset()
{
	sh2_device::device_reset();

	// Interrupt Controller (INTC)
	m_ext_irq_pending = 0;
	m_ext_irq_state = 0;
	m_nmi_input_state = false;
	m_ipra = 0;
	m_iprb = 0;
	m_iprc = 0;
	m_iprd = 0;
	m_ipre = 0;
	m_icr = 0;

	// User Break Controller (UBC)
	m_ubc.barh = 0;
	m_ubc.barl = 0;
	m_ubc.bamrh = 0;
	m_ubc.bamrl = 0;
	m_ubc.bbr = 0;
	m_ubc.pending = false;

	// Bus State Controller (BSC)
	m_bsc.bcr = 0;
	m_bsc.wcr1 = 0xffff;
	m_bsc.wcr2 = 0xffff;
	m_bsc.wcr3 = 0xf800;
	m_bsc.dcr = 0;
	m_bsc.pcr = 0;
	m_bsc.rcr = 0;
	m_bsc.rtcsr = 0;
	m_bsc.rtcsr_read = false;
	m_bsc.rtcnt = 0;
	m_bsc.rtcor = 0x00ff;
	m_bsc.et->adjust(attotime::never);

	// DMA Controller (DMAC)
	for (uint32_t i = 0; i < 4; i++)
	{
		m_dma[i].sar = 0;
		m_dma[i].dar = 0;
		m_dma[i].tcr = 0;
		m_dma[i].chcr = 0;
		m_dma[i].te_read = false;
	}
	m_dmaor = 0;
	m_dma_cycles = 0;

	// 16-Bit Integrated-Timer Pulse Unit (ITU)
	m_itu.tstr = 0;
	m_itu.tsnc = 0;
	m_itu.tmdr = 0;
	m_itu.tfcr = 0;
	m_itu.tocr = 3;
	for (uint32_t i = 0; i < 5; i++)
	{
		m_itu.timer[i].et->adjust(attotime::never);
		m_itu.timer[i].tcr = 0;
		m_itu.timer[i].tior = 0;
		m_itu.timer[i].tier = 0;
		m_itu.timer[i].tsr = 0;
		m_itu.timer[i].tsr_read = 0;
		m_itu.timer[i].tcnt = 0;
		m_itu.timer[i].gra = 0xffff;
		m_itu.timer[i].grb = 0xffff;
		m_itu.timer[i].bra = 0xffff;
		m_itu.timer[i].brb = 0xffff;
	}

	// Programmable Timing Pattern Controller (TPC)
	m_tpc.tpmr = 0xf0;
	m_tpc.tpcr = 0xff;
	m_tpc.nderb = 0;
	m_tpc.ndera = 0;
	m_tpc.ndra = 0;
	m_tpc.ndrb = 0;
	m_tpc.output = 0;

	// Watchdog Timer (WDT)
	m_wdt.tcsr = 0x18;
	m_wdt.tcnt = 0;
	m_wdt.rstcsr = 0x1f;
	m_wdt.ovf_read = false;
	m_wdt.wovf_read = false;
	m_wdt.et->adjust(attotime::never);

	// Serial Communication Interface (SCI)
	for (uint32_t i = 0; i < 2; i++)
	{
		m_sci[i].et->adjust(attotime::never);
		m_sci[i].smr = 0;
		m_sci[i].brr = 0xff;
		m_sci[i].scr = 0;
		m_sci[i].tsr = 0;
		m_sci[i].tdr = 0xff;
		m_sci[i].ssr = 0x84;
		m_sci[i].ssr_read = 0;
		m_sci[i].rsr = 0;
		m_sci[i].rdr = 0;
		m_sci[i].tx_busy = false;
	}

	// A/D Converter
	m_adc.et->adjust(attotime::never);
	std::fill(std::begin(m_adc.addr), std::end(m_adc.addr), 0);
	m_adc.adcsr = 0;
	m_adc.adcr = 0x7f;
	m_adc.channel = 0;
	m_adc.adf_read = false;

	// Pin Function Controller (PFC)
	m_pfc.paior = 0;
	m_pfc.pacr1 = 0x3302;
	m_pfc.pacr2 = 0xff95;
	m_pfc.pbior = 0;
	m_pfc.pbcr1 = 0;
	m_pfc.pbcr2 = 0;
	m_pfc.padr = 0;
	m_pfc.pbdr = 0;
	m_pfc.padr_in = 0;
	m_pfc.pbdr_in = 0;
	m_pfc.cascr = 0x5fff;
	m_pfc.pa_gpio_mask = 0;
	m_pfc.pb_gpio_mask = 0xffff;
	m_pfc.pcdr_in = 0;
	m_sbycr = 0;

	static constexpr uint16_t PACR1_W_MASK = 0xfffd;
	static constexpr uint16_t PACR2_W_MASK = 0x55ff;
	for (int i = 0; i < 16; i++)
	{
		int bit = (i & 7) << 1;

		uint16_t data = (i >= 8 ? (m_pfc.pacr1 & PACR1_W_MASK) : (m_pfc.pacr2 & PACR2_W_MASK));
		uint8_t func = (data >> bit) & 3;
		m_pfc.pafunc[i] = (data >> bit) & 3;
		if (func == 0)
			m_pfc.pa_gpio_mask |= 1 << i;
		m_pfc.pbfunc[i] = 0;
	}

	recalc_irq();
}

uint8_t sh7021_device::read_byte(offs_t offset)
{
	ubc_check(offset, false, false, false, 1);
	consume_bus_cycles(offset, false);

	return m_program->read_byte(offset & m_am);
}

uint16_t sh7021_device::read_word(offs_t offset)
{
	ubc_check(offset, false, false, false, 2);
	consume_bus_cycles(offset, false);

	return m_program->read_word(offset & m_am);
}

uint32_t sh7021_device::read_long(offs_t offset)
{
	ubc_check(offset, false, false, false, 3);
	consume_bus_cycles(offset, false);

	return m_program->read_dword(offset & m_am);
}

uint16_t sh7021_device::decrypted_read_word(offs_t offset)
{
	ubc_check(offset, false, true, false, 2);
	consume_bus_cycles(offset, false);

	return m_decrypted_program->read_word(offset);
}

void sh7021_device::write_byte(offs_t offset, uint8_t data)
{
	ubc_check(offset, false, false, true, 1);
	consume_bus_cycles(offset, true);

	m_program->write_byte(offset & m_am, data);
}

void sh7021_device::write_word(offs_t offset, uint16_t data)
{
	ubc_check(offset, false, false, true, 2);
	consume_bus_cycles(offset, true);

	m_program->write_word(offset & m_am, data);
}

void sh7021_device::write_long(offs_t offset, uint32_t data)
{
	ubc_check(offset, false, false, true, 3);
	consume_bus_cycles(offset, true);

	m_program->write_dword(offset & m_am, data);
}

void sh7021_device::consume_bus_cycles(offs_t offset, bool write)
{
	const unsigned area = (offset >> 24) & 7;

	// On-chip ROM and RAM always complete in one state.  Peripheral module
	// accesses are fixed at three states.
	if ((area == 0 && m_has_internal_rom) || area == 7)
		return;
	if (area == 5)
	{
		m_sh2_state->icount -= 2;
		return;
	}

	if (write)
	{
		// CPU external writes use a fixed two-state bus cycle.  DRAM can use
		// short pitch when DRAME is set and WW1 is clear.
		if (area != 1 || !BIT(m_bsc.bcr, 15) || BIT(m_bsc.wcr1, 1))
			m_sh2_state->icount--;
		return;
	}

	if (area == 0 || area == 2)
		m_sh2_state->icount -= ((m_bsc.wcr3 >> 13) & 3) + 1;
	else if (area == 6)
		m_sh2_state->icount -= ((m_bsc.wcr3 >> 11) & 3) + 1;
	else if (BIT(m_bsc.wcr1, 8 + area))
		m_sh2_state->icount--;
}

void sh7021_device::execute_set_input(int inputnum, int state)
{
	if (inputnum == INPUT_LINE_NMI)
	{
		const bool old_state = m_nmi_input_state;
		m_nmi_input_state = state != CLEAR_LINE;

		// MAME's asserted state represents the active-low NMI pin.  Pulse the
		// base core only for the edge selected by NMIE.
		const bool falling_edge = !old_state && m_nmi_input_state;
		const bool rising_edge = old_state && !m_nmi_input_state;
		if ((!BIT(m_icr, 8) && falling_edge) || (BIT(m_icr, 8) && rising_edge))
		{
			sh2_device::execute_set_input(INPUT_LINE_NMI, ASSERT_LINE);
			sh2_device::execute_set_input(INPUT_LINE_NMI, CLEAR_LINE);
		}
		return;
	}

	if (inputnum >= 0 && inputnum <= 7)
	{
		const uint8_t mask = 1U << inputnum;
		const bool old_state = bool(m_ext_irq_state & mask);
		const bool new_state = state != CLEAR_LINE;

		if (new_state)
			m_ext_irq_state |= mask;
		else
			m_ext_irq_state &= ~mask;

		if (BIT(m_icr, inputnum))
		{
			// Edge-sensed requests remain pending until accepted by the CPU.
			if (!old_state && new_state)
				m_ext_irq_pending |= mask;
		}
		else if (new_state)
			m_ext_irq_pending |= mask;
		else
			m_ext_irq_pending &= ~mask;
		recalc_irq();
	}
}

void sh7021_device::sh2_exception_internal(const char *message, int irqline, int vector)
{
	if (vector == 12)
		m_ubc.pending = false;
	// Acceptance clears the interrupt controller's latch for edge-sensed IRQs.
	if (vector >= 64 && vector <= 71 && BIT(m_icr, vector - 64))
		m_ext_irq_pending &= ~(1U << (vector - 64));

	sh2_device::sh2_exception_internal(message, irqline, vector);
	recalc_irq();
}

void sh7021_device::recalc_irq()
{
	int irq = -1;
	int vector = -1;

	// Sources are visited in the hardware's default priority order.  Equal
	// programmed levels retain the first source, as specified by table 5.3.
	auto consider = [&irq, &vector] (int level, int candidate_vector)
	{
		if (level > 0 && level > irq)
		{
			irq = level;
			vector = candidate_vector;
		}
	};

	if (m_ubc.pending)
		consider(15, 12);

	// External IRQs (IRQ0-IRQ7)
	// IRQ0-3 priorities in IPRA, IRQ4-7 in IPRB
	// Vectors 64-71
	for (int i = 0; i < 8; i++)
	{
		if (!BIT(m_ext_irq_pending, i))
			continue;

		int level;
		if (i < 4)
			level = (m_ipra >> (12 - i * 4)) & 0xf;
		else
			level = (m_iprb >> (12 - (i - 4) * 4)) & 0xf;

		consider(level, 64 + i);
	}

	// DMA IRQs
	// DMAC0/1 priority in IPRC bits 15-12, DMAC2/3 in IPRC bits 11-8
	for (int i = 0; i < 4; i++)
	{
		// Check if DMA transfer ended (TE flag) and interrupt enabled (IE flag)
		if ((m_dma[i].chcr & 2) && (m_dma[i].chcr & 4))
		{
			int level = (i < 2) ? ((m_iprc >> 12) & 0xf) : ((m_iprc >> 8) & 0xf);
			consider(level, 72 + i * 2);
		}
	}

	// ITU IRQs
	for (uint32_t i = 0; i < 5; ++i)
	{
		const uint8_t pending = m_itu.timer[i].tier & m_itu.timer[i].tsr & 7;
		if (pending)
		{
			int level;

			switch (i)
			{
				case 0:
					level = (m_iprc >> 4) & 0xf;
					break;
				case 1:
					level = (m_iprc >> 0) & 0xf;
					break;
				case 2:
					level = (m_iprd >> 12) & 0xf;
					break;
				case 3:
					level = (m_iprd >> 8) & 0xf;
					break;
				case 4:
					level = (m_iprd >> 4) & 0xf;
					break;
			}

			for (uint32_t j = 0; j < 3; j++)
			{
				if (BIT(pending, j))
				{
					consider(level, 80 + i * 4 + j);
					break;
				}
			}
		}
	}

	// SCI IRQs, ordered ERI, RXI, TXI, TEI within each module.
	for (uint32_t i = 0; i < 2; ++i)
	{
		const int level = i ? ((m_ipre >> 12) & 0xf) : (m_iprd & 0xf);
		const int base = 100 + i * 4;
		if (BIT(m_sci[i].scr, 6) && (m_sci[i].ssr & 0x38))
			consider(level, base);
		else if (BIT(m_sci[i].scr, 6) && BIT(m_sci[i].ssr, 6))
			consider(level, base + 1);
		else if (BIT(m_sci[i].scr, 7) && BIT(m_sci[i].ssr, 7))
			consider(level, base + 2);
		else if (BIT(m_sci[i].scr, 2) && BIT(m_sci[i].ssr, 2))
			consider(level, base + 3);
	}

	// Parity error (vector 108) is generated by the BSC; A/D conversion end
	// is vector 109.  They share IPRE bits 11-8.
	if ((m_adc.adcsr & 0xc0) == 0xc0)
		consider((m_ipre >> 8) & 0xf, 109);

	if (!BIT(m_wdt.tcsr, 6) && BIT(m_wdt.tcsr, 7))
		consider((m_ipre >> 4) & 0xf, 112);
	if ((m_bsc.rtcsr & 0xc0) == 0xc0)
		consider((m_ipre >> 4) & 0xf, 113);

	m_sh2_state->internal_irq_level = irq;
	m_internal_irq_vector = vector;
	m_test_irq = 1;
}


// Interrupt Controller (INTC)

uint16_t sh7021_device::intc_ipra_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_INTC_RD, "%s: intc_ipra_r: %04x\n", machine().describe_context(), m_ipra);
	return m_ipra;
}

void sh7021_device::intc_ipra_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_INTC_WR, "%s: intc_ipra_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_ipra);
	recalc_irq();
}

uint16_t sh7021_device::intc_iprb_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_INTC_RD, "%s: intc_iprb_r: %04x\n", machine().describe_context(), m_iprb);
	return m_iprb;
}

void sh7021_device::intc_iprb_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_INTC_WR, "%s: intc_iprb_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_iprb);
	recalc_irq();
}

uint16_t sh7021_device::intc_iprc_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_INTC_RD, "%s: intc_iprc_r: %04x\n", machine().describe_context(), m_iprc);
	return m_iprc;
}

void sh7021_device::intc_iprc_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_INTC_WR, "%s: intc_iprc_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_iprc);
	recalc_irq();
}

uint16_t sh7021_device::intc_iprd_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_INTC_RD, "%s: intc_iprd_r: %04x\n", machine().describe_context(), m_iprd);
	return m_iprd;
}

void sh7021_device::intc_iprd_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_INTC_WR, "%s: intc_iprd_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_iprd);
	recalc_irq();
}

uint16_t sh7021_device::intc_ipre_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_INTC_RD, "%s: intc_ipre_r: %04x\n", machine().describe_context(), m_ipre);
	return m_ipre;
}

void sh7021_device::intc_ipre_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_INTC_WR, "%s: intc_ipre_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_ipre);
	recalc_irq();
}

uint16_t sh7021_device::intc_icr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_INTC_RD, "%s: intc_icr_r: %04x\n", machine().describe_context(), m_icr);
	return m_icr | (m_nmi_input_state ? 0 : 0x8000);
}

void sh7021_device::intc_icr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_INTC_WR, "%s: intc_icr_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	const uint16_t old_icr = m_icr;
	COMBINE_DATA(&m_icr);
	m_icr &= 0x01ff;

	// A level-sensed request always reflects the current pin state.  Edge
	// latches are retained when their sense mode is not changed.
	for (unsigned i = 0; i < 8; i++)
	{
		const uint8_t mask = 1U << i;
		if (BIT(old_icr, i) && !BIT(m_icr, i))
		{
			if (m_ext_irq_state & mask)
				m_ext_irq_pending |= mask;
			else
				m_ext_irq_pending &= ~mask;
		}
		else if (!BIT(old_icr, i) && BIT(m_icr, i))
		{
			// Merely changing the mode must not manufacture an edge.
			m_ext_irq_pending &= ~mask;
		}
	}
	recalc_irq();
}


// User Break Controller (UBC)

uint16_t sh7021_device::ubc_barh_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_UBC_RD, "%s: Break Address Register H, ubc_barh_r: %04x\n", machine().describe_context(), m_ubc.barh);
	return m_ubc.barh;
}

void sh7021_device::ubc_barh_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_UBC_WR, "%s: Break Address Register H, ubc_barh_w = %04x\n", machine().describe_context(), data);
	COMBINE_DATA(&m_ubc.barh);
}

uint16_t sh7021_device::ubc_barl_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_UBC_RD, "%s: Break Address Register L, ubc_barl_r: %04x\n", machine().describe_context(), m_ubc.barl);
	return m_ubc.barl;
}

void sh7021_device::ubc_barl_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_UBC_WR, "%s: Break Address Register L, ubc_barl_w = %04x\n", machine().describe_context(), data);
	COMBINE_DATA(&m_ubc.barl);
}

uint16_t sh7021_device::ubc_bamrh_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_UBC_RD, "%s: Break Address Mask Register H, ubc_bamrh_r: %04x\n", machine().describe_context(), m_ubc.bamrh);
	return m_ubc.bamrh;
}

void sh7021_device::ubc_bamrh_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_UBC_WR, "%s: Break Address Mask Register H, ubc_bmarh_w = %04x\n", machine().describe_context(), data);
	COMBINE_DATA(&m_ubc.bamrh);
}

uint16_t sh7021_device::ubc_bamrl_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_UBC_RD, "%s: Break Address Mask Register L, ubc_bamrl_r: %04x\n", machine().describe_context(), m_ubc.bamrl);
	return m_ubc.bamrl;
}

void sh7021_device::ubc_bamrl_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_UBC_WR, "%s: Break Address Mask Register L, ubc_bmarl_w = %04x\n", machine().describe_context(), data);
	COMBINE_DATA(&m_ubc.bamrl);
}

uint16_t sh7021_device::ubc_bbr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_UBC_RD, "%s: Break Bus Cycle Register, ubc_bbr_r: %04x\n", machine().describe_context(), m_ubc.bbr);
	return m_ubc.bbr;
}

void sh7021_device::ubc_bbr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	static const char *const CPU_DMA_NAMES[4] = { "No break", "Break on CPU cycles", "Break on DMA cycles", "Break on both CPU and DMA cycles" };
	static const char *const INSN_DATA_NAMES[4] = { "No break", "Break on instruction fetch cycles", "Break on data access cycles", "Break on both instruction and data cycles" };
	static const char *const READ_WRITE_NAMES[4] = { "No break", "Break on read cycles", "Break on write cycles", "Break on both read and write cycles" };
	static const char *const SIZE_NAMES[4] = { "No break", "Break on byte access", "Break on word access", "Break on long word access" };
	LOGMASKED(LOG_UBC_WR, "%s: Break Bus Cycle Register, ubc_bbr_w = %04x\n", machine().describe_context(), data);
	LOGMASKED(LOG_UBC_WR, "%s:         CPU/DMA Cycle Select: %s\n", machine().describe_context(), CPU_DMA_NAMES[(data >> 6) & 3]);
	LOGMASKED(LOG_UBC_WR, "%s:         Instruction/Data Fetch Select: %s\n", machine().describe_context(), INSN_DATA_NAMES[(data >> 4) & 3]);
	LOGMASKED(LOG_UBC_WR, "%s:         Read/Write Select: %s\n", machine().describe_context(), READ_WRITE_NAMES[(data >> 2) & 3]);
	LOGMASKED(LOG_UBC_WR, "%s:         Operand Size Select: %s\n", machine().describe_context(), SIZE_NAMES[data & 3]);
	COMBINE_DATA(&m_ubc.bbr);
	m_ubc.bbr &= 0x00ff;
}

void sh7021_device::ubc_check(uint32_t address, bool is_dma, bool is_instruction, bool is_write, unsigned size)
{
	const uint8_t conditions = m_ubc.bbr;
	if (!(is_dma ? BIT(conditions, 7) : BIT(conditions, 6)))
		return;
	if (!(is_instruction ? BIT(conditions, 4) : BIT(conditions, 5)))
		return;
	if (!(is_write ? BIT(conditions, 3) : BIT(conditions, 2)))
		return;

	const unsigned size_select = conditions & 3;
	if (size_select && size_select != size)
		return;

	const uint32_t break_address = uint32_t(m_ubc.barh) << 16 | m_ubc.barl;
	const uint32_t ignore_mask = uint32_t(m_ubc.bamrh) << 16 | m_ubc.bamrl;
	if (((address ^ break_address) & ~ignore_mask) != 0)
		return;

	m_ubc.pending = true;
	recalc_irq();
}


// Bus State Controller (BSC)

uint16_t sh7021_device::bsc_bcr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_BSC_RD, "%s: Bus Control Register, bsc_bcr_r: %04x\n", machine().describe_context(), m_bsc.bcr);
	return m_bsc.bcr;
}

void sh7021_device::bsc_bcr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_BSC_WR, "%s: Bus Control Register bsc_bcr_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	LOGMASKED(LOG_BSC_WR, "%s:         DRAM Enable: %d\n", machine().describe_context(), BIT(data, 15));
	LOGMASKED(LOG_BSC_WR, "%s:         Multiplexed I/O Enable: %d\n", machine().describe_context(), BIT(data, 14));
	LOGMASKED(LOG_BSC_WR, "%s:         WARP Enable: %d\n", machine().describe_context(), BIT(data, 13));
	LOGMASKED(LOG_BSC_WR, "%s:         /RD High Duty Cycle: %d% of T1 state\n", machine().describe_context(), BIT(data, 12) ? 35 : 50);
	LOGMASKED(LOG_BSC_WR, "%s:         Byte Access Select: %s\n", machine().describe_context(), BIT(data, 11) ? "/LBS, /WR, /HBS enabled" : "/WRH, /WRL, A0 enabled");
	COMBINE_DATA(&m_bsc.bcr);
}

uint16_t sh7021_device::bsc_wcr1_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_BSC_RD, "%s: Wait State Control Register 1, bsc_wcr1_r: %04x\n", machine().describe_context(), m_bsc.wcr1);
	return m_bsc.wcr1;
}

void sh7021_device::bsc_wcr1_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_BSC_WR, "%s: Wait State Control Register 1, bsc_wcr1_w = %04x\n", machine().describe_context(), data);
	COMBINE_DATA(&m_bsc.wcr1);
}

uint16_t sh7021_device::bsc_wcr2_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_BSC_RD, "%s: Wait State Control Register 2, bsc_wcr2_r: %04x\n", machine().describe_context(), m_bsc.wcr2);
	return m_bsc.wcr2;
}

void sh7021_device::bsc_wcr2_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_BSC_WR, "%s: Wait State Control Register 2, bsc_wcr2_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_bsc.wcr2);
}

uint16_t sh7021_device::bsc_wcr3_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_BSC_RD, "%s: Wait State Control Register 3, bsc_wcr3_r: %04x\n", machine().describe_context(), m_bsc.wcr3);
	return m_bsc.wcr3;
}

void sh7021_device::bsc_wcr3_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_BSC_WR, "%s: Wait State Control Register 3, bsc_wcr3_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_bsc.wcr3);
}

uint16_t sh7021_device::bsc_dcr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_BSC_RD, "%s: DRAM Control Register, bsc_dcr_r: %04x\n", machine().describe_context(), m_bsc.dcr);
	return m_bsc.dcr;
}

void sh7021_device::bsc_dcr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_BSC_WR, "%s: DRAM Control Register, bsc_dcr_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	LOGMASKED(LOG_BSC_WR, "%s:         Dual-WE Mode: %d\n", machine().describe_context(), BIT(data, 15));
	LOGMASKED(LOG_BSC_WR, "%s:         RAS Down Mode: %d\n", machine().describe_context(), BIT(data, 14));
	LOGMASKED(LOG_BSC_WR, "%s:         2-state Precharge: %d\n", machine().describe_context(), BIT(data, 13));
	LOGMASKED(LOG_BSC_WR, "%s:         Burst Enable: %d\n", machine().describe_context(), BIT(data, 12));
	LOGMASKED(LOG_BSC_WR, "%s:         /CAS High Duty Cycle: %d% of Tc state\n", machine().describe_context(), BIT(data, 11) ? 35 : 50);
	LOGMASKED(LOG_BSC_WR, "%s:         Row/Column Multiplex Enable: %d\n", machine().describe_context(), BIT(data, 10));
	LOGMASKED(LOG_BSC_WR, "%s:         Multiplex Shift Count: %d\n", machine().describe_context(), 8 + ((data >> 8) & 3));
	COMBINE_DATA(&m_bsc.dcr);
}

uint16_t sh7021_device::bsc_pcr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_BSC_RD, "%s: Parity Control Register, bsc_pcr_r: %04x\n", machine().describe_context(), m_bsc.pcr);
	return m_bsc.pcr;
}

void sh7021_device::bsc_pcr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	static const char *const PARITY_CHECK_NAMES[4] =
	{
		"Don't Check, Don't Generate",
		"Check and Generate in DRAM",
		"Check and Generate in DRAM and Area 2",
		"Reserved",
	};

	LOGMASKED(LOG_BSC_WR, "%s: Parity Control Register, bsc_pcr_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	LOGMASKED(LOG_BSC_WR, "%s:         Parity Error Clear: %d\n", machine().describe_context(), BIT(data, 15));
	LOGMASKED(LOG_BSC_WR, "%s:         Force Parity Error: %d\n", machine().describe_context(), BIT(data, 14));
	LOGMASKED(LOG_BSC_WR, "%s:         Parity Polarity: %s\n", machine().describe_context(), BIT(data, 13) ? "odd" : "even");
	LOGMASKED(LOG_BSC_WR, "%s:         Parity Check Enable: %s\n", machine().describe_context(), PARITY_CHECK_NAMES[(data >> 11) & 3]);
	LOGMASKED(LOG_BSC_WR, "%s:         Parity Error Clear: %d\n", machine().describe_context(), BIT(data, 15));
	COMBINE_DATA(&m_bsc.pcr);
}

uint16_t sh7021_device::bsc_rcr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_BSC_RD, "%s: Refresh Control Register, bsc_rcr_r: %04x\n", machine().describe_context(), m_bsc.rcr);
	return m_bsc.rcr;
}

void sh7021_device::bsc_rcr_w(uint16_t data)
{
	if ((data >> 8) != 0x5a)
		return;

	LOGMASKED(LOG_BSC_WR, "%s: Refresh Control Register, bsc_rcr_w = %02x\n", machine().describe_context(), (uint8_t)data);
	LOGMASKED(LOG_BSC_WR, "%s:         Refresh Control Enable: %d\n", machine().describe_context(), BIT(data, 7));
	LOGMASKED(LOG_BSC_WR, "%s:         Refresh Mode: %s\n", machine().describe_context(), BIT(data, 6) ? "Self-refresh" : "CAS-before-RAS");
	LOGMASKED(LOG_BSC_WR, "%s:         CBR Wait States: %d\n", machine().describe_context(), 1 + ((data >> 4) & 3));
	m_bsc.rcr = (uint8_t)data;
}

uint16_t sh7021_device::bsc_rtcsr_r()
{
	if (!machine().side_effects_disabled())
	{
		LOGMASKED(LOG_BSC_RD, "%s: Refresh Timer Control/Status Register, bsc_rtcsr_r: %04x\n", machine().describe_context(), m_bsc.rtcsr);
		if (BIT(m_bsc.rtcsr, 7))
			m_bsc.rtcsr_read = true;
	}
	return m_bsc.rtcsr;
}

void sh7021_device::bsc_rtcsr_w(uint16_t data)
{
	if ((data >> 8) != 0xa5)
		return;

	static const char *const RTCSR_CKS_NAMES[8] =
	{
		"Disabled", "/2", "/8", "/32", "/128", "/512", "/2048", "/4096"
	};

	LOGMASKED(LOG_BSC_WR, "%s: Refresh Timer Control/Status Register, bsc_rcr_w = %02x\n", machine().describe_context(), (uint8_t)data);
	LOGMASKED(LOG_BSC_WR, "%s:         Refresh Control Enable: %d\n", machine().describe_context(), BIT(data, 7));
	LOGMASKED(LOG_BSC_WR, "%s:         Compare Match Interrupt Enable: %d\n", machine().describe_context(), BIT(data, 6));
	LOGMASKED(LOG_BSC_WR, "%s:         Refresh Timer Counter Prescale: %d\n", machine().describe_context(), RTCSR_CKS_NAMES[(data >> 3) & 7]);
	const uint8_t current_count = bsc_rtcnt_r();
	m_bsc.rtcsr = (m_bsc.rtcsr & 0x80) | (uint8_t)(data & 0x7f);
	if (m_bsc.rtcsr_read && !BIT(data, 7))
	{
		m_bsc.rtcsr_read = false;
		m_bsc.rtcsr &= 0x7f;
	}
	m_bsc.rtcnt = current_count;
	bsc_refresh_schedule();
	recalc_irq();
}

uint16_t sh7021_device::bsc_rtcnt_r()
{
	uint8_t value = m_bsc.rtcnt;
	if (((m_bsc.rtcsr >> 3) & 7) != 0 && m_bsc.et->enabled())
		value += m_bsc.et->elapsed().as_ticks(clock()) / bsc_refresh_divider();
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_BSC_RD, "%s: Refresh Timer Count, bsc_rtcnt_r: %04x\n", machine().describe_context(), value);
	return value;
}

void sh7021_device::bsc_rtcnt_w(uint16_t data)
{
	if ((data >> 8) != 0x69)
		return;

	LOGMASKED(LOG_BSC_WR, "%s: Refresh Timer Count, bsc_rtcnt_w = %02x\n", machine().describe_context(), (uint8_t)data);
	m_bsc.rtcnt = (uint8_t)data;
	bsc_refresh_schedule();
}

uint16_t sh7021_device::bsc_rtcor_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_BSC_RD, "%s: Refresh Time Constant Register, bsc_rtcor_r: %04x\n", machine().describe_context(), m_bsc.rtcor);
	return m_bsc.rtcor;
}

void sh7021_device::bsc_rtcor_w(uint16_t data)
{
	if ((data >> 8) != 0x96)
		return;

	LOGMASKED(LOG_BSC_WR, "%s: Refresh Time Constant Register, bsc_rtcor_w = %02x\n", machine().describe_context(), (uint8_t)data);
	m_bsc.rtcor = (uint8_t)data;
	bsc_refresh_schedule();
}

unsigned sh7021_device::bsc_refresh_divider() const
{
	static constexpr unsigned divisors[8] = { 0, 2, 8, 32, 128, 512, 2048, 4096 };
	return divisors[(m_bsc.rtcsr >> 3) & 7];
}

void sh7021_device::bsc_refresh_schedule()
{
	const unsigned divider = bsc_refresh_divider();
	if (!divider)
	{
		m_bsc.et->adjust(attotime::never);
		return;
	}

	const uint8_t difference = m_bsc.rtcor - m_bsc.rtcnt;
	const uint64_t ticks = (uint64_t(difference) + 1) * divider;
	m_bsc.et->adjust(attotime::from_ticks(ticks, clock()));
}

TIMER_CALLBACK_MEMBER(sh7021_device::bsc_refresh_timer)
{
	m_bsc.rtcnt = 0;
	m_bsc.rtcsr |= 0x80;
	bsc_refresh_schedule();
	recalc_irq();
}


// DMA Controller (DMAC)

template uint32_t sh7021_device::dma_sar_r<0>();
template uint32_t sh7021_device::dma_sar_r<1>();
template uint32_t sh7021_device::dma_sar_r<2>();
template uint32_t sh7021_device::dma_sar_r<3>();

template <int Channel>
uint32_t sh7021_device::dma_sar_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_DMA_RD, "%s: DMA Source Address Register %d, dma_sar_r: %08x\n", machine().describe_context(), Channel, m_dma[Channel].sar);
	return m_dma[Channel].sar;
}

template void sh7021_device::dma_sar_w<0>(offs_t offset, uint32_t data, uint32_t mem_mask);
template void sh7021_device::dma_sar_w<1>(offs_t offset, uint32_t data, uint32_t mem_mask);
template void sh7021_device::dma_sar_w<2>(offs_t offset, uint32_t data, uint32_t mem_mask);
template void sh7021_device::dma_sar_w<3>(offs_t offset, uint32_t data, uint32_t mem_mask);

template <int Channel>
void sh7021_device::dma_sar_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	LOGMASKED(LOG_DMA_WR, "%s: DMA Source Address Register %d, dma_sar_w = %08x & %08x\n", machine().describe_context(), Channel, data, mem_mask);
	COMBINE_DATA(&m_dma[Channel].sar);
}

template uint32_t sh7021_device::dma_dar_r<0>();
template uint32_t sh7021_device::dma_dar_r<1>();
template uint32_t sh7021_device::dma_dar_r<2>();
template uint32_t sh7021_device::dma_dar_r<3>();

template <int Channel>
uint32_t sh7021_device::dma_dar_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_DMA_RD, "%s: DMA Destination Address Register %d, dma_sar_r: %08x\n", machine().describe_context(), Channel, m_dma[Channel].dar);
	return m_dma[Channel].dar;
}

template void sh7021_device::dma_dar_w<0>(offs_t offset, uint32_t data, uint32_t mem_mask);
template void sh7021_device::dma_dar_w<1>(offs_t offset, uint32_t data, uint32_t mem_mask);
template void sh7021_device::dma_dar_w<2>(offs_t offset, uint32_t data, uint32_t mem_mask);
template void sh7021_device::dma_dar_w<3>(offs_t offset, uint32_t data, uint32_t mem_mask);

template <int Channel>
void sh7021_device::dma_dar_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	LOGMASKED(LOG_DMA_WR, "%s: DMA Destination Address Register %d, dma_dar_w = %08x & %08x\n", machine().describe_context(), Channel, data, mem_mask);
	COMBINE_DATA(&m_dma[Channel].dar);
}

template uint16_t sh7021_device::dma_tcr_r<0>();
template uint16_t sh7021_device::dma_tcr_r<1>();
template uint16_t sh7021_device::dma_tcr_r<2>();
template uint16_t sh7021_device::dma_tcr_r<3>();

template <int Channel>
uint16_t sh7021_device::dma_tcr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_DMA_RD, "%s: DMA Transfer Count Register %d, dma_tcr_r: %04x\n", machine().describe_context(), Channel, m_dma[Channel].tcr);
	return m_dma[Channel].tcr;
}

template void sh7021_device::dma_tcr_w<0>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::dma_tcr_w<1>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::dma_tcr_w<2>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::dma_tcr_w<3>(offs_t offset, uint16_t data, uint16_t mem_mask);

template <int Channel>
void sh7021_device::dma_tcr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_DMA_WR, "%s: DMA Transfer Count Register %d, dma_tcr_w = %04x & %04x\n", machine().describe_context(), Channel, data, mem_mask);
	COMBINE_DATA(&m_dma[Channel].tcr);
}


template uint16_t sh7021_device::dma_chcr_r<0>();
template uint16_t sh7021_device::dma_chcr_r<1>();
template uint16_t sh7021_device::dma_chcr_r<2>();
template uint16_t sh7021_device::dma_chcr_r<3>();

template <int Channel>
uint16_t sh7021_device::dma_chcr_r()
{
	if (!machine().side_effects_disabled())
	{
		LOGMASKED(LOG_DMA_RD, "%s: DMA Channel Control Register %d, dma_chcr_r: %04x\n", machine().describe_context(), Channel, m_dma[Channel].chcr);
		if (BIT(m_dma[Channel].chcr, 1))
			m_dma[Channel].te_read = true;
	}
	return m_dma[Channel].chcr;
}

template void sh7021_device::dma_chcr_w<0>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::dma_chcr_w<1>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::dma_chcr_w<2>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::dma_chcr_w<3>(offs_t offset, uint16_t data, uint16_t mem_mask);

template <int Channel>
void sh7021_device::dma_chcr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	static const char *const ADDR_MODE_NAMES[4] =
	{
		"Fixed", "Increment", "Decrement", "Reserved"
	};
	static const char *const SOURCE_NAMES[16] =
	{
		"/DREQ, Dual-Address Mode",
		"Reserved (1)",
		"/DREQ, Memory-to-device",
		"/DREQ, Device-to-memory",
		"RXI0 (Serial 0 Receive), Dual-Address Mode",
		"TXI0 (Serial 0 Transmit), Dual-Address Mode",
		"RXI1 (Serial 1 Receive), Dual-Address Mode",
		"TXI1 (Serial 1 Transmit), Dual-Address Mode",
		"IMIA0 (Timer 0 Capture/Compare-Match), Dual-Address Mode",
		"IMIA1 (Timer 1 Capture/Compare-Match), Dual-Address Mode",
		"IMIA2 (Timer 2 Capture/Compare-Match), Dual-Address Mode",
		"IMIA3 (Timer 3 Capture/Compare-Match), Dual-Address Mode",
		"Auto-Request, Dual-Address Mode",
		"Reserved (13)",
		"Reserved (14)",
		"Reserved (15)"
	};

	LOGMASKED(LOG_DMA_WR, "%s: DMA Channel Control Register %d, dma_chcr_w = %04x & %04x\n", machine().describe_context(), Channel, data, mem_mask);
	LOGMASKED(LOG_DMA_WR, "%s:         Dest Address Mode: %s\n", machine().describe_context(), ADDR_MODE_NAMES[data >> 14]);
	LOGMASKED(LOG_DMA_WR, "%s:         Source Address Mode: %s\n", machine().describe_context(), ADDR_MODE_NAMES[(data >> 12) & 3]);
	LOGMASKED(LOG_DMA_WR, "%s:         Resource Select: %s\n", machine().describe_context(), SOURCE_NAMES[(data >> 8) & 15]);
	LOGMASKED(LOG_DMA_WR, "%s:         Acknowledge Bit: DACK output in %s cycle\n", machine().describe_context(), BIT(data, 7) ? "write" : "read");
	LOGMASKED(LOG_DMA_WR, "%s:         Acknowledge Level Bit: DACK active %s\n", machine().describe_context(), BIT(data, 6) ? "low" : "high");
	LOGMASKED(LOG_DMA_WR, "%s:         /DREQ Detect Mode: %s\n", machine().describe_context(), BIT(data, 5) ? "Edge" : "Level");
	LOGMASKED(LOG_DMA_WR, "%s:         Transfer Mode: %s\n", machine().describe_context(), BIT(data, 4) ? "Burst Mode" : "Cycle-Steal");
	LOGMASKED(LOG_DMA_WR, "%s:         Transfer Size: %s\n", machine().describe_context(), BIT(data, 3) ? "Word" : "Byte");
	LOGMASKED(LOG_DMA_WR, "%s:         Interrupt Enable: %d\n", machine().describe_context(), BIT(data, 2));
	LOGMASKED(LOG_DMA_WR, "%s:         Transfer End Clear: %d\n", machine().describe_context(), BIT(data, 1));
	LOGMASKED(LOG_DMA_WR, "%s:         Transfer Enable: %d\n", machine().describe_context(), BIT(data, 0));
	const bool old_te = BIT(m_dma[Channel].chcr, 1);
	COMBINE_DATA(&m_dma[Channel].chcr);
	// TE is a status flag: software may clear it after observing it, but may
	// not set it by writing one.
	const bool clear_te = old_te && m_dma[Channel].te_read && ACCESSING_BITS_0_7 && !BIT(data, 1);
	if (old_te && !clear_te)
		m_dma[Channel].chcr |= 2;
	else
		m_dma[Channel].chcr &= ~2;
	if (clear_te)
		m_dma[Channel].te_read = false;
	execute_dma(Channel);
	recalc_irq();
}

uint16_t sh7021_device::dmaor_r()
{
	if (!machine().side_effects_disabled())
	{
		LOGMASKED(LOG_DMA_RD, "%s: DMA Operation Register, dmaor_r: %04x\n", machine().describe_context(), m_dmaor);
		LOGMASKED(LOG_DMA_RD, "%s:         Address Error Flag: %d\n", machine().describe_context(), BIT(m_dmaor, 2));
		LOGMASKED(LOG_DMA_RD, "%s:         NMI Flag: %d\n", machine().describe_context(), BIT(m_dmaor, 1));
		LOGMASKED(LOG_DMA_RD, "%s:         DMA Master Enable: %d\n", machine().describe_context(), BIT(m_dmaor, 0));
	}
	return m_dmaor;
}

void sh7021_device::dmaor_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	static const char *const PRIORITY_NAMES[4] =
	{
		"Fixed Priority (0 -> 3 -> 2 -> 1)",
		"Fixed Priority (1 -> 3 -> 2 -> 0)",
		"Round-Robin Priority",
		"External-Pin Round-Robin Priority"
	};
	LOGMASKED(LOG_DMA_WR, "%s: DMA Operation Register, dmaor_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	LOGMASKED(LOG_DMA_WR, "%s:         Priority Mode: %s\n", machine().describe_context(), PRIORITY_NAMES[(data >> 8) & 3]);
	LOGMASKED(LOG_DMA_WR, "%s:         Address Error Flag: %d\n", machine().describe_context(), BIT(data, 2));
	LOGMASKED(LOG_DMA_WR, "%s:         NMI Flag: %d\n", machine().describe_context(), BIT(data, 1));
	LOGMASKED(LOG_DMA_WR, "%s:         DMA Master Enable: %d\n", machine().describe_context(), BIT(data, 0));
	COMBINE_DATA(&m_dmaor);
	for (int channel = 0; channel < 4; channel++)
		execute_dma(channel);
	recalc_irq();
}

void sh7021_device::execute_dma(int ch)
{
	const short dma_word_size[4] = { 0, +1, -1, 0 };
	uint8_t rs = (m_dma[ch].chcr >> 8) & 0xf; /**< Resource Select bits */

	// channel enable & master enable
	if ((m_dma[ch].chcr & 3) != 1 || (m_dmaor & 7) != 1)
	{
		return;
	}

	uint8_t dm = (m_dma[ch].chcr >> 14) & 3;  /**< Destination Address Mode bits */
	uint8_t sm = (m_dma[ch].chcr >> 12) & 3;  /**< Source Address Mode bits */
	bool ts = (m_dma[ch].chcr & 8);         /**< Transfer Size bit */
	int src_word_size = dma_word_size[sm] * (ts ? 2 : 1);
	int dst_word_size = dma_word_size[dm] * (ts ? 2 : 1);
	uint32_t src_addr = m_dma[ch].sar;
	uint32_t dst_addr = m_dma[ch].dar;
	uint32_t count = m_dma[ch].tcr;

	if (count == 0)
	{
		count = 0x10000;
	}

	if (!ts)
	{
		//printf("SH7032: DMA byte mode check\n");
		//printf("DMA%u: S:%08x D:%08x C:%08x CTRL:%04x\n", ch, m_dma[ch].sar, m_dma[ch].dar, m_dma[ch].tcr, m_dma[ch].chcr);
		//printf("SRC_INC: %d  DST_INC: %d\n", src_word_size, dst_word_size);
		//printf("MODE: %s\n\n", m_dma[ch].chcr & (1 << 4) ? "BURST" : "STEAL");
	}

	// Fake a a Transmit End Interrupt
	if (rs == 5 || rs == 7)
	{
		int channel = (rs == 5 ? 0 : 1);
		const uint64_t clock_divider = uint64_t(1) << ((m_sci[channel].smr & 3) * 2);
		const uint64_t clocks_per_character = BIT(m_sci[channel].smr, 7)
			? 4 * clock_divider * (m_sci[channel].brr + 1) * 8
			: 32 * clock_divider * (m_sci[channel].brr + 1)
				* (1 + (BIT(m_sci[channel].smr, 6) ? 7 : 8) + BIT(m_sci[channel].smr, 5) + (BIT(m_sci[channel].smr, 3) ? 2 : 1));
		attotime transmit_time = attotime::from_ticks(clocks_per_character * count, clock());
		m_sci[channel].et->adjust(transmit_time);
		m_sci[channel].tx_busy = true;
	}

	for (int i = 0; i < count; ++i)
	{
		ubc_check(src_addr, true, false, false, ts ? 2 : 1);
		ubc_check(dst_addr, true, false, true, ts ? 2 : 1);
		if (ts)
			m_program->write_word(dst_addr & m_am, m_program->read_word(src_addr & m_am));
		else
			m_program->write_byte(dst_addr & m_am, m_program->read_byte(src_addr & m_am));

		src_addr += src_word_size;
		dst_addr += dst_word_size;
	}

	m_dma[ch].sar = src_addr;
	m_dma[ch].dar = dst_addr;
	m_dma[ch].tcr = 0;

	m_dma[ch].chcr |= 2; // Transfer ended
	m_dma[ch].te_read = false;

	// Fire DMA transfer end interrupt if enabled (CHCR bit 2 = IE)
	recalc_irq();
}

void sh7021_device::execute_peripherals(int peripheral_cycles)
{
	m_dma_cycles += peripheral_cycles;
}

// 16-Bit Integrated-Timer Pulse Unit (ITU)

template TIMER_CALLBACK_MEMBER(sh7021_device::sh7021_timer_callback<0>);
template TIMER_CALLBACK_MEMBER(sh7021_device::sh7021_timer_callback<1>);
template TIMER_CALLBACK_MEMBER(sh7021_device::sh7021_timer_callback<2>);
template TIMER_CALLBACK_MEMBER(sh7021_device::sh7021_timer_callback<3>);
template TIMER_CALLBACK_MEMBER(sh7021_device::sh7021_timer_callback<4>);

template<int Which>
TIMER_CALLBACK_MEMBER(sh7021_device::sh7021_timer_callback)
{
	auto &timer = m_itu.timer[Which];
	timer.tcnt++;

	const bool overflow = timer.tcnt == 0;
	const bool match_a = timer.tcnt == timer.gra;
	const bool match_b = timer.tcnt == timer.grb;

	if (overflow)
	{
		LOGMASKED(LOG_ITU_WR, "Timer %d has overflowed, flagging OVF\n", Which);
		timer.tsr |= 4;
	}

	if (match_a)
	{
		LOGMASKED(LOG_ITU_WR, "Timer %d count %04x matches GRA %04x, flagging IMFA\n", Which, timer.tcnt, timer.gra);
		timer.tsr |= 1;
		tpc_trigger(Which);
	}

	if (match_b)
	{
		LOGMASKED(LOG_ITU_WR, "Timer %d count %04x matches GRB %04x, flagging IMFB\n", Which, timer.tcnt, timer.grb);
		timer.tsr |= 2;
	}

	const unsigned clear_mode = (timer.tcr >> 5) & 3;
	if ((clear_mode == 1 && match_a) || (clear_mode == 2 && match_b))
		timer.tcnt = 0;

	recalc_irq();
}

void sh7021_device::start_timer(int i)
{
	if (m_itu.timer[i].tcr & 4)
	{
		// External clock pins are not connected yet; keep the counter stopped
		// rather than terminating the machine.
		m_itu.timer[i].et->adjust(attotime::never);
		return;
	}

	int prescale = 1 << (m_itu.timer[i].tcr & 3);

//  printf("Starting timer %u: TCNT:%x GR:%x TCR:%x TI:%x TICK:%f\n", i, m_itu.timer[i].tcnt, m_itu.timer[i].gra, m_itu.timer[i].tcr & 7, m_itu.timer[i].tier, (double)clock()/psc);
//  printf("%d\n", clock());
	LOGMASKED(LOG_ITU_WR, "Starting Timer %d, prescale %d, clock %d, current count %04x\n", i, prescale, clock(), m_itu.timer[i].tcnt);
	attotime period = attotime::from_ticks(prescale, clock());
	m_itu.timer[i].et->adjust(period, i, period);
}

uint8_t sh7021_device::itu_tstr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Timer Start Register, itu_tstr_r: %02x\n", machine().describe_context(), m_itu.tstr);
	return (m_itu.tstr & 0x1f) | 0x60;
}

void sh7021_device::itu_tstr_w(uint8_t data)
{
	LOGMASKED(LOG_ITU_WR, "%s: Timer Start Register, itu_tstr_w = %02x\n", machine().describe_context(), data);

	// Starts timers
	data &= 0x1f;
	const uint8_t newly_enabled = ~m_itu.tstr & data;
	const uint8_t newly_disabled = m_itu.tstr & ~data;
	m_itu.tstr = data;

	for (int i = 0; i < 5; ++i)
	{
		if (BIT(newly_disabled, i))
		{
			LOGMASKED(LOG_ITU_WR, "%s: Timer Start Register, timer %d disabled\n", machine().describe_context(), i);
			m_itu.timer[i].et->adjust(attotime::never);
		}
		else if (BIT(newly_enabled, i))
		{
			LOGMASKED(LOG_ITU_WR, "%s: Timer Start Register, timer %d enabled\n", machine().describe_context(), i);
			start_timer(i);
		}
	}
}

uint8_t sh7021_device::itu_tsnc_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Timer Synchro Register, itu_tsnc_r: %02x\n", machine().describe_context(), m_itu.tsnc);
	return (m_itu.tsnc & 0x1f) | 0x60;
}

void sh7021_device::itu_tsnc_w(uint8_t data)
{
	LOGMASKED(LOG_ITU_WR, "%s: Timer Synchro Register, itu_tsnc_w = %02x\n", machine().describe_context(), data);
	m_itu.tsnc = data & 0x1f;
}

uint8_t sh7021_device::itu_tmdr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Timer Mode Register, itu_tmdr_r: %02x\n", machine().describe_context(), m_itu.tmdr);
	return m_itu.tmdr;
}

void sh7021_device::itu_tmdr_w(uint8_t data)
{
	LOGMASKED(LOG_ITU_WR, "%s: Timer Mode Register, itu_tmdr_w = %02x\n", machine().describe_context(), data);
	LOGMASKED(LOG_ITU_WR, "%s:         Channel 2 Phase Counting Mode: %d\n", machine().describe_context(), BIT(data, 6));
	LOGMASKED(LOG_ITU_WR, "%s:         Channel 2 Overflow Behavior: %d\n", machine().describe_context(), BIT(data, 5) ? "Set OVF of TSR2 on TCNT2 overflow" : "Set OVF of TSR2 on TCNT2 overflow and underflow");
	LOGMASKED(LOG_ITU_WR, "%s:         Channel 4 PWM Mode: %d\n", machine().describe_context(), BIT(data, 4));
	LOGMASKED(LOG_ITU_WR, "%s:         Channel 3 PWM Mode: %d\n", machine().describe_context(), BIT(data, 3));
	LOGMASKED(LOG_ITU_WR, "%s:         Channel 2 PWM Mode: %d\n", machine().describe_context(), BIT(data, 2));
	LOGMASKED(LOG_ITU_WR, "%s:         Channel 1 PWM Mode: %d\n", machine().describe_context(), BIT(data, 1));
	LOGMASKED(LOG_ITU_WR, "%s:         Channel 0 PWM Mode: %d\n", machine().describe_context(), BIT(data, 0));
	m_itu.tmdr = data & 0x7f;
}

uint8_t sh7021_device::itu_tfcr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Timer Function Control Register, itu_tfcr_r: %02x\n", machine().describe_context(), m_itu.tfcr);
	return (m_itu.tfcr & 0x3f) | 0x40;
}

void sh7021_device::itu_tfcr_w(uint8_t data)
{
	static const char *const COMBO_MODE_NAMES[4] =
	{
		"Channel 3 & 4 Normal (0)",
		"Channel 3 & 4 Normal (1)",
		"Channel 3 & 4 Together, Complementary PWM",
		"Channel 3 & 4 Together, Reset-Synced PWM"
	};
	LOGMASKED(LOG_ITU_WR, "%s: Timer Function Control Register, itu_tfcr_w = %02x\n", machine().describe_context(), data);
	LOGMASKED(LOG_ITU_WR, "%s:         Combination Mode: %s\n", machine().describe_context(), COMBO_MODE_NAMES[(data >> 4) & 3]);
	LOGMASKED(LOG_ITU_WR, "%s:         Buffer Mode B4: %s\n", machine().describe_context(), BIT(data, 3) ? "GRB4 and BRB4 buffer mode for Ch.4" : "GRB4 normal for Ch.4");
	LOGMASKED(LOG_ITU_WR, "%s:         Buffer Mode A4: %s\n", machine().describe_context(), BIT(data, 2) ? "GRA4 and BRA4 buffer mode for Ch.4" : "GRA4 normal for Ch.4");
	LOGMASKED(LOG_ITU_WR, "%s:         Buffer Mode B3: %s\n", machine().describe_context(), BIT(data, 1) ? "GRB3 and BRB3 buffer mode for Ch.3" : "GRB3 normal for Ch.3");
	LOGMASKED(LOG_ITU_WR, "%s:         Buffer Mode A3: %s\n", machine().describe_context(), BIT(data, 0) ? "GRA3 and BRA3 buffer mode for Ch.3" : "GRA3 normal for Ch.3");
	m_itu.tfcr = data & 0x3f;
}

uint8_t sh7021_device::itu_tocr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Timer Output Control Register, itu_tfcr_r: %02x\n", machine().describe_context(), m_itu.tocr);
	return (m_itu.tocr & 3) | 0x7c;
}

void sh7021_device::itu_tocr_w(uint8_t data)
{
	LOGMASKED(LOG_ITU_WR, "%s: Timer Output Control Register, itu_tocr_w = %02x\n", machine().describe_context(), data);
	LOGMASKED(LOG_ITU_WR, "%s:         Output Level Select Ch.4: %s\n", machine().describe_context(), BIT(data, 1) ? "Direct" : "Inverted");
	LOGMASKED(LOG_ITU_WR, "%s:         Output Level Select Ch.3: %s\n", machine().describe_context(), BIT(data, 0) ? "Direct" : "Inverted");
	m_itu.tocr = data & 3;
}

template uint8_t sh7021_device::itu_tcr_r<0>();
template uint8_t sh7021_device::itu_tcr_r<1>();
template uint8_t sh7021_device::itu_tcr_r<2>();
template uint8_t sh7021_device::itu_tcr_r<3>();
template uint8_t sh7021_device::itu_tcr_r<4>();

template <int Channel>
uint8_t sh7021_device::itu_tcr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Timer Control Register %d, itu_tcr_r: %02x\n", machine().describe_context(), Channel, m_itu.timer[Channel].tcr);
	return m_itu.timer[Channel].tcr & 0x7f;
}

template void sh7021_device::itu_tcr_w<0>(uint8_t data);
template void sh7021_device::itu_tcr_w<1>(uint8_t data);
template void sh7021_device::itu_tcr_w<2>(uint8_t data);
template void sh7021_device::itu_tcr_w<3>(uint8_t data);
template void sh7021_device::itu_tcr_w<4>(uint8_t data);

template <int Channel>
void sh7021_device::itu_tcr_w(uint8_t data)
{
	static const char *const CCLR_NAMES[4] =
	{
		"Don't clear TCNT", "Clear TCNT on GRA match or input capture", "Clear TCNT on GRB match or input capture", "Synced clear"
	};
	static const char *const CKEG_NAMES[4] =
	{
		"Count rising edges", "Count falling edges", "Count both edges (2)", "Count both edges (3)"
	};
	static const char *const TPSC_NAMES[8] =
	{
		"/1", "/2", "/4", "/8", "External clock A", "External clock B", "External clock C", "External clock D"
	};

	LOGMASKED(LOG_ITU_WR, "%s: Timer Control Register %d, itu_tcr_w = %02x\n", machine().describe_context(), Channel, data);
	LOGMASKED(LOG_ITU_WR, "%s:         Counter Clear Mode: %s\n", machine().describe_context(), CCLR_NAMES[(data >> 5) & 3]);
	LOGMASKED(LOG_ITU_WR, "%s:         External-Clock Edge Mode: %s\n", machine().describe_context(), CKEG_NAMES[(data >> 3) & 3]);
	LOGMASKED(LOG_ITU_WR, "%s:         Timer Prescaler Mode: %s\n", machine().describe_context(), TPSC_NAMES[data & 7]);
	m_itu.timer[Channel].tcr = data & 0x7f;
	if (BIT(m_itu.tstr, Channel))
		start_timer(Channel);
}

template uint8_t sh7021_device::itu_tior_r<0>();
template uint8_t sh7021_device::itu_tior_r<1>();
template uint8_t sh7021_device::itu_tior_r<2>();
template uint8_t sh7021_device::itu_tior_r<3>();
template uint8_t sh7021_device::itu_tior_r<4>();

template <int Channel>
uint8_t sh7021_device::itu_tior_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Timer I/O Control Register %d, itu_tior_r: %02x\n", machine().describe_context(), Channel, m_itu.timer[Channel].tior);
	return (m_itu.timer[Channel].tior & 0x77) | 0x08;
}

template void sh7021_device::itu_tior_w<0>(uint8_t data);
template void sh7021_device::itu_tior_w<1>(uint8_t data);
template void sh7021_device::itu_tior_w<2>(uint8_t data);
template void sh7021_device::itu_tior_w<3>(uint8_t data);
template void sh7021_device::itu_tior_w<4>(uint8_t data);

template <int Channel>
void sh7021_device::itu_tior_w(uint8_t data)
{
	static const char *const IOB_NAMES[8] =
	{
		"Output compare GRB / Compare Match",
		"Output compare GRB / 0 output at GRB match",
		"Output compare GRB / 1 output at GRB match",
		"Output compare GRB / Toggle at GRB match",
		"Input capture GRB / Capture rising edge",
		"Input capture GRB / Capture falling edge",
		"Input capture GRB / Capture both edges (6)",
		"Input capture GRB / Capture both edges (7)",
	};
	static const char *const IOA_NAMES[8] =
	{
		"Output compare GRA / Compare Match",
		"Output compare GRA / 0 output at GRA match",
		"Output compare GRA / 1 output at GRA match",
		"Output compare GRA / Toggle at GRA match",
		"Input capture GRA / Capture rising edge",
		"Input capture GRA / Capture falling edge",
		"Input capture GRA / Capture both edges (6)",
		"Input capture GRA / Capture both edges (7)",
	};

	LOGMASKED(LOG_ITU_WR, "%s: Timer I/O Control Register %d, itu_tior_w = %02x\n", machine().describe_context(), Channel, data);
	LOGMASKED(LOG_ITU_WR, "%s:         GRB Function: %s\n", machine().describe_context(), IOB_NAMES[(data >> 4) & 7]);
	LOGMASKED(LOG_ITU_WR, "%s:         GRA Function: %s\n", machine().describe_context(), IOA_NAMES[data & 7]);
	m_itu.timer[Channel].tior = data & 0x77;
}

template uint8_t sh7021_device::itu_tier_r<0>();
template uint8_t sh7021_device::itu_tier_r<1>();
template uint8_t sh7021_device::itu_tier_r<2>();
template uint8_t sh7021_device::itu_tier_r<3>();
template uint8_t sh7021_device::itu_tier_r<4>();

template <int Channel>
uint8_t sh7021_device::itu_tier_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Timer Interrupt Enable Register %d, itu_tier_r: %02x\n", machine().describe_context(), Channel, m_itu.timer[Channel].tier);
	return (m_itu.timer[Channel].tier & 7) | 0x78;
}

template void sh7021_device::itu_tier_w<0>(uint8_t data);
template void sh7021_device::itu_tier_w<1>(uint8_t data);
template void sh7021_device::itu_tier_w<2>(uint8_t data);
template void sh7021_device::itu_tier_w<3>(uint8_t data);
template void sh7021_device::itu_tier_w<4>(uint8_t data);

template <int Channel>
void sh7021_device::itu_tier_w(uint8_t data)
{
	LOGMASKED(LOG_ITU_WR, "%s: Timer Interrupt Enable Register %d, itu_tier_w = %02x\n", machine().describe_context(), Channel, data);
	LOGMASKED(LOG_ITU_WR, "%s:         Enable Overflow Interrupts: %d\n", machine().describe_context(), BIT(data, 2));
	LOGMASKED(LOG_ITU_WR, "%s:         Enable Capture/Compare B Interrupts: %d\n", machine().describe_context(), BIT(data, 1));
	LOGMASKED(LOG_ITU_WR, "%s:         Enable Capture/Compare A Interrupts: %d\n", machine().describe_context(), BIT(data, 0));
	m_itu.timer[Channel].tier = data & 7;
	recalc_irq();
}

template uint8_t sh7021_device::itu_tsr_r<0>();
template uint8_t sh7021_device::itu_tsr_r<1>();
template uint8_t sh7021_device::itu_tsr_r<2>();
template uint8_t sh7021_device::itu_tsr_r<3>();
template uint8_t sh7021_device::itu_tsr_r<4>();

template <int Channel>
uint8_t sh7021_device::itu_tsr_r()
{
	if (!machine().side_effects_disabled())
	{
		LOGMASKED(LOG_ITU_RD, "%s: Timer Status Register %d, itu_tsr_r: %02x\n", machine().describe_context(), Channel, m_itu.timer[Channel].tsr);
		LOGMASKED(LOG_ITU_RD, "%s:         Overflow Flag: %d\n", machine().describe_context(), BIT(m_itu.timer[Channel].tsr, 2));
		LOGMASKED(LOG_ITU_RD, "%s:         Compare/Capture B Flag: %d\n", machine().describe_context(), BIT(m_itu.timer[Channel].tsr, 1));
		LOGMASKED(LOG_ITU_RD, "%s:         Compare/Capture A Flag: %d\n", machine().describe_context(), BIT(m_itu.timer[Channel].tsr, 0));
		m_itu.timer[Channel].tsr_read |= m_itu.timer[Channel].tsr & 7;
	}
	return (m_itu.timer[Channel].tsr & 7) | 0x78;
}

template void sh7021_device::itu_tsr_w<0>(uint8_t data);
template void sh7021_device::itu_tsr_w<1>(uint8_t data);
template void sh7021_device::itu_tsr_w<2>(uint8_t data);
template void sh7021_device::itu_tsr_w<3>(uint8_t data);
template void sh7021_device::itu_tsr_w<4>(uint8_t data);

template <int Channel>
void sh7021_device::itu_tsr_w(uint8_t data)
{
	LOGMASKED(LOG_ITU_WR, "%s: Timer Status Register %d, itu_tsr_w = %02x\n", machine().describe_context(), Channel, m_itu.timer[Channel].tsr);
	if (BIT(m_itu.timer[Channel].tsr, 2) && !BIT(data, 2))
		LOGMASKED(LOG_ITU_WR, "%s:         Overflow Clear\n", machine().describe_context());
	if (BIT(m_itu.timer[Channel].tsr, 1) && !BIT(data, 1))
		LOGMASKED(LOG_ITU_WR, "%s:         Compare/Capture B Clear\n", machine().describe_context());
	if (BIT(m_itu.timer[Channel].tsr, 0) && !BIT(data, 0))
		LOGMASKED(LOG_ITU_WR, "%s:         Compare/Capture A Clear\n", machine().describe_context());
	const uint8_t clear = m_itu.timer[Channel].tsr_read & ~data & 7;
	m_itu.timer[Channel].tsr &= ~clear;
	m_itu.timer[Channel].tsr_read &= ~clear;
	recalc_irq();
}

template uint16_t sh7021_device::itu_tcnt_r<0>();
template uint16_t sh7021_device::itu_tcnt_r<1>();
template uint16_t sh7021_device::itu_tcnt_r<2>();
template uint16_t sh7021_device::itu_tcnt_r<3>();
template uint16_t sh7021_device::itu_tcnt_r<4>();

template <int Channel>
uint16_t sh7021_device::itu_tcnt_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Timer Counter %d, itu_tcnt_r: %04x\n", machine().describe_context(), Channel, m_itu.timer[Channel].tcnt);
	return m_itu.timer[Channel].tcnt;
}

template void sh7021_device::itu_tcnt_w<0>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_tcnt_w<1>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_tcnt_w<2>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_tcnt_w<3>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_tcnt_w<4>(offs_t offset, uint16_t data, uint16_t mem_mask);

template <int Channel>
void sh7021_device::itu_tcnt_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_ITU_WR, "%s: Timer Counter %d, itu_tcnt_w = %04x & %04x\n", machine().describe_context(), Channel, data, mem_mask);
	COMBINE_DATA(&m_itu.timer[Channel].tcnt);
}

template uint16_t sh7021_device::itu_gra_r<0>();
template uint16_t sh7021_device::itu_gra_r<1>();
template uint16_t sh7021_device::itu_gra_r<2>();
template uint16_t sh7021_device::itu_gra_r<3>();
template uint16_t sh7021_device::itu_gra_r<4>();

template <int Channel>
uint16_t sh7021_device::itu_gra_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: General Register A %d, itu_gra_r: %04x\n", machine().describe_context(), Channel, m_itu.timer[Channel].gra);
	return m_itu.timer[Channel].gra;
}

template void sh7021_device::itu_gra_w<0>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_gra_w<1>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_gra_w<2>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_gra_w<3>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_gra_w<4>(offs_t offset, uint16_t data, uint16_t mem_mask);

template <int Channel>
void sh7021_device::itu_gra_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_ITU_WR, "%s: General Register A %d, itu_gra_w = %04x\n", machine().describe_context(), Channel, data);
	COMBINE_DATA(&m_itu.timer[Channel].gra);
}

template uint16_t sh7021_device::itu_grb_r<0>();
template uint16_t sh7021_device::itu_grb_r<1>();
template uint16_t sh7021_device::itu_grb_r<2>();
template uint16_t sh7021_device::itu_grb_r<3>();
template uint16_t sh7021_device::itu_grb_r<4>();

template <int Channel>
uint16_t sh7021_device::itu_grb_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: General Register B %d, itu_grb_r: %04x\n", machine().describe_context(), Channel, m_itu.timer[Channel].grb);
	return m_itu.timer[Channel].grb;
}

template void sh7021_device::itu_grb_w<0>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_grb_w<1>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_grb_w<2>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_grb_w<3>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_grb_w<4>(offs_t offset, uint16_t data, uint16_t mem_mask);

template <int Channel>
void sh7021_device::itu_grb_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_ITU_WR, "%s: General Register B %d, itu_grb_w = %04x\n", machine().describe_context(), Channel, data);
	COMBINE_DATA(&m_itu.timer[Channel].grb);
}

template uint16_t sh7021_device::itu_bra_r<3>();
template uint16_t sh7021_device::itu_bra_r<4>();

template <int Channel>
uint16_t sh7021_device::itu_bra_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Buffer Register A %d, itu_bra_r: %04x\n", machine().describe_context(), Channel, m_itu.timer[Channel].bra);
	return m_itu.timer[Channel].bra;
}

template void sh7021_device::itu_bra_w<3>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_bra_w<4>(offs_t offset, uint16_t data, uint16_t mem_mask);

template <int Channel>
void sh7021_device::itu_bra_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_ITU_WR, "%s: Buffer Register A %d, itu_bra_w = %04x\n", machine().describe_context(), Channel, data);
	COMBINE_DATA(&m_itu.timer[Channel].bra);
}

template uint16_t sh7021_device::itu_brb_r<3>();
template uint16_t sh7021_device::itu_brb_r<4>();

template <int Channel>
uint16_t sh7021_device::itu_brb_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_ITU_RD, "%s: Buffer Register B %d, itu_brb_r: %04x\n", machine().describe_context(), Channel, m_itu.timer[Channel].brb);
	return m_itu.timer[Channel].brb;
}

template void sh7021_device::itu_brb_w<3>(offs_t offset, uint16_t data, uint16_t mem_mask);
template void sh7021_device::itu_brb_w<4>(offs_t offset, uint16_t data, uint16_t mem_mask);

template <int Channel>
void sh7021_device::itu_brb_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_ITU_WR, "%s: Buffer Register B %d, itu_brb_w = %04x\n", machine().describe_context(), Channel, data);
	COMBINE_DATA(&m_itu.timer[Channel].brb);
}


// Programmable Timing Pattern Controller (TPC)

uint8_t sh7021_device::tpc_tpmr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_TPC_RD, "%s: TPC Output Mode Register, tpc_tpmr_r: %02x\n", machine().describe_context(), m_tpc.tpmr);
	return m_tpc.tpmr;
}

void sh7021_device::tpc_tpmr_w(uint8_t data)
{
	LOGMASKED(LOG_TPC_WR, "%s: TPC Output Mode Register, tpc_tpmr_w = %02x\n", machine().describe_context(), data);
	LOGMASKED(LOG_TPC_WR, "%s:         TPC Output Group 3 Operation: %s\n", machine().describe_context(), BIT(data, 3) ? "Non-Overlap Mode (1/0 from A/B)" : "Normal (Compare-Match A)");
	LOGMASKED(LOG_TPC_WR, "%s:         TPC Output Group 2 Operation: %s\n", machine().describe_context(), BIT(data, 2) ? "Non-Overlap Mode (1/0 from A/B)" : "Normal (Compare-Match A)");
	LOGMASKED(LOG_TPC_WR, "%s:         TPC Output Group 1 Operation: %s\n", machine().describe_context(), BIT(data, 1) ? "Non-Overlap Mode (1/0 from A/B)" : "Normal (Compare-Match A)");
	LOGMASKED(LOG_TPC_WR, "%s:         TPC Output Group 0 Operation: %s\n", machine().describe_context(), BIT(data, 0) ? "Non-Overlap Mode (1/0 from A/B)" : "Normal (Compare-Match A)");
	m_tpc.tpmr = data;
}

uint8_t sh7021_device::tpc_tpcr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_TPC_RD, "%s: TPC Output Control Register, tpc_tpcr_r: %02x\n", machine().describe_context(), m_tpc.tpcr);
	return m_tpc.tpcr;
}

void sh7021_device::tpc_tpcr_w(uint8_t data)
{
	LOGMASKED(LOG_TPC_WR, "%s: TPC Output Control Register, tpc_tpcr_w = %02x\n", machine().describe_context(), data);
	LOGMASKED(LOG_TPC_WR, "%s:         TPC Output Group 3 output triggered by compare-match in ITU channel %d\n", machine().describe_context(), (data >> 6) & 3);
	LOGMASKED(LOG_TPC_WR, "%s:         TPC Output Group 2 output triggered by compare-match in ITU channel %d\n", machine().describe_context(), (data >> 4) & 3);
	LOGMASKED(LOG_TPC_WR, "%s:         TPC Output Group 1 output triggered by compare-match in ITU channel %d\n", machine().describe_context(), (data >> 2) & 3);
	LOGMASKED(LOG_TPC_WR, "%s:         TPC Output Group 0 output triggered by compare-match in ITU channel %d\n", machine().describe_context(), data & 3);
	m_tpc.tpcr = data;
}

uint8_t sh7021_device::tpc_ndera_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_TPC_RD, "%s: Next Data Enable Register A, tpc_ndera_r (TP7-0): %02x\n", machine().describe_context(), m_tpc.ndera);
	return m_tpc.ndera;
}

void sh7021_device::tpc_ndera_w(uint8_t data)
{
	LOGMASKED(LOG_TPC_WR, "%s: Next Data Enable Register A, tpc_ndera_w (TP7-0) = %02x\n", machine().describe_context(), data);
	m_tpc.ndera = data;
}

uint8_t sh7021_device::tpc_nderb_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_TPC_RD, "%s: Next Data Enable Register B, tpc_nderb_r (TP15-8): %02x\n", machine().describe_context(), m_tpc.nderb);
	return m_tpc.nderb;
}

void sh7021_device::tpc_nderb_w(uint8_t data)
{
	LOGMASKED(LOG_TPC_WR, "%s: Next Data Enable Register B, tpc_nderb_w (TP15-8) = %02x\n", machine().describe_context(), data);
	m_tpc.nderb = data;
}

uint8_t sh7021_device::tpc_ndra_r()
{
	const bool shared = (m_tpc.tpcr & 3) == ((m_tpc.tpcr >> 2) & 3);
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_TPC_RD, "%s: Next Data Register A, tpc_ndra_r: %02x\n", machine().describe_context(), m_tpc.ndra);
	return shared ? m_tpc.ndra : (m_tpc.ndra & 0xf0);
}

void sh7021_device::tpc_ndra_w(uint8_t data)
{
	LOGMASKED(LOG_TPC_WR, "%s: Next Data Register A, tpc_ndra_w = %02x\n", machine().describe_context(), data);
	if ((m_tpc.tpcr & 3) == ((m_tpc.tpcr >> 2) & 3))
		m_tpc.ndra = data;
	else
		m_tpc.ndra = (m_tpc.ndra & 0x0f) | (data & 0xf0);
}

uint8_t sh7021_device::tpc_ndra_alt_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_TPC_RD, "%s: Next Data Register A (alt. address), tpc_ndra_alt_r: %02x\n", machine().describe_context(), m_tpc.ndra & 0x0f);
	return m_tpc.ndra & 0x0f;
}

void sh7021_device::tpc_ndra_alt_w(uint8_t data)
{
	LOGMASKED(LOG_TPC_WR, "%s: Next Data Register A (alt. address), tpc_ndra_alt_w = %02x\n", machine().describe_context(), data);
	m_tpc.ndra = (m_tpc.ndra & 0xf0) | (data & 0x0f);
}

uint8_t sh7021_device::tpc_ndrb_r()
{
	const bool shared = ((m_tpc.tpcr >> 4) & 3) == ((m_tpc.tpcr >> 6) & 3);
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_TPC_RD, "%s: Next Data Register B, tpc_ndrb_r: %02x\n", machine().describe_context(), m_tpc.ndrb);
	return shared ? m_tpc.ndrb : (m_tpc.ndrb & 0xf0);
}

void sh7021_device::tpc_ndrb_w(uint8_t data)
{
	LOGMASKED(LOG_TPC_WR, "%s: Next Data Register B, tpc_ndrb_w = %02x\n", machine().describe_context(), data);
	if (((m_tpc.tpcr >> 4) & 3) == ((m_tpc.tpcr >> 6) & 3))
		m_tpc.ndrb = data;
	else
		m_tpc.ndrb = (m_tpc.ndrb & 0x0f) | (data & 0xf0);
}

uint8_t sh7021_device::tpc_ndrb_alt_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_TPC_RD, "%s: Next Data Register B (alt. address), tpc_ndrb_alt_r: %02x\n", machine().describe_context(), m_tpc.ndrb & 0x0f);
	return m_tpc.ndrb & 0x0f;
}

void sh7021_device::tpc_ndrb_alt_w(uint8_t data)
{
	LOGMASKED(LOG_TPC_WR, "%s: Next Data Register B (alt. address), tpc_ndrb_alt_w = %02x\n", machine().describe_context(), data);
	m_tpc.ndrb = (m_tpc.ndrb & 0xf0) | (data & 0x0f);
}

void sh7021_device::tpc_trigger(unsigned itu_channel)
{
	uint16_t enable = uint16_t(m_tpc.nderb) << 8 | m_tpc.ndera;
	const uint16_t next_data = uint16_t(m_tpc.ndrb) << 8 | m_tpc.ndra;
	uint16_t triggered = 0;
	for (unsigned group = 0; group < 4; group++)
	{
		if (((m_tpc.tpcr >> (group * 2)) & 3) == itu_channel)
			triggered |= 0x000f << (group * 4);
	}

	const uint16_t mask = enable & triggered;
	if (mask)
	{
		m_tpc.output = (m_tpc.output & ~mask) | (next_data & mask);
		m_tpc_out(m_tpc.output, mask);
	}
}


// Watchdog Timer (WDT)

unsigned sh7021_device::wdt_divider() const
{
	static constexpr unsigned divisors[8] = { 2, 64, 128, 256, 512, 1024, 4096, 8192 };
	return divisors[m_wdt.tcsr & 7];
}

uint8_t sh7021_device::wdt_tcsr_r()
{
	if (!machine().side_effects_disabled() && BIT(m_wdt.tcsr, 7))
		m_wdt.ovf_read = true;
	return m_wdt.tcsr | 0x18;
}

uint8_t sh7021_device::wdt_tcnt_r()
{
	if (!BIT(m_wdt.tcsr, 5))
		return 0;
	const uint64_t ticks = m_wdt.et->elapsed().as_ticks(clock()) / wdt_divider();
	return m_wdt.tcnt + ticks;
}

uint8_t sh7021_device::wdt_rstcsr_r()
{
	if (!machine().side_effects_disabled() && BIT(m_wdt.rstcsr, 7))
		m_wdt.wovf_read = true;
	return m_wdt.rstcsr | 0x1f;
}

void sh7021_device::wdt_tcsr_tcnt_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (mem_mask != 0xffff)
		return;

	if ((data >> 8) == 0x5a)
	{
		m_wdt.tcnt = data;
		if (BIT(m_wdt.tcsr, 5))
			wdt_schedule();
		return;
	}

	if ((data >> 8) != 0xa5)
		return;

	const uint8_t old = m_wdt.tcsr;
	const uint8_t current_count = wdt_tcnt_r();
	const bool clear_ovf = BIT(old, 7) && m_wdt.ovf_read && !BIT(data, 7);
	m_wdt.tcsr = (data & 0x67) | (clear_ovf ? 0 : (old & 0x80)) | 0x18;
	if (clear_ovf)
		m_wdt.ovf_read = false;

	if (!BIT(m_wdt.tcsr, 5))
	{
		m_wdt.tcnt = 0;
		m_wdt.et->adjust(attotime::never);
	}
	else
	{
		m_wdt.tcnt = BIT(old, 5) ? current_count : 0;
		wdt_schedule();
	}
	recalc_irq();
}

void sh7021_device::wdt_rstcsr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (mem_mask != 0xffff)
		return;

	if ((data >> 8) == 0xa5 && uint8_t(data) == 0 && m_wdt.wovf_read)
	{
		m_wdt.rstcsr &= ~0x80;
		m_wdt.wovf_read = false;
	}
	else if ((data >> 8) == 0x5a)
	{
		m_wdt.rstcsr = (m_wdt.rstcsr & 0x80) | (data & 0x60) | 0x1f;
	}
	recalc_irq();
}

void sh7021_device::wdt_schedule()
{
	const uint64_t ticks = uint64_t(0x100 - m_wdt.tcnt) * wdt_divider();
	m_wdt.et->adjust(attotime::from_ticks(ticks, clock()));
}

TIMER_CALLBACK_MEMBER(sh7021_device::wdt_overflow)
{
	m_wdt.tcnt = 0;
	if (BIT(m_wdt.tcsr, 6))
	{
		m_wdt.rstcsr |= 0x80;
		m_wdtovf(ASSERT_LINE);
		m_wdtovf(CLEAR_LINE);
		// Without an internal reset request, only the WDT itself is reset.
		// A board can use WDTOVF to reset the complete machine externally.
		m_wdt.tcsr = 0x18;
		m_wdt.et->adjust(attotime::never);
	}
	else
	{
		m_wdt.tcsr |= 0x80;
		wdt_schedule();
	}
	recalc_irq();
}

// Serial Communication Interface (SCI)

template TIMER_CALLBACK_MEMBER(sh7021_device::sh7021_sci_callback<0>);
template TIMER_CALLBACK_MEMBER(sh7021_device::sh7021_sci_callback<1>);

template<int Which>
TIMER_CALLBACK_MEMBER(sh7021_device::sh7021_sci_callback)
{
	auto &sci = m_sci[Which];
	m_sci_tx[Which](sci.tsr);
	sci.tx_busy = false;

	if (!BIT(sci.ssr, 7) && BIT(sci.scr, 5))
	{
		sci.tsr = sci.tdr;
		sci.ssr |= 0x80;
		sci_start_tx(Which);
	}
	else
	{
		sci.ssr |= 0x84;
	}
	recalc_irq();
}

template uint8_t sh7021_device::sci_smr_r<0>();
template uint8_t sh7021_device::sci_smr_r<1>();

template <int Channel>
uint8_t sh7021_device::sci_smr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_SCI_RD, "%s: Serial Mode Register %d, sci_smr_r: %02x\n", machine().describe_context(), Channel, m_sci[Channel].smr);
	return m_sci[Channel].smr;
}

template void sh7021_device::sci_smr_w<0>(uint8_t data);
template void sh7021_device::sci_smr_w<1>(uint8_t data);

template <int Channel>
void sh7021_device::sci_smr_w(uint8_t data)
{
	LOGMASKED(LOG_SCI_WR, "%s: Serial Mode Register %d, sci_smr_w = %02x\n", machine().describe_context(), Channel, data);
	LOGMASKED(LOG_SCI_WR, "%s:         Communication Mode: %s\n", machine().describe_context(), BIT(data, 7) ? "Clocked synchronous" : "Asynchronous");
	LOGMASKED(LOG_SCI_WR, "%s:         Character Length: %d\n", machine().describe_context(), BIT(data, 6) ? 7 : 8);
	LOGMASKED(LOG_SCI_WR, "%s:         Parity Enable: %d\n", machine().describe_context(), BIT(data, 5));
	LOGMASKED(LOG_SCI_WR, "%s:         Parity Mode: %s\n", machine().describe_context(), BIT(data, 4) ? "Odd" : "Even");
	LOGMASKED(LOG_SCI_WR, "%s:         Stop Bits: %d\n", machine().describe_context(), BIT(data, 3) ? 2 : 1);
	LOGMASKED(LOG_SCI_WR, "%s:         Multiprocessor Mode: %d\n", machine().describe_context(), BIT(data, 2));
	LOGMASKED(LOG_SCI_WR, "%s:         Clock Divider: %d\n", machine().describe_context(), 1 << ((data & 3) * 2));
	m_sci[Channel].smr = data;
}

template uint8_t sh7021_device::sci_brr_r<0>();
template uint8_t sh7021_device::sci_brr_r<1>();

template <int Channel>
uint8_t sh7021_device::sci_brr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_SCI_RD, "%s: Bit Rate Register %d, sci_brr_r: %02x\n", machine().describe_context(), Channel, m_sci[Channel].brr);
	return m_sci[Channel].brr;
}

template void sh7021_device::sci_brr_w<0>(uint8_t data);
template void sh7021_device::sci_brr_w<1>(uint8_t data);

template <int Channel>
void sh7021_device::sci_brr_w(uint8_t data)
{
	LOGMASKED(LOG_SCI_WR, "%s: Bit Rate Register %d, sci_brr_w = %02x\n", machine().describe_context(), Channel, data);
	m_sci[Channel].brr = data;
}

template uint8_t sh7021_device::sci_scr_r<0>();
template uint8_t sh7021_device::sci_scr_r<1>();

template <int Channel>
uint8_t sh7021_device::sci_scr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_SCI_RD, "%s: Serial Control Register %d, sci_scr_r: %02x\n", machine().describe_context(), Channel, m_sci[Channel].scr);
	return m_sci[Channel].scr;
}

template void sh7021_device::sci_scr_w<0>(uint8_t data);
template void sh7021_device::sci_scr_w<1>(uint8_t data);

template <int Channel>
void sh7021_device::sci_scr_w(uint8_t data)
{
	LOGMASKED(LOG_SCI_WR, "%s: Serial Control Register %d, sci_scr_w = %02x\n", machine().describe_context(), Channel, data);
	LOGMASKED(LOG_SCI_WR, "%s:         Transmit-Empty Interrupt Enable: %d\n", machine().describe_context(), BIT(data, 7));
	LOGMASKED(LOG_SCI_WR, "%s:         Receive-Full Interrupt Enable: %d\n", machine().describe_context(), BIT(data, 6));
	LOGMASKED(LOG_SCI_WR, "%s:         Transmit Enable: %d\n", machine().describe_context(), BIT(data, 5));
	LOGMASKED(LOG_SCI_WR, "%s:         Receive Enable: %d\n", machine().describe_context(), BIT(data, 4));
	LOGMASKED(LOG_SCI_WR, "%s:         Multiprocessor Interrupt Enable: %d\n", machine().describe_context(), BIT(data, 3));
	LOGMASKED(LOG_SCI_WR, "%s:         Transmit-End Interrupt Enable: %d\n", machine().describe_context(), BIT(data, 2));
	LOGMASKED(LOG_SCI_WR, "%s:         Clock Enable Mode: %d%d\n", machine().describe_context(), BIT(data, 1), BIT(data, 0));
	m_sci[Channel].scr = data;
	recalc_irq();
}

template uint8_t sh7021_device::sci_tdr_r<0>();
template uint8_t sh7021_device::sci_tdr_r<1>();

template <int Channel>
uint8_t sh7021_device::sci_tdr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_SCI_RD, "%s: Transmit Data Register %d, sci_tdr_r: %02x\n", machine().describe_context(), Channel, m_sci[Channel].tdr);
	return m_sci[Channel].tdr;
}

template void sh7021_device::sci_tdr_w<0>(uint8_t data);
template void sh7021_device::sci_tdr_w<1>(uint8_t data);

template <int Channel>
void sh7021_device::sci_tdr_w(uint8_t data)
{
	LOGMASKED(LOG_SCI_WR, "%s: Transmit Data Register %d, sci_tdr_w = %02x\n", machine().describe_context(), Channel, data);
	m_sci[Channel].tdr = data;
}

template uint8_t sh7021_device::sci_ssr_r<0>();
template uint8_t sh7021_device::sci_ssr_r<1>();

template <int Channel>
uint8_t sh7021_device::sci_ssr_r()
{
	if (!machine().side_effects_disabled())
	{
		LOGMASKED(LOG_SCI_RD, "%s: Serial Status Register %d, sci_ssr_r: %02x\n", machine().describe_context(), Channel, m_sci[Channel].ssr);
		LOGMASKED(LOG_SCI_RD, "%s:         Transmit Empty Flag: %d\n", machine().describe_context(), Channel, BIT(m_sci[Channel].ssr, 7));
		LOGMASKED(LOG_SCI_RD, "%s:         Receiver Full Flag: %d\n", machine().describe_context(), Channel, BIT(m_sci[Channel].ssr, 6));
		LOGMASKED(LOG_SCI_RD, "%s:         Overrun Error Flag: %d\n", machine().describe_context(), Channel, BIT(m_sci[Channel].ssr, 5));
		LOGMASKED(LOG_SCI_RD, "%s:         Framing Error Flag: %d\n", machine().describe_context(), Channel, BIT(m_sci[Channel].ssr, 4));
		LOGMASKED(LOG_SCI_RD, "%s:         Parity Error Flag: %d\n", machine().describe_context(), Channel, BIT(m_sci[Channel].ssr, 3));
		LOGMASKED(LOG_SCI_RD, "%s:         Transmit End Flag: %d\n", machine().describe_context(), Channel, BIT(m_sci[Channel].ssr, 2));
		LOGMASKED(LOG_SCI_RD, "%s:         Multiprocessor Bit Flag: %d\n", machine().describe_context(), Channel, BIT(m_sci[Channel].ssr, 1));
		LOGMASKED(LOG_SCI_RD, "%s:         Multiprocessor Bit Transfer Flag: %d\n", machine().describe_context(), Channel, BIT(m_sci[Channel].ssr, 0));

		m_sci[Channel].ssr_read |= m_sci[Channel].ssr & 0xf8;
	}

	return m_sci[Channel].ssr;
}

template void sh7021_device::sci_ssr_w<0>(uint8_t data);
template void sh7021_device::sci_ssr_w<1>(uint8_t data);

template <int Channel>
void sh7021_device::sci_ssr_w(uint8_t data)
{
	LOGMASKED(LOG_SCI_WR, "%s: Serial Status Register %d, sci_ssr_w = %02x\n", machine().describe_context(), Channel, data);
	if (!BIT(data, 7) && BIT(m_sci[Channel].ssr, 7) && BIT(m_sci[Channel].ssr_read, 7))
	{
		// Clearing TDRE transfers TDR to the shift register when possible.
		m_sci[Channel].ssr_read &= ~(1 << 7);
		m_sci[Channel].ssr &= ~(1 << 7);
		m_sci[Channel].ssr &= ~(1 << 2);
		if (!m_sci[Channel].tx_busy)
		{
			m_sci[Channel].tsr = m_sci[Channel].tdr;
			m_sci[Channel].ssr |= 0x80;
			sci_start_tx(Channel);
		}
	}

	if (!BIT(data, 6) && BIT(m_sci[Channel].ssr, 6) && BIT(m_sci[Channel].ssr_read, 6))
	{
		// Clear RDR-Full Flag
		m_sci[Channel].ssr_read &= ~(1 << 6);
		m_sci[Channel].ssr &= ~(1 << 6);
	}

	if (!BIT(data, 5) && BIT(m_sci[Channel].ssr, 5) && BIT(m_sci[Channel].ssr_read, 5))
	{
		// Clear Overrun Error Flag
		m_sci[Channel].ssr_read &= ~(1 << 5);
		m_sci[Channel].ssr &= ~(1 << 5);
	}

	if (!BIT(data, 4) && BIT(m_sci[Channel].ssr, 4) && BIT(m_sci[Channel].ssr_read, 4))
	{
		// Clear Framing Error Flag
		m_sci[Channel].ssr_read &= ~(1 << 4);
		m_sci[Channel].ssr &= ~(1 << 4);
	}

	if (!BIT(data, 3) && BIT(m_sci[Channel].ssr, 3) && BIT(m_sci[Channel].ssr_read, 3))
	{
		// Clear Parity Error Flag
		m_sci[Channel].ssr_read &= ~(1 << 3);
		m_sci[Channel].ssr &= ~(1 << 3);
	}

	m_sci[Channel].ssr &= 0xfe;
	m_sci[Channel].ssr |= data & 1;
	recalc_irq();
}

void sh7021_device::sci_start_tx(unsigned channel)
{
	auto &sci = m_sci[channel];
	if (!BIT(sci.scr, 5))
		return;

	const uint64_t clock_divider = uint64_t(1) << ((sci.smr & 3) * 2);
	uint64_t character_clocks;
	if (BIT(sci.smr, 7))
	{
		character_clocks = 4 * clock_divider * (sci.brr + 1) * 8;
	}
	else
	{
		const unsigned data_bits = BIT(sci.smr, 6) ? 7 : 8;
		const unsigned parity_bits = BIT(sci.smr, 5) ? 1 : 0;
		const unsigned stop_bits = BIT(sci.smr, 3) ? 2 : 1;
		character_clocks = 32 * clock_divider * (sci.brr + 1) * (1 + data_bits + parity_bits + stop_bits);
	}

	sci.tx_busy = true;
	sci.ssr &= ~0x04;
	sci.et->adjust(attotime::from_ticks(character_clocks, clock()));
}

void sh7021_device::sci_receive_byte(unsigned channel, uint8_t data, bool framing_error, bool parity_error)
{
	if (channel >= 2 || !BIT(m_sci[channel].scr, 4))
		return;

	auto &sci = m_sci[channel];
	if (BIT(sci.ssr, 6))
		sci.ssr |= 0x20;
	else
	{
		sci.rdr = data;
		sci.ssr |= 0x40;
	}
	if (framing_error)
		sci.ssr |= 0x10;
	if (parity_error)
		sci.ssr |= 0x08;
	recalc_irq();
}

template uint8_t sh7021_device::sci_rdr_r<0>();
template uint8_t sh7021_device::sci_rdr_r<1>();

template <int Channel>
uint8_t sh7021_device::sci_rdr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_SCI_RD, "%s: Receive Data Register %d, sci_rdr_r: %02x\n", machine().describe_context(), Channel, m_sci[Channel].rdr);
	return m_sci[Channel].rdr;
}


// A/D Converter

uint16_t sh7021_device::adc_addr_r(offs_t offset)
{
	return m_adc.addr[offset & 3];
}

uint8_t sh7021_device::adc_adcsr_r()
{
	if (!machine().side_effects_disabled() && BIT(m_adc.adcsr, 7))
		m_adc.adf_read = true;
	return m_adc.adcsr;
}

void sh7021_device::adc_adcsr_w(uint8_t data)
{
	const uint8_t old = m_adc.adcsr;
	const bool clear_adf = BIT(old, 7) && m_adc.adf_read && !BIT(data, 7);
	m_adc.adcsr = (data & 0x7f) | (clear_adf ? 0 : (old & 0x80));
	if (clear_adf)
		m_adc.adf_read = false;

	if (!BIT(m_adc.adcsr, 5))
		m_adc.et->adjust(attotime::never);
	else if (!BIT(old, 5))
		adc_start();

	recalc_irq();
}

uint8_t sh7021_device::adc_adcr_r()
{
	return (m_adc.adcr & 0x80) | 0x7f;
}

void sh7021_device::adc_adcr_w(uint8_t data)
{
	m_adc.adcr = (data & 0x80) | 0x7f;
}

void sh7021_device::adc_start()
{
	// Scan groups begin at AN0 or AN4; single mode starts directly at CH2-0.
	m_adc.channel = BIT(m_adc.adcsr, 4) ? (BIT(m_adc.adcsr, 2) ? 4 : 0) : (m_adc.adcsr & 7);
	const unsigned states = BIT(m_adc.adcsr, 3) ? 134 : 266;
	m_adc.et->adjust(attotime::from_ticks(states, clock()));
}

TIMER_CALLBACK_MEMBER(sh7021_device::adc_conversion_complete)
{
	const unsigned channel = m_adc.channel & 7;
	m_adc.addr[channel & 3] = (m_an_in[channel]() & 0x03ff) << 6;

	if (BIT(m_adc.adcsr, 4))
	{
		const unsigned end_channel = (BIT(m_adc.adcsr, 2) ? 4 : 0) | (m_adc.adcsr & 3);
		if (channel != end_channel)
		{
			m_adc.channel++;
			const unsigned states = BIT(m_adc.adcsr, 3) ? 134 : 266;
			m_adc.et->adjust(attotime::from_ticks(states, clock()));
			return;
		}
	}

	m_adc.adcsr |= 0x80;
	if (BIT(m_adc.adcsr, 4) && BIT(m_adc.adcsr, 5))
		adc_start();
	else
		m_adc.adcsr &= ~0x20;
	recalc_irq();
}


// Pin Function Controller (PFC)

void sh7021_device::write_padr(uint16_t data)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port A Input write: write_padr: %04x\n", machine().describe_context(), data);
	m_pfc.padr_in = data;
}

void sh7021_device::write_pbdr(uint16_t data)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port B Input write: write_pbdr: %04x\n", machine().describe_context(), data);
	m_pfc.pbdr_in = data;
}

void sh7021_device::write_pcdr(uint8_t data)
{
	m_pfc.pcdr_in = data;
}

void sh7021_device::pfc_recalc_gpio_masks()
{
	m_pfc.pa_gpio_mask = 0;
	m_pfc.pb_gpio_mask = 0;
	for (unsigned i = 0; i < 16; i++)
	{
		if (!m_pfc.pafunc[i])
			m_pfc.pa_gpio_mask |= 1U << i;
		if (!m_pfc.pbfunc[i])
			m_pfc.pb_gpio_mask |= 1U << i;
	}
}

template void sh7021_device::write_padr_bit< 0>(int state);
template void sh7021_device::write_padr_bit< 1>(int state);
template void sh7021_device::write_padr_bit< 2>(int state);
template void sh7021_device::write_padr_bit< 3>(int state);
template void sh7021_device::write_padr_bit< 4>(int state);
template void sh7021_device::write_padr_bit< 5>(int state);
template void sh7021_device::write_padr_bit< 6>(int state);
template void sh7021_device::write_padr_bit< 7>(int state);
template void sh7021_device::write_padr_bit< 8>(int state);
template void sh7021_device::write_padr_bit< 9>(int state);
template void sh7021_device::write_padr_bit<10>(int state);
template void sh7021_device::write_padr_bit<11>(int state);
template void sh7021_device::write_padr_bit<12>(int state);
template void sh7021_device::write_padr_bit<13>(int state);
template void sh7021_device::write_padr_bit<14>(int state);
template void sh7021_device::write_padr_bit<15>(int state);

template <int Line>
void sh7021_device::write_padr_bit(int state)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port A Bit %d Input write: write_padr_bit: %d\n", machine().describe_context(), Line, state);
	m_pfc.padr_in = (m_pfc.padr_in & ~(1 << Line)) | (state << Line);
}

template void sh7021_device::write_pbdr_bit< 0>(int state);
template void sh7021_device::write_pbdr_bit< 1>(int state);
template void sh7021_device::write_pbdr_bit< 2>(int state);
template void sh7021_device::write_pbdr_bit< 3>(int state);
template void sh7021_device::write_pbdr_bit< 4>(int state);
template void sh7021_device::write_pbdr_bit< 5>(int state);
template void sh7021_device::write_pbdr_bit< 6>(int state);
template void sh7021_device::write_pbdr_bit< 7>(int state);
template void sh7021_device::write_pbdr_bit< 8>(int state);
template void sh7021_device::write_pbdr_bit< 9>(int state);
template void sh7021_device::write_pbdr_bit<10>(int state);
template void sh7021_device::write_pbdr_bit<11>(int state);
template void sh7021_device::write_pbdr_bit<12>(int state);
template void sh7021_device::write_pbdr_bit<13>(int state);
template void sh7021_device::write_pbdr_bit<14>(int state);
template void sh7021_device::write_pbdr_bit<15>(int state);

template <int Line>
void sh7021_device::write_pbdr_bit(int state)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port B Bit %d Input write: write_pbdr_bit: %d\n", machine().describe_context(), Line, state);
	m_pfc.pbdr_in = (m_pfc.pbdr_in & ~(1 << Line)) | (state << Line);
}

uint16_t sh7021_device::pfc_paior_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_PFC_RD, "%s: Port A I/O (Direction) Register, pfc_paior_r: %04x\n", machine().describe_context(), m_pfc.paior);
	return m_pfc.paior;
}

void sh7021_device::pfc_paior_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port A I/O (Direction) Register, pfc_paior_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_pfc.paior);
	const uint16_t output_mask = m_pfc.paior & m_pfc.pa_gpio_mask;
	m_pa_out(m_pfc.padr & output_mask, output_mask);
}

uint16_t sh7021_device::pfc_pacr1_r()
{
	static constexpr uint16_t PACR1_R_MASK = 0x0002;
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_PFC_RD, "%s: Port A Control Register 1, pfc_pacr1_r: %04x\n", machine().describe_context(), m_pfc.pacr1);
	return m_pfc.pacr1 | PACR1_R_MASK;
}

void sh7021_device::pfc_pacr1_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	static constexpr uint16_t PACR1_W_MASK = 0xfffd;
	LOGMASKED(LOG_PFC_WR, "%s: Port A Control Register 1, pfc_pacr1_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_pfc.pacr1);
	const uint16_t pacr1_masked = m_pfc.pacr1 & PACR1_W_MASK;
	for (int i = 0; i < 8; i++)
	{
		int bit = i << 1;
		m_pfc.pafunc[8 + i] = (pacr1_masked >> bit) & 3;
	}
	pfc_recalc_gpio_masks();
}

uint16_t sh7021_device::pfc_pacr2_r()
{
	static constexpr uint16_t PACR2_R_MASK = 0xaa00;
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_PFC_RD, "%s: Port A Control Register 2, pfc_pacr2_r: %04x\n", machine().describe_context(), m_pfc.pacr2);
	return m_pfc.pacr2 | PACR2_R_MASK;
}

void sh7021_device::pfc_pacr2_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	static constexpr uint16_t PACR2_W_MASK = 0x55ff;
	LOGMASKED(LOG_PFC_WR, "%s: Port A Control Register 2, pfc_pacr2_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_pfc.pacr2);
	const uint16_t pacr2_masked = m_pfc.pacr2 & PACR2_W_MASK;
	for (int i = 0; i < 8; i++)
	{
		int bit = i << 1;
		m_pfc.pafunc[i] = (pacr2_masked >> bit) & 3;
	}
	pfc_recalc_gpio_masks();
}

uint16_t sh7021_device::pfc_pbior_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_PFC_RD, "%s: Port B I/O (Direction) Register, pfc_pbior_r: %04x\n", machine().describe_context(), m_pfc.pbior);
	return m_pfc.pbior;
}

void sh7021_device::pfc_pbior_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port B I/O (Direction) Register, pfc_pbior_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_pfc.pbior);
	const uint16_t output_mask = m_pfc.pbior & m_pfc.pb_gpio_mask;
	m_pb_out(m_pfc.pbdr & output_mask, output_mask);
}

uint16_t sh7021_device::pfc_pbcr1_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_PFC_RD, "%s: Port B Control Register 1, pfc_pbcr1_r: %04x\n", machine().describe_context(), m_pfc.pbcr1);
	return m_pfc.pbcr1;
}

void sh7021_device::pfc_pbcr1_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port B Control Register 1, pfc_pbcr1_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_pfc.pbcr1);
	for (int i = 0; i < 8; i++)
	{
		int bit = i << 1;
		m_pfc.pbfunc[8 + i] = (m_pfc.pbcr1 >> bit) & 3;
	}
	pfc_recalc_gpio_masks();
}

uint16_t sh7021_device::pfc_pbcr2_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_PFC_RD, "%s: Port B Control Register 2, pfc_pbcr2_r: %04x\n", machine().describe_context(), m_pfc.pbcr2);
	return m_pfc.pbcr2;
}

void sh7021_device::pfc_pbcr2_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port B Control Register 2, pfc_pbcr2_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_pfc.pbcr2);
	for (int i = 0; i < 8; i++)
	{
		int bit = i << 1;
		m_pfc.pbfunc[i] = (m_pfc.pbcr2 >> bit) & 3;
	}
	pfc_recalc_gpio_masks();
}

uint16_t sh7021_device::pfc_padr_r()
{
	const uint16_t data = ((m_pfc.padr_in & ~m_pfc.paior) | (m_pfc.padr & m_pfc.paior)) & m_pfc.pa_gpio_mask;
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_PFC_RD, "%s: Port A Data Register, pfc_padr_r: %04x ((%04x & ~%04x) | (%04x & %04x)) & %04x\n", machine().describe_context(), data, m_pfc.padr_in, m_pfc.paior, m_pfc.padr, m_pfc.paior, m_pfc.pa_gpio_mask);
	return data;
}

void sh7021_device::pfc_padr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port A Data Register, pfc_padr_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	const uint16_t old = m_pfc.padr;
	COMBINE_DATA(&m_pfc.padr);
	const uint16_t changed = m_pfc.padr ^ old;

	uint16_t output_mask = m_pfc.paior & m_pfc.pa_gpio_mask;
	for (int i = 0; i < 16; i++)
	{
		if (BIT(output_mask, i) && BIT(changed, i))
		{
			m_pa_bit_out[i](BIT(m_pfc.padr, i));
		}
	}

	if (output_mask)
	{
		m_pa_out(m_pfc.padr & output_mask, output_mask);
	}
}

uint16_t sh7021_device::pfc_pbdr_r()
{
	const uint16_t data = ((m_pfc.pbdr_in & ~m_pfc.pbior) | (m_pfc.pbdr & m_pfc.pbior)) & m_pfc.pb_gpio_mask;
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_PFC_RD, "%s: Port B Data Register, pfc_pbdr_r: %04x\n", machine().describe_context(), data);
	return data;
}

void sh7021_device::pfc_pbdr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_PFC_WR, "%s: Port B Data Register, pfc_pbdr_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	const uint16_t old = m_pfc.pbdr;
	COMBINE_DATA(&m_pfc.pbdr);
	const uint16_t changed = m_pfc.pbdr ^ old;

	uint16_t output_mask = m_pfc.pbior & m_pfc.pb_gpio_mask;
	for (int i = 0; i < 16; i++)
	{
		if (m_pfc.pbfunc[i] != 0)
		{
			output_mask &= ~(1 << i);
		}
		else if (BIT(changed, i))
		{
			m_pb_bit_out[i](BIT(m_pfc.pbdr, i));
		}
	}

	if (output_mask)
	{
		m_pb_out(m_pfc.pbdr & output_mask, output_mask);
	}
}

uint16_t sh7021_device::pfc_pcdr_r()
{
	return BIT(m_adc.adcsr, 5) ? 0x00ff : m_pfc.pcdr_in;
}

uint8_t sh7021_device::sbycr_r()
{
	return (m_sbycr & 0xc0) | 0x1f;
}

void sh7021_device::sbycr_w(uint8_t data)
{
	// SBY and HIZ cannot be set while the watchdog is running.
	const uint8_t requested = data & 0xc0;
	if (BIT(m_wdt.tcsr, 5))
		m_sbycr &= requested;
	else
		m_sbycr = requested;
}

uint16_t sh7021_device::pfc_cascr_r()
{
	if (!machine().side_effects_disabled())
		LOGMASKED(LOG_PFC_RD, "%s: Column Address Strobe Pin Control Register, pfc_cascr_r: %04x\n", machine().describe_context(), m_pfc.cascr);
	return m_pfc.cascr;
}

void sh7021_device::pfc_cascr_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	LOGMASKED(LOG_PFC_WR, "%s: Column Address Strobe Pin Control Register, pfc_cascr_w = %04x & %04x\n", machine().describe_context(), data, mem_mask);
	COMBINE_DATA(&m_pfc.cascr);
}
