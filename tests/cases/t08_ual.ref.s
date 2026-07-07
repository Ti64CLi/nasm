.syntax unified
start:
 LDRBEQ r0,[r1]
 STRBNE r2,[r3]
 LDRSHHS r4,[r5]
 LDRSBLO r6,[r7]
 STRHGE r8,[r9]
 SWPBEQ r0,r1,[r2]
 LDMFDEQ sp!,{r0,pc}
 LDMIANE r0,{r1,r2}
 STMDBHI r1!,{r2,r3}
 SUBSEQ r0,r0,r1
 ADDSNE r2,r2,r3
 MOVSLO r4,r5
 MULSEQ r0,r1,r2
 UMULLSNE r0,r1,r2,r3
 LDRTEQ r0,[r1],#4
 LDRBTNE r2,[r3],#1
