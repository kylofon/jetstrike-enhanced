// Headless pre-script for a flat image written by tools/lefile.py (raw import, x86:LE:32).
// Makes the image RWX, adds the Watcom register calling convention "__watcall"
// (args EAX, EDX, EBX, ECX, then stack; result EAX or EDX:EAX; callee saves the rest)
// (available, not applied), marks the entry point, and declares the Watcom stack-check routine
// __CHK (__stdcall, pops its size argument) before analysis: it is the most common target of the
// prologue "push imm32; call rel32", and without it Ghidra's stack tracking breaks in every function.
// args: <entry hex>
import ghidra.app.script.GhidraScript;
import ghidra.program.database.SpecExtension;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class SetupLE extends GhidraScript {
    static final String WATCALL =
        "<prototype name=\"__watcall\" extrapop=\"unknown\" stackshift=\"4\">" +
        "<input>" +
        "<pentry minsize=\"1\" maxsize=\"4\"><register name=\"EAX\"/></pentry>" +
        "<pentry minsize=\"1\" maxsize=\"4\"><register name=\"EDX\"/></pentry>" +
        "<pentry minsize=\"1\" maxsize=\"4\"><register name=\"EBX\"/></pentry>" +
        "<pentry minsize=\"1\" maxsize=\"4\"><register name=\"ECX\"/></pentry>" +
        "<pentry minsize=\"1\" maxsize=\"500\" align=\"4\"><addr offset=\"4\" space=\"stack\"/></pentry>" +
        "</input>" +
        "<output>" +
        "<pentry minsize=\"1\" maxsize=\"4\"><register name=\"EAX\"/></pentry>" +
        "<pentry minsize=\"5\" maxsize=\"8\"><addr space=\"join\" piece1=\"EDX\" piece2=\"EAX\"/></pentry>" +
        "</output>" +
        "<unaffected><register name=\"EBX\"/><register name=\"ECX\"/><register name=\"EDX\"/>" +
        "<register name=\"ESI\"/><register name=\"EDI\"/><register name=\"EBP\"/><register name=\"ESP\"/></unaffected>" +
        "</prototype>";

    @Override
    public void run() throws Exception {
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            b.setRead(true); b.setWrite(true); b.setExecute(true);
        }
        new SpecExtension(currentProgram).addReplaceCompilerSpecExtension(WATCALL, monitor);
        Address entry = toAddr(Long.parseLong(getScriptArgs()[0], 16));
        currentProgram.getSymbolTable().addExternalEntryPoint(entry);
        createLabel(entry, "_cstart_", true, SourceType.USER_DEFINED);
        disassemble(entry);

        MemoryBlock blk = currentProgram.getMemory().getBlocks()[0];
        byte[] img = new byte[(int) blk.getSize()];
        blk.getBytes(blk.getStart(), img);
        long base = blk.getStart().getOffset();
        java.util.Map<Long, Integer> hits = new java.util.HashMap<>();
        for (int i = 0; i + 10 < img.length; i++) {
            if ((img[i] & 0xff) != 0x68 || (img[i + 5] & 0xff) != 0xe8) continue;
            long rel = (img[i + 6] & 0xffL) | (img[i + 7] & 0xffL) << 8 | (img[i + 8] & 0xffL) << 16 | (long) img[i + 9] << 24;
            long t = base + i + 10 + rel;
            if (t >= base && t < base + img.length) hits.merge(t, 1, Integer::sum);
        }
        if (!hits.isEmpty()) {
            long best = java.util.Collections.max(hits.entrySet(), java.util.Map.Entry.comparingByValue()).getKey();
            Address a = toAddr(best);
            disassemble(a);
            ghidra.program.model.listing.Function f = createFunction(a, "__CHK");
            if (f != null) {
                f.setCallingConvention("__stdcall");
                f.replaceParameters(ghidra.program.model.listing.Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true,
                        SourceType.USER_DEFINED,
                        new ghidra.program.model.listing.ParameterImpl("size", ghidra.program.model.data.IntegerDataType.dataType, currentProgram));
                f.setReturnType(ghidra.program.model.data.VoidDataType.dataType, SourceType.USER_DEFINED);
                f.setStackPurgeSize(4);
                println("__CHK at " + a + " (" + hits.get(best) + " prologues)");
            }
        }
        println("LE setup done, entry " + entry);
    }
}
