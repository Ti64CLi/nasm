start:
 LDR r0,[r1]
 LDR r0,[r1,#4]
 LDR r0,[r1,#-4]
 LDR r0,[r1,#0xFFF]
 LDR r0,[r1,#-0xFF]!
 STR r2,[r3,#8]!
 LDR r0,[r1],#4
 STR r2,[r3],#-8
 LDR r0,[r1,r2]
 LDR r0,[r1,-r2]
 LDR r0,[r1,+r2]
 STR r0,[r1,r2,LSL #2]
 LDR r0,[r1,-r2,LSR #3]!
 LDR r0,[r1,r2,ASR #32]
 LDR r0,[r1,r2,ROR #1]
 LDR r0,[r1,r2,RRX]
 LDR r0,[r1],r2
 STR r0,[r1],-r2
 LDR r0,[r1],r2,LSL #4
 LDRB r4,[r5,#1]
 STRB r4,[r5],#1
 LDREQB r6,[r7]
 STREQB r6,[r7]
 LDRT r0,[r1],#4
 STRT r2,[r3],#4
 LDRBT r4,[r5],#2
 STRBT r6,[r7],#2
 LDR r8,data
 STR r8,data
data:
 .4byte 0
