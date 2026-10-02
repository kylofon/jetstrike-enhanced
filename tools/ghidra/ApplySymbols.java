// Headless post-script: applies merged port symbols (tools/merge_symbols.py) to a flat LE image.
// args: <work/<exe>_symbols_ghidra.txt>   lines: "func 0001a2b4 name" / "global 00081170 name"
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

import java.nio.file.Files;
import java.nio.file.Paths;

public class ApplySymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        int funcs = 0, globals = 0, failed = 0;
        for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
            String[] p = line.trim().split("\s+");
            if (p.length < 3) continue;
            try {
                Address a = toAddr(Long.parseLong(p[1], 16));
                if (p[0].equals("func")) {
                    Function f = getFunctionAt(a);
                    if (f == null) { disassemble(a); f = createFunction(a, p[2]); }
                    if (f != null) { f.setName(p[2], SourceType.USER_DEFINED); funcs++; } else failed++;
                } else {
                    Symbol s = getSymbolAt(a);
                    if (s != null && s.getSource() == SourceType.USER_DEFINED) s.setName(p[2], SourceType.USER_DEFINED);
                    else createLabel(a, p[2], true, SourceType.USER_DEFINED);
                    globals++;
                }
            } catch (Exception e) {
                println("skip " + line + ": " + e.getMessage());
                failed++;
            }
        }
        println("applied " + funcs + " function names, " + globals + " global names, " + failed + " failed");
    }
}
