bits 16
_DATA2 equ 0x2000
[map all main.map]
section .text vstart=0
entry_stop:
    hlt
    %include "macros.asm"
    %include "maincode_int.asm"
sound_thing:
    nop
    ret
section .data vstart=0 follows=.text align=16
    incbin "data.bin"
    db "SEPARATOR", 0
nova_linha:
    db "GAMBIARRA FOREVER!", 0
%include "newvars_defs.asm"
