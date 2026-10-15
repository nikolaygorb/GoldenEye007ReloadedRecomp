.symfix
.sympath+ D:\dev\RECOMP\GoldenEye007ReloadedRecomp\out\build\win-amd64-relwithdebinfo
.reload /f
.logopen /t D:\dev\RECOMP\GoldenEye007ReloadedRecomp\out\build\win-amd64-relwithdebinfo\logs\windbg_crash.log
sxi -c2 ".echo === SECOND CHANCE AV ===; r; .echo --- insn ---; u . L4; .echo --- ctx.r3 (this/scratch) ---; dq rdi L1; .echo --- ctx.r4 ---; dq rdi+20 L1; .echo --- ctx.r11 (ea) ---; dq rdi+58 L1; .echo --- ctx.r31 ---; dq rdi+F8 L1; .echo --- ctx.lr ---; dq rdi+100 L1; .echo --- full ctx dump ---; dq rdi L28; .echo --- region rcx ---; !address @rcx; .echo --- region rdx ---; !address @rdx; .echo --- stack ---; kv; .echo --- analyze ---; !analyze -v; .logclose" av
sxe -c ".echo === ILLEGAL INSTRUCTION ===; r; u . L8; kv; !analyze -v; .logclose" 0xc000001d
sxe -c ".echo === STACK OVERFLOW ===; kv; !analyze -v; .logclose" 0xc00000fd
sxe -c ".echo === HEAP CORRUPTION ===; kv; !analyze -v; .logclose" 0xc0000374
sxi -c2 ".echo === CPP EXCEPTION (2nd chance) ===; .exr -1; kv; !analyze -v; .logclose" eh
sxe -c2 ".echo === FASTFAIL / STACK BUFFER OVERRUN ===; .exr -1; kv; !analyze -v; .logclose" 0xc0000409
sxe -c2 ".echo === UNKNOWN SOFTWARE EXCEPTION ===; .exr -1; kv; !analyze -v; .logclose" 0xe0434352
bu KERNELBASE!RaiseFailFastException ".echo === FASTFAIL DISPATCH (RaiseFailFastException) ===; kv; r; .echo --- exception record (rcx) ---; dt ntdll!_EXCEPTION_RECORD @rcx; !analyze -v; .logclose"
g
