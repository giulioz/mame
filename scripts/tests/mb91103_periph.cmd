// Run against ex5r with -debug -debugger none -debugscript <this file>.
focus maincpu
logerror "PERIPH reset_uart %d\n",b@1c==8&&b@1e==4&&b@20==8&&b@22==4
logerror "PERIPH reset_utimer %d\n",b@7b==1&&b@7f==1
logerror "PERIPH reset_udc %d\n",b@89==8&&b@91==8
do pc=700000
do ps=000f0000
fill 700000,800,w.9fa0
do w@3a=0200
logerror "PERIPH adc_started %d\n",(w@3a&c200)==8000
s 100
logerror "PERIPH adc_complete %d\n",w@38==0200&&(w@3a&c000)==4000
do pc=700000
do w@3a=0280
s 100
logerror "PERIPH adc_continuous %d\n",(w@3a&c000)==c000
do w@3a=0080
logerror "PERIPH adc_forced_stop %d\n",(w@3a&c000)==0000
do pc=700000
do w@3a=02c0
s 100
logerror "PERIPH adc_stop_mode %d\n",(w@3a&c000)==c000
do w@3a=00c0
do d@258=00700700
do w@26c=0002
do d@234=0160600c
do d@230=80002904
do w@3a=0201
s 300
logerror "PERIPH adc_dma_complete %d\n",(d@230&8f000000)==08000000&&w@26c==0&&w@700700==0200&&w@700702==0200
do pc=700000
do b@93=00
do w@8e=0003
do w@90=1058
logerror "PERIPH udc_transfer %d count=%04x control=%04x\n",w@8c==0003&&(w@90&ff7f)==1018,w@8c,w@90
logerror "PERIPH udc_stopped %d status=%02x\n",b@93==00,b@93
logerror "PERIPH udc_reload %d reload=%04x\n",w@8e==0003,w@8e
do b@93=a0
s 20
logerror "PERIPH udc_timer_underflow %d count=%04x status=%02x\n",(b@93&ac)==a4&&w@8c<=0003,w@8c,b@93
do b@93=a0
logerror "PERIPH udc_timer_clear %d\n",(b@93&0c)==00
do pc=700000
do w@28=0003
do w@2e=000b
logerror "PERIPH reload_started %d\n",(w@2e&7)==2
s 20
logerror "PERIPH reload_underflow %d\n",(w@2e&6)==6&&w@2a==ffff
do pc=700000
do b@77=10
do w@74=0000
do w@58=0004
do w@54=0011
do b@77=00
s 40
logerror "PERIPH freerun_counting %d\n",w@74>4
logerror "PERIPH output_compare %d\n",(b@55&40)==40
do b@55=11
logerror "PERIPH output_compare_clear %d\n",(b@55&40)==0
do b@77=10
do w@74=fffe
do b@77=20
s 20
logerror "PERIPH freerun_overflow %d\n",(b@77&40)==40
do b@77=00
do pc=700000
do w@78=0003
do b@7b=13
logerror "PERIPH utimer_started %d\n",(b@7b&1b)==13
s 10
logerror "PERIPH utimer_underflow %d\n",(b@7b&1b)==1b
do b@7b=13
logerror "PERIPH utimer_clear_irq %d\n",(b@7b&1b)==13
do pc=700000
do w@78=0000
do b@1e=15
do b@1c=01
do b@1d=55
logerror "PERIPH uart_transmit_busy %d\n",(b@1c&9)==1
s 200
logerror "PERIPH uart_transmit_empty %d\n",(b@1c&9)==9
do d@3f0=ffff0fff
logerror "PERIPH bitsearch_one %d\n",d@3fc==10
do d@3f4=0000ffff
logerror "PERIPH bitsearch_zero %d\n",d@3fc==10
do d@3f8=ffff0fff
logerror "PERIPH bitsearch_change %d\n",d@3fc==10
do b@402=3
logerror "PERIPH icr_mask %d\n",b@402==3
do b@430=1
logerror "PERIPH delayed_interrupt_set %d\n",b@430==1
do b@430=0
logerror "PERIPH delayed_interrupt_clear %d\n",b@430==0
do pc=701000
do tbr=700400
do ps=000f0010
do r15=702000
do w@701000=9fa0
do d@700700=703000
do w@703000=9730
do b@42f=1
do b@430=1
s 1
logerror "PERIPH delayed_interrupt_vector %d\n",pc==703000&&d@701ff8==701002&&(ps&1f0000)==010000
do b@430=0
s 1
logerror "PERIPH delayed_interrupt_reti %d\n",pc==701002&&r15==702000
quit
