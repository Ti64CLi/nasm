start:
 B start
 B fwd
 BEQ start
 BNE fwd
 BHS start
 BLO fwd
 BMI start
 BPL fwd
 BVS start
 BVC fwd
 BHI start
 BLS fwd
 BGE start
 BLT fwd
 BGT start
 BLE fwd
 BAL fwd
 BL start
 BLEQ fwd
 BLLT start
 B start+8
 B fwd-4
 BX lr
 BXEQ r0
fwd:
 BX r1
