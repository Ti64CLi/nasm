start:
 .4byte 1,2,0xFFFFFFFF,-1,-2147483648
 .2byte 0,1,0xFFFF,-1,-32768
 .byte 1,2,255,-1,-128,65,122
 .ascii "hello, world"
 .byte 0
 .ascii "semi;colon"
 .byte 59,10
 .balign 4
 .4byte 0xCAFEBABE
 .byte 1
 .balign 8
 .4byte 3
 .byte 2
 .byte 0
 .2byte 0x1234
 .byte 7
 .4byte 0x11223344
 .2byte 0x5566
 .incbin "cases/blob.bin"
