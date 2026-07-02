; JV-1080 debug-ROM payload — MIDI-sysex peek/poke/dump for the XP chip.
; Assembled by sh1asm.py, spliced into ic20 free space at file 0x59d48 by build.py.
;
; HOOK (applied separately by build.py): the literal at file 0x28a84 inside
; midi_task's sysex completion path (value 0x0a000200, the "message complete"
; dispatcher) is replaced with the address of dbg_shim below.  Every completed
; F0..F7 sysex message therefore enters dbg_shim with @(8,r15) = pointer to the
; buffered message page (page[2]=0xF0, page[3]=mfr-id, page[4]=op, page[5..]=payload).
; dbg_shim handles our private F0 7D ... messages and tail-forwards everything
; else to the real dispatcher 0x0a000200 so normal MIDI is untouched.
;
; Private wire format:  F0 7D <op> <nibblized payload> F7
;   each data byte is sent as two bytes: (b>>4), (b&0x0f)   [keeps all <0x80]
;   op 0x00 PING               -> reply F0 7D 00 01 F7
;   op 0x01 SETFLAG  <flag:1>  -> store debug freeze byte at FLAG
;   op 0x10 POKE     <addr:4><width:1><data:4>  -> MOV.b/w/l data -> addr
;   op 0x20 PEEK     <addr:4><width:1>          -> reply F0 7D 20 <value:4> F7
;   op 0x21 PEEKDSP  <xpoff:2>                  -> latch read, reply F0 7D 21 <hi:2 lo:2> F7
; (multi-byte fields are big-endian; addr 0x0c00xxxx hits the XP on CS4.)

      .org 0x0a059d48

; ====================================================================
; dbg_shim — entered in place of 0x0a000200 at the two completion sites.
; On entry: @(8,r15)=message page ptr, r4=0, frame = sysex handler's.
; ====================================================================
dbg_shim:
      mov.l r8,@-r15
      mov.l r9,@-r15
      mov.l r10,@-r15
      mov.l r11,@-r15
      mov.l r12,@-r15
      sts.l pr,@-r15            ; pushed 0x18 bytes; page ptr now at @(0x20,r15)
      mov.l @(0x20,r15),r8      ; r8 = message page pointer
      mov.b @(0x3,r8),r0        ; page[3] = manufacturer id
      extu.b r0,r0
      mov #0x7d,r1
      cmp/eq r1,r0
      bt is_ours
      ; not ours -> restore regs and forward to the real dispatcher
      lds.l @r15+,pr
      mov.l @r15+,r12
      mov.l @r15+,r11
      mov.l @r15+,r10
      mov.l @r15+,r9
      mov.l @r15+,r8
      ldlit r3, =0x0a000200
      jmp @r3                   ; r4 still 0, stack/frame intact
      nop
is_ours:
      mov.b @(0x4,r8),r0        ; page[4] = opcode
      extu.b r0,r0
      mov #0x00,r1
      cmp/eq r1,r0
      bt do_ping
      mov #0x01,r1
      cmp/eq r1,r0
      bt do_setflag
      mov #0x10,r1
      cmp/eq r1,r0
      bt do_poke
      mov #0x20,r1
      cmp/eq r1,r0
      bt do_peek
      mov #0x21,r1
      cmp/eq r1,r0
      bt do_peekdsp
shim_ret:
      lds.l @r15+,pr
      mov.l @r15+,r12
      mov.l @r15+,r11
      mov.l @r15+,r10
      mov.l @r15+,r9
      mov.l @r15+,r8
      rts
      nop

do_ping:
      bsr send_ping
      nop
      bra shim_ret
      nop
do_setflag:
      bsr handle_setflag
      nop
      bra shim_ret
      nop
do_poke:
      bsr handle_poke
      nop
      bra shim_ret
      nop
do_peek:
      bsr handle_peek
      nop
      bra shim_ret
      nop
do_peekdsp:
      bsr handle_peekdsp
      nop
      bra shim_ret
      nop

      .pool

; ====================================================================
; rdn — read r4 nibbles from page+r9 (big-endian) into r0.  r9 += r4.
; leaf (no calls).  clobbers r0,r1,r2,r4; advances r9.  preserves r8.
; ====================================================================
rdn:
      mov #0,r0
rdn_loop:
      mov r8,r1
      add r9,r1
      mov.b @r1,r2
      extu.b r2,r2
      shll2 r0
      shll2 r0
      or r2,r0
      add #1,r9
      add #-1,r4
      cmp/pl r4
      bt rdn_loop
      rts
      nop

; ====================================================================
; en — append byte r0 to TX buffer at r10 as two nibbles. r10 += 2.
; leaf.  clobbers r0,r3.
; ====================================================================
en:
      extu.b r0,r0
      mov r0,r3
      shlr2 r3
      shlr2 r3
      mov.b r3,@r10
      add #1,r10
      and #0x0f,r0
      mov.b r0,@r10
      add #1,r10
      rts
      nop

; ====================================================================
; emit4 — append 32-bit r1 to TX buffer (MSB first) as 4 nibblized bytes.
; calls en (saves pr).  clobbers r0,r2,r3; preserves r1,r10-advanced.
; ====================================================================
emit4:
      sts.l pr,@-r15
      mov r1,r2
      shlr16 r2
      shlr8 r2
      mov r2,r0
      bsr en
      nop
      mov r1,r2
      shlr16 r2
      extu.b r2,r0
      bsr en
      nop
      mov r1,r2
      shlr8 r2
      extu.b r2,r0
      bsr en
      nop
      mov r1,r0
      extu.b r0,r0
      bsr en
      nop
      lds.l @r15+,pr
      rts
      nop

; ====================================================================
; tx_send — transmit [TXBUF, r10) over MIDI out via midi_transmit_bytes.
; calls 0x0000bc04(r4=ptr, r5=count).  saves pr.
; ====================================================================
tx_send:
      sts.l pr,@-r15
      ldlit r4, =0x0901ff10     ; TXBUF
      mov r10,r5
      sub r4,r5                 ; count = r10 - TXBUF
      ldlit r3, =0x0000bc04     ; midi_transmit_bytes
      jsr @r3
      nop
      lds.l @r15+,pr
      rts
      nop

; tx_hdr — start a reply frame: TXBUF[0]=F0,[1]=7D,[2]=op(r0). r10 -> TXBUF+3.
; leaf.  clobbers r0,r3,r10.  caller passes op byte in r0.
tx_hdr:
      mov r0,r11                ; stash op
      ldlit r10, =0x0901ff10
      mov #0xf0,r3
      mov.b r3,@r10
      add #1,r10
      mov #0x7d,r3
      mov.b r3,@r10
      add #1,r10
      mov r11,r3
      mov.b r3,@r10
      add #1,r10
      rts
      nop

; tx_eot — append F7 then transmit. calls tx_send (saves pr).
tx_eot:
      sts.l pr,@-r15
      mov #0xf7,r0       ; -9 == 0xf7
      mov.b r0,@r10
      add #1,r10
      bsr tx_send
      nop
      lds.l @r15+,pr
      rts
      nop

      .pool

; ====================================================================
; Handlers
; ====================================================================

; PING -> F0 7D 00 01 F7
send_ping:
      sts.l pr,@-r15
      mov #0x00,r0
      bsr tx_hdr
      nop
      mov #0x01,r0
      bsr en
      nop
      bsr tx_eot
      nop
      lds.l @r15+,pr
      rts
      nop

; SETFLAG <flag:1 byte = 2 nibbles> -> store to FLAG (0x0901ff00)
handle_setflag:
      sts.l pr,@-r15
      mov #5,r9
      mov #2,r4
      bsr rdn                   ; r0 = flag byte
      nop
      ldlit r1, =0x0901ff00     ; FLAG
      mov.b r0,@r1
      lds.l @r15+,pr
      rts
      nop

; POKE <addr:4><width:1><data:4>
handle_poke:
      sts.l pr,@-r15
      mov #5,r9
      mov #8,r4
      bsr rdn                   ; r0 = addr
      nop
      mov r0,r11                ; r11 = addr
      mov #2,r4
      bsr rdn                   ; r0 = width
      nop
      mov r0,r12                ; r12 = width
      mov #8,r4
      bsr rdn                   ; r0 = data
      nop
      mov #1,r1
      cmp/eq r1,r12
      bt poke_b
      mov #2,r1
      cmp/eq r1,r12
      bt poke_w
      mov.l r0,@r11
      bra poke_done
      nop
poke_w:
      mov.w r0,@r11
      bra poke_done
      nop
poke_b:
      mov.b r0,@r11
poke_done:
      lds.l @r15+,pr
      rts
      nop

      .pool

; PEEK <addr:4><width:1> -> reply F0 7D 20 <value:4 bytes> F7
handle_peek:
      sts.l pr,@-r15
      mov #5,r9
      mov #8,r4
      bsr rdn                   ; r0 = addr
      nop
      mov r0,r11                ; r11 = addr
      mov #2,r4
      bsr rdn                   ; r0 = width
      nop
      mov #1,r1
      cmp/eq r1,r0
      bt peek_b
      mov #2,r1
      cmp/eq r1,r0
      bt peek_w
      mov.l @r11,r12            ; width 4
      bra peek_emit
      nop
peek_w:
      mov.w @r11,r0
      extu.w r0,r12
      bra peek_emit
      nop
peek_b:
      mov.b @r11,r0
      extu.b r0,r12
peek_emit:
      mov #0x20,r0
      bsr tx_hdr
      nop
      mov r12,r1
      bsr emit4
      nop
      bsr tx_eot
      nop
      lds.l @r15+,pr
      rts
      nop

; PEEKDSP <xpoff:2 bytes = 16-bit> -> trigger read, reply F0 7D 21 <hi:2><lo:2> F7
handle_peekdsp:
      sts.l pr,@-r15
      mov #5,r9
      mov #4,r4
      bsr rdn                   ; r0 = xp offset (16-bit)
      nop
      ldlit r1, =0x0c000000
      add r0,r1
      mov.w @r1,r2              ; trigger read (discarded).  Must be 16-bit: the XP
                               ; latch follows the last aligned sub-access, so a
                               ; 32-bit read of 16-bit CRAM (<0x3000) would latch the
                               ; adjacent slot.  A word read latches the correct slot
                               ; for both CRAM (2-byte) and IRAM/PRAM (4-byte) regions.
      ldlit r1, =0x0c003910
      mov.w @r1,r3             ; low readback word
      extu.w r3,r3
      ldlit r1, =0x0c003912
      mov.w @r1,r2             ; high readback word
      extu.w r2,r2
      shll16 r2
      or r3,r2                  ; r2 = (hi<<16)|lo
      mov #0x21,r0
      bsr tx_hdr
      nop
      mov r2,r1
      bsr emit4
      nop
      bsr tx_eot
      nop
      lds.l @r15+,pr
      rts
      nop

      .pool
