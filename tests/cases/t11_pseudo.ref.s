start:
 PUSH {r0-r3,lr}
 PUSH {r0,r2,r4-r6}
 POP {r0-r3,pc}
 POP {r4}
 PUSHEQ {r0,lr}
 POPNE {r0,pc}
 NOP
 NOPEQ
 push {r1}
 pop {r1}
