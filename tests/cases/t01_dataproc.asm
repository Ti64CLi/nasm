start
 AND r0,r1,r2
 ANDS r0,r1,r2
 EOR r3,r4,r5
 EORS r3,r4,r5
 SUB r6,r7,r8
 SUBS r6,r7,r8
 RSB r9,r10,r11
 RSBS r9,r10,r11
 ADD r12,sp,lr
 ADDS r12,r13,r14
 ADC r0,r1,#255
 ADCS r0,r1,#0xFF00
 SBC r2,r3,#0xFF000000
 SBCS r2,r3,#1020
 RSC r4,r5,#0b1010
 RSCS r4,r5,#017
 ORR r6,r7,r8,LSL #4
 ORRS r6,r7,r8,LSR #32
 BIC r9,r10,r11,ASR #32
 BICS r9,r10,r11,ROR #7
 MOV r0,r1
 MOVS r0,r1,RRX
 MVN r2,r3,ASL #2
 MVNS r2,#0
 CMP r4,r5,LSL r6
 CMN r7,r8,LSR r9
 TEQ r10,r11,ASR r12
 TST r0,r1,ROR r2
 ADDEQ r0,r1,r2
 SUBNES r3,r4,r5
 movlo r6,r7
 CMPHS r0,#1
 andhis r1,r2,r3
