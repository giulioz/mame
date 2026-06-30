// license:BSD-3-Clause
// copyright-holders:Angelo Salese

#include "emu.h"
#include "sh7032.h"

DEFINE_DEVICE_TYPE(SH7032,  sh7032_device,  "sh7032",  "Hitachi SH-1 (SH7032)")
DEFINE_DEVICE_TYPE(SH7034,  sh7034_device,  "sh7034",  "Hitachi SH-1 (SH7034)")


sh7032_device::sh7032_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: sh7032_device(mconfig, SH7032, tag, owner, clock, address_map_constructor(FUNC(sh7032_device::sh7032_map), this))
{
}

sh7032_device::sh7032_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, address_map_constructor internal_map)
	: sh7021_device(mconfig, type, tag, owner, clock, internal_map)
{
	m_has_internal_rom = type == SH7034;
}

sh7034_device::sh7034_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: sh7032_device(mconfig, SH7034, tag, owner, clock, address_map_constructor(FUNC(sh7034_device::sh7034_map), this))
{
}

void sh7032_device::sh7032_map(address_map &map)
{
	sh703x_map(map);
	map(0x07000000, 0x07001fff).ram().mirror(0x08ffe000); // 8KB internal RAM shadows
}

void sh7034_device::sh7034_map(address_map &map)
{
	sh703x_map(map);
	map(0x00000000, 0x0000ffff).rom().region(DEVICE_SELF, 0).mirror(0x08ff0000); // 64KB internal ROM shadows
	map(0x07000000, 0x07000fff).ram().mirror(0x08fff000); // 4KB internal RAM shadows
}

void sh7032_device::sh703x_map(address_map &map)
{
	map(0x05fffee0, 0x05fffee7).r(FUNC(sh7032_device::adc_addr_r));
	map(0x05fffef8, 0x05fffef8).rw(FUNC(sh7032_device::adc_adcsr_r), FUNC(sh7032_device::adc_adcsr_w));
	map(0x05fffef9, 0x05fffef9).rw(FUNC(sh7032_device::adc_adcr_r), FUNC(sh7032_device::adc_adcr_w));

	map(0x05fffec0, 0x05fffec0).rw(FUNC(sh7032_device::sci_smr_r<0>), FUNC(sh7032_device::sci_smr_w<0>));
	map(0x05fffec1, 0x05fffec1).rw(FUNC(sh7032_device::sci_brr_r<0>), FUNC(sh7032_device::sci_brr_w<0>));
	map(0x05fffec2, 0x05fffec2).rw(FUNC(sh7032_device::sci_scr_r<0>), FUNC(sh7032_device::sci_scr_w<0>));
	map(0x05fffec3, 0x05fffec3).rw(FUNC(sh7032_device::sci_tdr_r<0>), FUNC(sh7032_device::sci_tdr_w<0>));
	map(0x05fffec4, 0x05fffec4).rw(FUNC(sh7032_device::sci_ssr_r<0>), FUNC(sh7032_device::sci_ssr_w<0>));
	map(0x05fffec5, 0x05fffec5).r(FUNC(sh7032_device::sci_rdr_r<0>));
	map(0x05fffec8, 0x05fffec8).rw(FUNC(sh7032_device::sci_smr_r<1>), FUNC(sh7032_device::sci_smr_w<1>));
	map(0x05fffec9, 0x05fffec9).rw(FUNC(sh7032_device::sci_brr_r<1>), FUNC(sh7032_device::sci_brr_w<1>));
	map(0x05fffeca, 0x05fffeca).rw(FUNC(sh7032_device::sci_scr_r<1>), FUNC(sh7032_device::sci_scr_w<1>));
	map(0x05fffecb, 0x05fffecb).rw(FUNC(sh7032_device::sci_tdr_r<1>), FUNC(sh7032_device::sci_tdr_w<1>));
	map(0x05fffecc, 0x05fffecc).rw(FUNC(sh7032_device::sci_ssr_r<1>), FUNC(sh7032_device::sci_ssr_w<1>));
	map(0x05fffecd, 0x05fffecd).r(FUNC(sh7032_device::sci_rdr_r<1>));

	map(0x05ffff00, 0x05ffff00).rw(FUNC(sh7032_device::itu_tstr_r), FUNC(sh7032_device::itu_tstr_w));
	map(0x05ffff01, 0x05ffff01).rw(FUNC(sh7032_device::itu_tsnc_r), FUNC(sh7032_device::itu_tsnc_w));
	map(0x05ffff02, 0x05ffff02).rw(FUNC(sh7032_device::itu_tmdr_r), FUNC(sh7032_device::itu_tmdr_w));
	map(0x05ffff03, 0x05ffff03).rw(FUNC(sh7032_device::itu_tfcr_r), FUNC(sh7032_device::itu_tfcr_w));
	map(0x05ffff31, 0x05ffff31).rw(FUNC(sh7032_device::itu_tocr_r), FUNC(sh7032_device::itu_tocr_w));

	map(0x05ffff04, 0x05ffff04).rw(FUNC(sh7032_device::itu_tcr_r<0>), FUNC(sh7032_device::itu_tcr_w<0>));
	map(0x05ffff05, 0x05ffff05).rw(FUNC(sh7032_device::itu_tior_r<0>), FUNC(sh7032_device::itu_tior_w<0>));
	map(0x05ffff06, 0x05ffff06).rw(FUNC(sh7032_device::itu_tier_r<0>), FUNC(sh7032_device::itu_tier_w<0>));
	map(0x05ffff07, 0x05ffff07).rw(FUNC(sh7032_device::itu_tsr_r<0>), FUNC(sh7032_device::itu_tsr_w<0>));
	map(0x05ffff08, 0x05ffff09).rw(FUNC(sh7032_device::itu_tcnt_r<0>), FUNC(sh7032_device::itu_tcnt_w<0>));
	map(0x05ffff0a, 0x05ffff0b).rw(FUNC(sh7032_device::itu_gra_r<0>), FUNC(sh7032_device::itu_gra_w<0>));
	map(0x05ffff0c, 0x05ffff0d).rw(FUNC(sh7032_device::itu_grb_r<0>), FUNC(sh7032_device::itu_grb_w<0>));

	map(0x05ffff0e, 0x05ffff0e).rw(FUNC(sh7032_device::itu_tcr_r<1>), FUNC(sh7032_device::itu_tcr_w<1>));
	map(0x05ffff0f, 0x05ffff0f).rw(FUNC(sh7032_device::itu_tior_r<1>), FUNC(sh7032_device::itu_tior_w<1>));
	map(0x05ffff10, 0x05ffff10).rw(FUNC(sh7032_device::itu_tier_r<1>), FUNC(sh7032_device::itu_tier_w<1>));
	map(0x05ffff11, 0x05ffff11).rw(FUNC(sh7032_device::itu_tsr_r<1>), FUNC(sh7032_device::itu_tsr_w<1>));
	map(0x05ffff12, 0x05ffff13).rw(FUNC(sh7032_device::itu_tcnt_r<1>), FUNC(sh7032_device::itu_tcnt_w<1>));
	map(0x05ffff14, 0x05ffff15).rw(FUNC(sh7032_device::itu_gra_r<1>), FUNC(sh7032_device::itu_gra_w<1>));
	map(0x05ffff16, 0x05ffff17).rw(FUNC(sh7032_device::itu_grb_r<1>), FUNC(sh7032_device::itu_grb_w<1>));

	map(0x05ffff18, 0x05ffff18).rw(FUNC(sh7032_device::itu_tcr_r<2>), FUNC(sh7032_device::itu_tcr_w<2>));
	map(0x05ffff19, 0x05ffff19).rw(FUNC(sh7032_device::itu_tior_r<2>), FUNC(sh7032_device::itu_tior_w<2>));
	map(0x05ffff1a, 0x05ffff1a).rw(FUNC(sh7032_device::itu_tier_r<2>), FUNC(sh7032_device::itu_tier_w<2>));
	map(0x05ffff1b, 0x05ffff1b).rw(FUNC(sh7032_device::itu_tsr_r<2>), FUNC(sh7032_device::itu_tsr_w<2>));
	map(0x05ffff1c, 0x05ffff1d).rw(FUNC(sh7032_device::itu_tcnt_r<2>), FUNC(sh7032_device::itu_tcnt_w<2>));
	map(0x05ffff1e, 0x05ffff1f).rw(FUNC(sh7032_device::itu_gra_r<2>), FUNC(sh7032_device::itu_gra_w<2>));
	map(0x05ffff20, 0x05ffff21).rw(FUNC(sh7032_device::itu_grb_r<2>), FUNC(sh7032_device::itu_grb_w<2>));

	map(0x05ffff22, 0x05ffff22).rw(FUNC(sh7032_device::itu_tcr_r<3>), FUNC(sh7032_device::itu_tcr_w<3>));
	map(0x05ffff23, 0x05ffff23).rw(FUNC(sh7032_device::itu_tior_r<3>), FUNC(sh7032_device::itu_tior_w<3>));
	map(0x05ffff24, 0x05ffff24).rw(FUNC(sh7032_device::itu_tier_r<3>), FUNC(sh7032_device::itu_tier_w<3>));
	map(0x05ffff25, 0x05ffff25).rw(FUNC(sh7032_device::itu_tsr_r<3>), FUNC(sh7032_device::itu_tsr_w<3>));
	map(0x05ffff26, 0x05ffff27).rw(FUNC(sh7032_device::itu_tcnt_r<3>), FUNC(sh7032_device::itu_tcnt_w<3>));
	map(0x05ffff28, 0x05ffff29).rw(FUNC(sh7032_device::itu_gra_r<3>), FUNC(sh7032_device::itu_gra_w<3>));
	map(0x05ffff2a, 0x05ffff2b).rw(FUNC(sh7032_device::itu_grb_r<3>), FUNC(sh7032_device::itu_grb_w<3>));
	map(0x05ffff2c, 0x05ffff2d).rw(FUNC(sh7032_device::itu_bra_r<3>), FUNC(sh7032_device::itu_bra_w<3>));
	map(0x05ffff2e, 0x05ffff2f).rw(FUNC(sh7032_device::itu_brb_r<3>), FUNC(sh7032_device::itu_brb_w<3>));

	map(0x05ffff32, 0x05ffff32).rw(FUNC(sh7032_device::itu_tcr_r<4>), FUNC(sh7032_device::itu_tcr_w<4>));
	map(0x05ffff33, 0x05ffff33).rw(FUNC(sh7032_device::itu_tior_r<4>), FUNC(sh7032_device::itu_tior_w<4>));
	map(0x05ffff34, 0x05ffff34).rw(FUNC(sh7032_device::itu_tier_r<4>), FUNC(sh7032_device::itu_tier_w<4>));
	map(0x05ffff35, 0x05ffff35).rw(FUNC(sh7032_device::itu_tsr_r<4>), FUNC(sh7032_device::itu_tsr_w<4>));
	map(0x05ffff36, 0x05ffff37).rw(FUNC(sh7032_device::itu_tcnt_r<4>), FUNC(sh7032_device::itu_tcnt_w<4>));
	map(0x05ffff38, 0x05ffff39).rw(FUNC(sh7032_device::itu_gra_r<4>), FUNC(sh7032_device::itu_gra_w<4>));
	map(0x05ffff3a, 0x05ffff3b).rw(FUNC(sh7032_device::itu_grb_r<4>), FUNC(sh7032_device::itu_grb_w<4>));
	map(0x05ffff3c, 0x05ffff3d).rw(FUNC(sh7032_device::itu_bra_r<4>), FUNC(sh7032_device::itu_bra_w<4>));
	map(0x05ffff3e, 0x05ffff3f).rw(FUNC(sh7032_device::itu_brb_r<4>), FUNC(sh7032_device::itu_brb_w<4>));

	map(0x05ffff40, 0x05ffff43).rw(FUNC(sh7032_device::dma_sar_r<0>), FUNC(sh7032_device::dma_sar_w<0>));
	map(0x05ffff44, 0x05ffff47).rw(FUNC(sh7032_device::dma_dar_r<0>), FUNC(sh7032_device::dma_dar_w<0>));
	map(0x05ffff48, 0x05ffff49).rw(FUNC(sh7032_device::dmaor_r), FUNC(sh7032_device::dmaor_w));
	map(0x05ffff4a, 0x05ffff4b).rw(FUNC(sh7032_device::dma_tcr_r<0>), FUNC(sh7032_device::dma_tcr_w<0>));
	map(0x05ffff4e, 0x05ffff4f).rw(FUNC(sh7032_device::dma_chcr_r<0>), FUNC(sh7032_device::dma_chcr_w<0>));

	map(0x05ffff50, 0x05ffff53).rw(FUNC(sh7032_device::dma_sar_r<1>), FUNC(sh7032_device::dma_sar_w<1>));
	map(0x05ffff54, 0x05ffff57).rw(FUNC(sh7032_device::dma_dar_r<1>), FUNC(sh7032_device::dma_dar_w<1>));
	map(0x05ffff5a, 0x05ffff5b).rw(FUNC(sh7032_device::dma_tcr_r<1>), FUNC(sh7032_device::dma_tcr_w<1>));
	map(0x05ffff5e, 0x05ffff5f).rw(FUNC(sh7032_device::dma_chcr_r<1>), FUNC(sh7032_device::dma_chcr_w<1>));

	map(0x05ffff60, 0x05ffff63).rw(FUNC(sh7032_device::dma_sar_r<2>), FUNC(sh7032_device::dma_sar_w<2>));
	map(0x05ffff64, 0x05ffff67).rw(FUNC(sh7032_device::dma_dar_r<2>), FUNC(sh7032_device::dma_dar_w<2>));
	map(0x05ffff6a, 0x05ffff6b).rw(FUNC(sh7032_device::dma_tcr_r<2>), FUNC(sh7032_device::dma_tcr_w<2>));
	map(0x05ffff6e, 0x05ffff6f).rw(FUNC(sh7032_device::dma_chcr_r<2>), FUNC(sh7032_device::dma_chcr_w<2>));

	map(0x05ffff70, 0x05ffff73).rw(FUNC(sh7032_device::dma_sar_r<3>), FUNC(sh7032_device::dma_sar_w<3>));
	map(0x05ffff74, 0x05ffff77).rw(FUNC(sh7032_device::dma_dar_r<3>), FUNC(sh7032_device::dma_dar_w<3>));
	map(0x05ffff7a, 0x05ffff7b).rw(FUNC(sh7032_device::dma_tcr_r<3>), FUNC(sh7032_device::dma_tcr_w<3>));
	map(0x05ffff7e, 0x05ffff7f).rw(FUNC(sh7032_device::dma_chcr_r<3>), FUNC(sh7032_device::dma_chcr_w<3>));

	map(0x05ffff84, 0x05ffff85).rw(FUNC(sh7032_device::intc_ipra_r), FUNC(sh7032_device::intc_ipra_w));
	map(0x05ffff86, 0x05ffff87).rw(FUNC(sh7032_device::intc_iprb_r), FUNC(sh7032_device::intc_iprb_w));
	map(0x05ffff88, 0x05ffff89).rw(FUNC(sh7032_device::intc_iprc_r), FUNC(sh7032_device::intc_iprc_w));
	map(0x05ffff8a, 0x05ffff8b).rw(FUNC(sh7032_device::intc_iprd_r), FUNC(sh7032_device::intc_iprd_w));
	map(0x05ffff8c, 0x05ffff8d).rw(FUNC(sh7032_device::intc_ipre_r), FUNC(sh7032_device::intc_ipre_w));
	map(0x05ffff8e, 0x05ffff8f).rw(FUNC(sh7032_device::intc_icr_r), FUNC(sh7032_device::intc_icr_w));

	map(0x05ffff90, 0x05ffff91).rw(FUNC(sh7032_device::ubc_barh_r), FUNC(sh7032_device::ubc_barh_w));
	map(0x05ffff92, 0x05ffff93).rw(FUNC(sh7032_device::ubc_barl_r), FUNC(sh7032_device::ubc_barl_w));
	map(0x05ffff94, 0x05ffff95).rw(FUNC(sh7032_device::ubc_bamrh_r), FUNC(sh7032_device::ubc_bamrh_w));
	map(0x05ffff96, 0x05ffff97).rw(FUNC(sh7032_device::ubc_bamrl_r), FUNC(sh7032_device::ubc_bamrl_w));
	map(0x05ffff98, 0x05ffff99).rw(FUNC(sh7032_device::ubc_bbr_r), FUNC(sh7032_device::ubc_bbr_w));

	map(0x05ffffa0, 0x05ffffa1).rw(FUNC(sh7032_device::bsc_bcr_r), FUNC(sh7032_device::bsc_bcr_w));
	map(0x05ffffa2, 0x05ffffa3).rw(FUNC(sh7032_device::bsc_wcr1_r), FUNC(sh7032_device::bsc_wcr1_w));
	map(0x05ffffa4, 0x05ffffa5).rw(FUNC(sh7032_device::bsc_wcr2_r), FUNC(sh7032_device::bsc_wcr2_w));
	map(0x05ffffa6, 0x05ffffa7).rw(FUNC(sh7032_device::bsc_wcr3_r), FUNC(sh7032_device::bsc_wcr3_w));
	map(0x05ffffa8, 0x05ffffa9).rw(FUNC(sh7032_device::bsc_dcr_r), FUNC(sh7032_device::bsc_dcr_w));
	map(0x05ffffaa, 0x05ffffab).rw(FUNC(sh7032_device::bsc_pcr_r), FUNC(sh7032_device::bsc_pcr_w));
	map(0x05ffffac, 0x05ffffad).rw(FUNC(sh7032_device::bsc_rcr_r), FUNC(sh7032_device::bsc_rcr_w));
	map(0x05ffffae, 0x05ffffaf).rw(FUNC(sh7032_device::bsc_rtcsr_r), FUNC(sh7032_device::bsc_rtcsr_w));
	map(0x05ffffb0, 0x05ffffb1).rw(FUNC(sh7032_device::bsc_rtcnt_r), FUNC(sh7032_device::bsc_rtcnt_w));
	map(0x05ffffb2, 0x05ffffb3).rw(FUNC(sh7032_device::bsc_rtcor_r), FUNC(sh7032_device::bsc_rtcor_w));

	map(0x05ffffb8, 0x05ffffb8).r(FUNC(sh7032_device::wdt_tcsr_r));
	map(0x05ffffb9, 0x05ffffb9).r(FUNC(sh7032_device::wdt_tcnt_r));
	map(0x05ffffb8, 0x05ffffb9).w(FUNC(sh7032_device::wdt_tcsr_tcnt_w));
	map(0x05ffffbb, 0x05ffffbb).r(FUNC(sh7032_device::wdt_rstcsr_r));
	map(0x05ffffba, 0x05ffffbb).w(FUNC(sh7032_device::wdt_rstcsr_w));
	map(0x05ffffbc, 0x05ffffbc).rw(FUNC(sh7032_device::sbycr_r), FUNC(sh7032_device::sbycr_w));

	map(0x05ffffc0, 0x05ffffc1).rw(FUNC(sh7032_device::pfc_padr_r), FUNC(sh7032_device::pfc_padr_w));
	map(0x05ffffc2, 0x05ffffc3).rw(FUNC(sh7032_device::pfc_pbdr_r), FUNC(sh7032_device::pfc_pbdr_w));
	map(0x05ffffc4, 0x05ffffc5).rw(FUNC(sh7032_device::pfc_paior_r), FUNC(sh7032_device::pfc_paior_w));
	map(0x05ffffc6, 0x05ffffc7).rw(FUNC(sh7032_device::pfc_pbior_r), FUNC(sh7032_device::pfc_pbior_w));
	map(0x05ffffc8, 0x05ffffc9).rw(FUNC(sh7032_device::pfc_pacr1_r), FUNC(sh7032_device::pfc_pacr1_w));
	map(0x05ffffca, 0x05ffffcb).rw(FUNC(sh7032_device::pfc_pacr2_r), FUNC(sh7032_device::pfc_pacr2_w));
	map(0x05ffffcc, 0x05ffffcd).rw(FUNC(sh7032_device::pfc_pbcr1_r), FUNC(sh7032_device::pfc_pbcr1_w));
	map(0x05ffffce, 0x05ffffcf).rw(FUNC(sh7032_device::pfc_pbcr2_r), FUNC(sh7032_device::pfc_pbcr2_w));
	map(0x05ffffd0, 0x05ffffd1).r(FUNC(sh7032_device::pfc_pcdr_r));
	map(0x05ffffee, 0x05ffffef).rw(FUNC(sh7032_device::pfc_cascr_r), FUNC(sh7032_device::pfc_cascr_w));

	map(0x05fffff0, 0x05fffff0).rw(FUNC(sh7032_device::tpc_tpmr_r), FUNC(sh7032_device::tpc_tpmr_w));
	map(0x05fffff1, 0x05fffff1).rw(FUNC(sh7032_device::tpc_tpcr_r), FUNC(sh7032_device::tpc_tpcr_w));
	map(0x05fffff2, 0x05fffff2).rw(FUNC(sh7032_device::tpc_ndera_r), FUNC(sh7032_device::tpc_ndera_w));
	map(0x05fffff3, 0x05fffff3).rw(FUNC(sh7032_device::tpc_nderb_r), FUNC(sh7032_device::tpc_nderb_w));
	map(0x05fffff4, 0x05fffff4).rw(FUNC(sh7032_device::tpc_ndrb_r), FUNC(sh7032_device::tpc_ndrb_w));
	map(0x05fffff5, 0x05fffff5).rw(FUNC(sh7032_device::tpc_ndra_r), FUNC(sh7032_device::tpc_ndra_w));
	map(0x05fffff6, 0x05fffff6).rw(FUNC(sh7032_device::tpc_ndrb_alt_r), FUNC(sh7032_device::tpc_ndrb_alt_w));
	map(0x05fffff7, 0x05fffff7).rw(FUNC(sh7032_device::tpc_ndra_alt_r), FUNC(sh7032_device::tpc_ndra_alt_w));

}
