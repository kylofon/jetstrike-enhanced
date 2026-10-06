// Headless post-script for the 32-bit LE images: optionally creates functions at listed addresses,
// finds the Watcom stack-check routine (every prologue starts "push imm32; call __CHK", and __CHK
// pops its argument) and declares it __stdcall, gives every other function the default convention
// <cc> (__cdecl for the -3s stack-convention builds, __watcall for register builds) and writes every
// decompiled function to one C file.
// args: <starts.txt (hex addresses) or -> <out.c> [timeout_s] [cc]
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class DecompileAll32 extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        int timeout = args.length > 2 ? Integer.parseInt(args[2]) : 120;
        if (!args[0].equals("-")) {
            int created = 0;
            for (String s : Files.readAllLines(Paths.get(args[0]))) {
                s = s.trim();
                if (s.isEmpty()) continue;
                Address a = toAddr(Long.parseLong(s, 16));
                if (getInstructionAt(a) == null) new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                if (getFunctionContaining(a) == null && createFunction(a, null) != null) created++;
            }
            println("functions created: " + created);
        }
        String conv = args.length > 3 ? args[3] : "__cdecl";
        java.util.Map<Long, Integer> chk = new java.util.HashMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            byte[] b = getBytes(f.getEntryPoint(), 10);
            if ((b[0] & 0xff) == 0x68 && (b[5] & 0xff) == 0xe8) {
                long rel = (b[6] & 0xffL) | (b[7] & 0xffL) << 8 | (b[8] & 0xffL) << 16 | (long) b[9] << 24;
                chk.merge(f.getEntryPoint().getOffset() + 10 + rel, 1, Integer::sum);
            }
        }
        Function chkF = null;
        if (!chk.isEmpty()) {
            long best = java.util.Collections.max(chk.entrySet(), java.util.Map.Entry.comparingByValue()).getKey();
            chkF = getFunctionAt(toAddr(best));
            if (chkF != null) {
                chkF.setName("__CHK", ghidra.program.model.symbol.SourceType.USER_DEFINED);
                chkF.setCallingConvention("__stdcall");
                chkF.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true,
                        ghidra.program.model.symbol.SourceType.USER_DEFINED,
                        new ghidra.program.model.listing.ParameterImpl("size", ghidra.program.model.data.IntegerDataType.dataType, currentProgram));
                chkF.setReturnType(ghidra.program.model.data.VoidDataType.dataType, ghidra.program.model.symbol.SourceType.USER_DEFINED);
                println("__CHK at " + chkF.getEntryPoint() + " (" + chk.get(best) + " callers)");
            }
        }
        int n = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f == chkF) continue;
            String cc = f.getCallingConventionName();
            if (cc == null || cc.equals("unknown") || cc.equals("default")) { f.setCallingConvention(conv); n++; }
        }
        println(conv + " set on " + n + " functions");

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        int ok = 0, fail = 0;
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(Paths.get(args[1])))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                DecompileResults r = decomp.decompileFunction(f, timeout, monitor);
                out.println("// " + f.getName() + " @ " + f.getEntryPoint());
                if (r != null && r.decompileCompleted()) { out.println(r.getDecompiledFunction().getC()); ok++; }
                else { out.println("// decompile failed: " + (r == null ? "?" : r.getErrorMessage())); fail++; }
            }
        }
        println("decompiled " + ok + ", failed " + fail);
    }
}
