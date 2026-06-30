// Run against d50 with -debug -debugger none -debugscript <this file>.
// The D-50 maps executable work RAM at C000-DFFF.
focus maincpu

// Debug-state visibility and writable execution RAM smoke test.
do pc=c000
do psw=0000
do b@c000=00
s 1
logerror "UPD78312 smoke %d\n",pc==c001&&psw==0000

// Arithmetic: overflow, auxiliary carry/borrow, carry/borrow, and SUB.
do pc=c000
do psw=0000
do a=7f
do b@c000=a8
do b@c001=01
s 1
logerror "UPD78312 add_flags %d\n",a==80&&(psw&00d7)==0094
do pc=c000
do psw=0001
do a=ff
do b@c000=a9
do b@c001=00
s 1
logerror "UPD78312 addc_flags %d\n",a==00&&(psw&00d7)==0051
do pc=c000
do psw=0000
do a=00
do b@c000=aa
do b@c001=01
s 1
logerror "UPD78312 sub_flags %d\n",a==ff&&(psw&00d7)==0093
do pc=c000
do psw=0001
do a=80
do b@c000=ab
do b@c001=00
s 1
logerror "UPD78312 subc_flags %d\n",a==7f&&(psw&00d7)==0016

// INC/DEC preserve CY while updating the other arithmetic flags.
do pc=c000
do psw=0001
do r0=7f
do b@c000=c0
s 1
logerror "UPD78312 inc_preserves_cy %d\n",r0==80&&(psw&00d7)==0095
do pc=c000
do psw=0001
do r0=00
do b@c000=c8
s 1
logerror "UPD78312 dec_preserves_cy %d\n",r0==ff&&(psw&00d7)==0093

// Rotate changes P/V, SUB and CY only; shifts additionally update S/Z and
// clear AC.  Word parity is defined from the low byte.
do pc=c000
do psw=00d2
do r0=81
do b@c000=30
do b@c001=48
s 1
logerror "UPD78312 ror_flags %d\n",r0==c0&&(psw&00d7)==00d5
do pc=c000
do psw=00d3
do r0=81
do b@c000=30
do b@c001=88
s 1
logerror "UPD78312 shr_flags %d\n",r0==40&&(psw&00d7)==0001
do pc=c000
do psw=00d2
do rp0=8001
do b@c000=30
do b@c001=c8
s 1
logerror "UPD78312 shrw_flags %d\n",rp0==4000&&(psw&00d7)==0005

// Decimal adjust examples from the manual, for addition and subtraction.
do pc=c000
do psw=0011
do a=01
do b@c000=04
s 1
logerror "UPD78312 adj4_add %d\n",a==67&&(psw&0017)==0001
do pc=c000
do psw=0013
do a=33
do b@c000=04
s 1
logerror "UPD78312 adj4_sub %d\n",a==cd&&(psw&0017)==0013

// Single-byte indirect transfer direction and post-adjust behavior.
do pc=c000
do psw=0000
do de=c100
do a=5a
do b@c100=a5
do b@c000=50
s 1
logerror "UPD78312 mov_de_store %d\n",b@c100==5a&&de==c101&&a==5a
do pc=c000
do de=c100
do a=00
do b@c100=a5
do b@c000=58
s 1
logerror "UPD78312 mov_de_load %d\n",a==a5&&de==c101

// CPU-control timing and debugger bank-state exposure.
do pc=c000
do psw=0000
do b@c000=05
do b@c001=aa
s 1
logerror "UPD78312 sel_state %d\n",rbs==2&&lastinstructioncycles==4
do psw=2000
logerror "UPD78312 rbs_state %d\n",rbs==2

// Only the first member of each interrupt group implements PR2-PR0;
// reserved priority bits in the other control registers always read as 1.
do pc=c000
do psw=0000
do b@c000=2b
do b@c001=d0
do b@c002=00
do b@c003=10
do b@c004=d0
s 2
logerror "UPD78312 tmic1_fixed_priority %d\n",a==07
do pc=c000
do b@c000=2b
do b@c001=c2
do b@c002=00
do b@c003=10
do b@c004=c2
s 2
logerror "UPD78312 cric01_fixed_priority %d\n",a==07
do pc=c000
do b@c000=2b
do b@c001=e2
do b@c002=ff
do b@c003=10
do b@c004=e2
s 2
logerror "UPD78312 tbic_reserved_bits %d\n",a==d7

// WDM/STBC ignore ordinary SFR writes; WDM accepts only the complemented
// protected form.  The time-base flag follows the selected TBC falling edge.
do pc=c000
do psw=0000
do b@c000=2b
do b@c001=42
do b@c002=96
do b@c003=10
do b@c004=42
s 2
logerror "UPD78312 wdm_rejects_plain_write %d\n",a==00
do pc=c000
do b@c000=09
do b@c001=42
do b@c002=69
do b@c003=96
do b@c004=10
do b@c005=42
s 2
logerror "UPD78312 wdm_protected_write %d\n",a==96
fill c000,200,b.00
do pc=c000
do b@c000=2b
do b@c001=e2
do b@c002=47
s 401
do pc=c300
do b@c300=10
do b@c301=e2
s 1
logerror "UPD78312 time_base_flag %d\n",(a&80)==80

// In interval mode a TM1 underflow asserts both TMF1 and TMF2.
do pc=c000
do psw=0000
do b@c000=2b
do b@c001=d0
do b@c002=47
do b@c003=2b
do b@c004=d2
do b@c005=47
do b@c006=0b
do b@c007=8e
do b@c008=00
do b@c009=00
do b@c00a=2b
do b@c00b=82
do b@c00c=80
do b@c00d=00
do b@c00e=00
s 6
s 1
do pc=c300
do b@c300=10
do b@c301=d0
s 1
logerror "UPD78312 timer1_tmf1 %d\n",(a&80)==80
do pc=c300
do b@c300=10
do b@c301=d2
s 1
logerror "UPD78312 timer1_tmf2 %d\n",(a&80)==80
do pc=c300
do b@c300=2b
do b@c301=82
do b@c302=00
do b@c303=2b
do b@c304=d0
do b@c305=47
do b@c306=2b
do b@c307=d2
do b@c308=47
s 3

// In one-shot mode writes to TM0 and MD0 independently start TS and MS;
// reaching zero stops each counter and raises TMF0 and TMF1 respectively.
fill c000,40,b.00
do pc=c000
do psw=0000
do b@c000=2b
do b@c001=ce
do b@c002=47
do b@c003=2b
do b@c004=d0
do b@c005=47
do b@c006=2b
do b@c007=80
do b@c008=01
do b@c009=2b
do b@c00a=88
do b@c00b=01
do b@c00c=2b
do b@c00d=89
do b@c00e=00
do b@c00f=2b
do b@c010=8a
do b@c011=01
do b@c012=2b
do b@c013=8b
do b@c014=00
s 10
s 5
do pc=c300
do b@c300=10
do b@c301=80
s 1
logerror "UPD78312 oneshot_stops %d\n",(a&e0)==00&&(a&01)==01
do pc=c300
do b@c300=10
do b@c301=ce
s 1
logerror "UPD78312 oneshot_tmf0 %d\n",(a&80)==80
do pc=c300
do b@c300=10
do b@c301=d0
s 1
logerror "UPD78312 oneshot_tmf1 %d\n",(a&80)==80
do pc=c300
do b@c300=2b
do b@c301=80
do b@c302=00
do b@c303=2b
do b@c304=ce
do b@c305=47
do b@c306=2b
do b@c307=d0
do b@c308=47
s 3

// ADC channel 0 defaults high when unbound.  ADIC selects macro service
// channel 7, which transfers ADCR to memory without stacking CPU context.
fill c000,100,b.00
do pc=c000
do psw=0000
do sp=c300
do b@c100=00
do b@c000=0c
do b@c001=ec
do b@c002=00
do b@c003=c1
do b@c004=3a
do b@c005=ee
do b@c006=02
do b@c007=3a
do b@c008=ef
do b@c009=6a
do b@c00a=2b
do b@c00b=e1
do b@c00c=97
do b@c00d=2b
do b@c00e=e0
do b@c00f=27
do b@c010=2b
do b@c011=68
do b@c012=90
s 46
do psw=0200
s 1
logerror "UPD78312 adc_macro_service %d\n",b@c100==ff&&b@feee==01&&w@feec==c100&&sp==c300
do psw=0000
do pc=c080
do b@c080=2b
do b@c081=68
do b@c082=00
do b@c083=2b
do b@c084=e0
do b@c085=40
s 2

// Equal programmable priorities fall back to the hardware order in table
// 5-2.  CRF00 must beat ADF, and automatic save is PC then PSW at SP..SP+3.
do pc=c000
do sp=c300
do psw=0000
do b@c000=2b
do b@c001=c0
do b@c002=80
do b@c003=2b
do b@c004=e0
do b@c005=80
do b@c006=2b
do b@c007=4e
do b@c008=02
do b@c009=4b
do b@c00a=00
s 4
logerror "UPD78312 interrupt_order %d\n",pc==w@801a&&sp==c2fc&&w@c2fc==c00a&&w@c2fe==0200

quit
