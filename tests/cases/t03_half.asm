start
 LDRH r0,[r1]
 LDRH r0,[r1,#6]
 STRH r2,[r3,#-6]
 LDRSB r4,[r5,#0xFF]
 LDRSH r6,[r7,#-0xFF]!
 STRH r0,[r1],#2
 LDRH r0,[r1],#-2
 LDRSB r2,[r3],#1
 LDRSH r4,[r5],#1
 LDRH r0,[r1,r2]
 STRH r0,[r1,-r2]
 LDRSB r0,[r1],r2
 LDREQH r0,[r1]
 STRNEH r2,[r3]
 LDRGESB r4,[r5]
 LDRLTSH r6,[r7]
 LDRHS r8,[r9]
 LDRGT r10,[r11]
 LDRVSB r0,[r1]
 STRLSH r2,[r3]
 SWPHS r4,r5,[r6]
 LDRH r8,hdata
hdata
 DCW 0xBEEF,0x1234
