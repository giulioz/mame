// Run against ex5r with -debug -debugger none -debugscript <this file>.
focus maincpu

// Immediate loads and register/special-register transfers
do pc=700000
do ps=000f0000
do w@700000=caa0
s 1
logerror "FRTEST ldi8 %d\n",r0==aa
do pc=700000
do w@700000=9ba1
do w@700002=5678
s 1
logerror "FRTEST ldi20 %d\n",r1==a5678&&pc==700004
do pc=700000
do w@700000=9f82
do w@700002=89ab
do w@700004=cdef
s 1
logerror "FRTEST ldi32 %d\n",r2==89abcdef&&pc==700006
do pc=700000
do r1=13579bdf
do w@700000=8b10
s 1
logerror "FRTEST mov_rr %d\n",r0==13579bdf
do pc=700000
do r0=76543210
do w@700000=b300
do w@700002=b701
s 2
logerror "FRTEST mov_special %d\n",tbr==76543210&&r1==76543210

// Arithmetic and flags (NZVC occupy bits 3..0)
do pc=700000
do ps=000f0000
do r0=7fffffff
do w@700000=a410
s 1
logerror "FRTEST add_overflow %d\n",r0==80000000&&(ps&f)==a
do pc=700000
do ps=000f0001
do r0=ffffffff
do r1=0
do w@700000=a710
s 1
logerror "FRTEST addc_carry_zero %d\n",r0==0&&(ps&f)==5
do pc=700000
do ps=000f0000
do r0=80000000
do r1=1
do w@700000=ac10
s 1
logerror "FRTEST sub_overflow %d\n",r0==7fffffff&&(ps&f)==2
do pc=700000
do ps=000f0001
do r0=0
do r1=0
do w@700000=ad10
s 1
logerror "FRTEST subc_borrow %d\n",r0==ffffffff&&(ps&f)==9
do pc=700000
do ps=000f0000
do r0=5
do w@700000=a850
s 1
logerror "FRTEST cmp_zero %d\n",r0==5&&(ps&f)==4
do pc=700000
do ps=000f000f
do r0=10
do r1=3
do w@700000=ae10
s 1
logerror "FRTEST subn_flags_unchanged %d\n",r0==d&&(ps&f)==f

// Logical, bit, exchange and shift instructions
do pc=700000
do ps=000f0003
do r0=f0f00000
do r1=0ff00000
do w@700000=8210
s 1
logerror "FRTEST and_reg %d\n",r0==00f00000&&(ps&f)==3
do pc=700000
do ps=000f000f
do r0=700100
do r1=0f0f0f0f
do d@700100=f0f00000
do w@700000=9c10
s 1
logerror "FRTEST eor_mem %d\n",d@700100==ffff0f0f&&(ps&f)==b
do pc=700000
do r0=700100
do b@700100=ab
do w@700000=8050
s 1
logerror "FRTEST band_low %d\n",b@700100==a1
do pc=700000
do ps=000f000f
do r0=700100
do b@700100=80
do w@700000=8980
s 1
logerror "FRTEST btst_high %d\n",(ps&c)==8
do pc=700000
do r0=12345678
do r1=700100
do b@700100=ab
do w@700000=8a10
s 1
logerror "FRTEST xchb %d\n",r0==ab&&b@700100==78
do pc=700000
do ps=000f0000
do r0=80000001
do w@700000=b410
s 1
logerror "FRTEST lsl %d\n",r0==2&&(ps&f)==1
do pc=700000
do ps=000f0000
do r0=80000001
do w@700000=b010
s 1
logerror "FRTEST lsr %d\n",r0==40000000&&(ps&f)==1
do pc=700000
do ps=000f0000
do r0=80000001
do w@700000=b810
s 1
logerror "FRTEST asr %d\n",r0==c0000000&&(ps&f)==9
do pc=700000
do ps=000f000f
do r0=12345678
do w@700000=b400
s 1
logerror "FRTEST shift_zero %d\n",r0==12345678&&(ps&f)==2

// Multiply and extension instructions
do pc=700000
do ps=000f0000
do r0=ffffffff
do r1=2
do w@700000=af10
s 1
logerror "FRTEST mul_signed %d\n",mdh==ffffffff&&mdl==fffffffe&&(ps&f)==8
do pc=700000
do ps=000f0000
do r0=ffffffff
do r1=2
do w@700000=ab10
s 1
logerror "FRTEST mul_unsigned %d\n",mdh==1&&mdl==fffffffe&&(ps&f)==2
do pc=700000
do ps=000f0000
do r0=ffff
do r1=2
do w@700000=bf10
s 1
logerror "FRTEST mulh %d\n",mdl==fffffffe&&(ps&f)==8
do pc=700000
do r0=80
do w@700000=9780
do w@700002=9790
s 1
logerror "FRTEST extsb %d\n",r0==ffffff80
s 1
logerror "FRTEST extub %d\n",r0==80
do pc=700000
do r0=8000
do w@700000=97a0
do w@700002=97b0
s 1
logerror "FRTEST extsh %d\n",r0==ffff8000
s 1
logerror "FRTEST extuh %d\n",r0==8000

// Hardware divide step sequences (the DIV/DIVU assembler macros)
do pc=700000
do ps=000f0000
do mdh=0
do mdl=64
do r1=7
do w@700000=9751
fill 700002,40,w.9761
s 21
logerror "FRTEST divu_sequence %d\n",mdl==e&&mdh==2
do pc=700000
do ps=000f0000
do mdh=0
do mdl=ffffff9c
do r1=7
do w@700000=9741
fill 700002,40,w.9761
do w@700042=9771
do w@700044=9f60
do w@700046=9f70
s 24
logerror "FRTEST div_sequence %d\n",mdl==fffffff2&&mdh==fffffffe

// General, frame-relative and stack memory addressing
do pc=700000
do r1=700100
do d@700100=89abcdef
do w@700000=0410
s 1
logerror "FRTEST ld_indirect %d\n",r0==89abcdef
do pc=700000
do r1=700100
do w@700100=ff80
do w@700000=0510
s 1
logerror "FRTEST lduh %d\n",r0==ff80
do pc=700000
do r13=700080
do r1=80
do b@700100=fe
do w@700000=0210
s 1
logerror "FRTEST ldub_indexed %d\n",r0==fe
do pc=700000
do r14=700120
do d@700110=12345678
do w@700000=2fc0
s 1
logerror "FRTEST ld_fp_negative %d\n",r0==12345678
do pc=700000
do ps=000f0000
do r15=700100
do d@700108=cafebabe
do w@700000=0320
s 1
logerror "FRTEST ld_sp_disp %d\n",r0==cafebabe&&r15==700100
do pc=700000
do r1=700100
do r0=deadbeef
do w@700000=1410
s 1
logerror "FRTEST st_indirect %d\n",d@700100==deadbeef
do pc=700000
do r14=700120
do r0=beef
do w@700000=5f80
s 1
logerror "FRTEST sth_fp_negative %d\n",w@700110==beef
do pc=700000
do ps=000f0000
do r15=700100
do d@700100=11223344
do w@700000=0700
s 1
logerror "FRTEST pop_reg %d\n",r0==11223344&&r15==700104
do pc=700000
do r0=55667788
do r15=700104
do w@700000=1700
s 1
logerror "FRTEST push_reg %d\n",r15==700100&&d@700100==55667788

// Direct moves, multi-register operations and stack frames
do pc=700000
do w@000180=ff80
do w@700000=09c0
s 1
logerror "FRTEST dmovh_sign_extend %d\n",r13==ffffff80
do pc=700000
do r13=700100
do d@000300=10203040
do w@700000=0cc0
s 1
logerror "FRTEST dmov_mem_mem %d\n",d@700100==10203040&&r13==700104
do pc=700000
do ps=000f0000
do r15=700100
do d@700100=11111111
do d@700104=22222222
do w@700000=8c03
s 1
logerror "FRTEST ldm0 %d\n",r0==11111111&&r1==22222222&&r15==700108
do pc=700000
do r0=11111111
do r1=22222222
do r15=700108
do w@700000=8ec0
s 1
logerror "FRTEST stm0 %d\n",r15==700100&&d@700100==11111111&&d@700104==22222222
do pc=700000
do r14=12345678
do r15=700120
do w@700000=0f04
s 1
logerror "FRTEST enter %d\n",r14==70011c&&r15==70010c&&d@70011c==12345678
do pc=700000
do w@700000=9f90
s 1
logerror "FRTEST leave %d\n",r14==12345678&&r15==700120

// Resource registers
do pc=700000
do r0=700100
do d@700100=0badcafe
do w@700000=bc30
s 1
logerror "FRTEST resource_load %d\n",r0==700104
do pc=700000
do r1=700104
do w@700000=bd31
s 1
logerror "FRTEST resource_store %d\n",r1==700108&&d@700104==0badcafe

// Normal and delayed control flow
do pc=700000
do w@700000=e001
s 1
logerror "FRTEST bra %d\n",pc==700004
do pc=700000
do w@700000=e101
s 1
logerror "FRTEST bno %d\n",pc==700002
do pc=700000
do w@700000=d001
s 1
logerror "FRTEST call_relative %d\n",pc==700004&&rp==700002
do pc=700000
do r0=700100
do w@700000=9700
s 1
logerror "FRTEST jmp_register %d\n",pc==700100
do pc=700000
do w@700000=f001
do w@700002=9fa0
s 1
logerror "FRTEST delay_slot_enter %d\n",pc==700002
s 1
logerror "FRTEST delay_slot_target %d\n",pc==700004
do pc=700000
do w@700000=d801
do w@700002=9fa0
s 2
logerror "FRTEST call_delay %d\n",pc==700004&&rp==700004

// ILM restrictions, trace, software exceptions, coprocessor trap and RETI
do pc=700000
do ps=001f0000
do r0=0005003f
do w@700000=0710
s 1
logerror "FRTEST mov_ps_ilm_restriction %d\n",ps==0015003f
do pc=700000
do ps=00110000
do w@700000=8703
s 1
logerror "FRTEST stilm_restriction %d\n",(ps&1f0000)==130000
do pc=701000
do tbr=700400
do ps=000f0100
do r15=702000
do w@701000=9fa0
do d@7007cc=703000
do w@703000=9fa0
do w@703002=9730
s 1
logerror "FRTEST trace_exception %d\n",pc==703000&&d@701ff8==701002
s 1
logerror "FRTEST trace_handler_suppressed %d\n",pc==703002&&r15==701ff8
s 1
logerror "FRTEST trace_reti_suppressed %d\n",pc==701002&&r15==702000
do pc=701000
do ps=000f0100
do r15=702000
do w@701000=1f0c
do w@703000=9fa0
do w@703002=9730
s 1
logerror "FRTEST int12_is_software_exception %d\n",pc==703000&&r15==701ff8
s 1
logerror "FRTEST int12_handler_can_trace %d\n",pc==703000&&r15==701ff0&&d@701ff0==703002
do w@703000=9730
s 1
logerror "FRTEST int12_trace_reti %d\n",pc==703002&&r15==701ff8
s 1
logerror "FRTEST int12_software_reti %d\n",pc==701002&&r15==702000
do pc=701000
do ps=000f0030
do r15=702000
do d@7007e8=703000
do w@701000=1f05
do w@703000=9730
s 1
logerror "FRTEST int_exception %d\n",pc==703000&&(ps&30)==0&&d@701ff8==701002&&d@701ffc==000f0030
s 1
logerror "FRTEST int_reti %d\n",pc==701002&&ps==000f0030&&r15==702000
do pc=701000
do ps=000f0010
do r15=702000
do d@7007d8=703000
do w@701000=9f30
do w@703000=9730
s 1
logerror "FRTEST inte_exception %d\n",pc==703000&&(ps&1f0010)==00040010
s 1
logerror "FRTEST inte_reti %d\n",pc==701002&&ps==000f0010
do pc=701000
do ps=000f0000
do r15=702000
do d@7007c4=703000
do w@701000=9fa0
do w@701002=be00
do w@703000=9730
s 2
logerror "FRTEST undefined_exception %d\n",pc==703000&&d@701ff8==701002
s 1
logerror "FRTEST undefined_reti %d\n",pc==701002&&r15==702000
do pc=701000
do ps=000f0000
do r15=702000
do d@7007e0=703000
do w@701000=9fc0
do w@701002=1234
do w@703000=9730
s 1
logerror "FRTEST coprocessor_exception %d\n",pc==703000&&d@701ff8==701004
s 1
logerror "FRTEST coprocessor_reti %d\n",pc==701004&&r15==702000
quit
