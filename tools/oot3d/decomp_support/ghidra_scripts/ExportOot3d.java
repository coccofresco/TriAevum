import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.CodeUnitIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;

public class ExportOot3d extends GhidraScript {
    private String csv(String value) {
        if (value == null) {
            return "";
        }
        return "\"" + value.replace("\"", "\"\"") + "\"";
    }

    @Override
    protected void run() throws Exception {
        String outArg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "work/ghidra_export";
        File outDir = new File(outArg);
        File decompDir = new File(outDir, "decompiled");
        outDir.mkdirs();
        decompDir.mkdirs();

        DecompInterface decompiler = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        decompiler.setOptions(options);
        decompiler.openProgram(currentProgram);

        try (BufferedWriter funcs = new BufferedWriter(new FileWriter(new File(outDir, "functions.csv")));
             BufferedWriter asm = new BufferedWriter(new FileWriter(new File(outDir, "disassembly.txt")))) {
            funcs.write("name,entry,body_min,body_max,calling_convention,signature\n");

            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            int count = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function fn = it.next();
                AddressSetView body = fn.getBody();
                funcs.write(String.join(",",
                    csv(fn.getName()),
                    csv(fn.getEntryPoint().toString()),
                    csv(body.getMinAddress().toString()),
                    csv(body.getMaxAddress().toString()),
                    csv(fn.getCallingConventionName()),
                    csv(fn.getSignature().toString())));
                funcs.write("\n");

                asm.write("\n\n// " + fn.getName() + " @ " + fn.getEntryPoint() + "\n");
                CodeUnitIterator cuIt = currentProgram.getListing().getCodeUnits(body, true);
                while (cuIt.hasNext()) {
                    CodeUnit cu = cuIt.next();
                    asm.write(cu.getAddress() + ": " + cu.toString() + "\n");
                }

                DecompileResults result = decompiler.decompileFunction(fn, 60, monitor);
                if (result != null && result.decompileCompleted()) {
                    if (result.getDecompiledFunction() != null) {
                        String safeName = fn.getName().replaceAll("[^A-Za-z0-9_.-]", "_");
                        File cFile = new File(decompDir, String.format("%05d_%s_%s.c", count, fn.getEntryPoint(), safeName));
                        try (BufferedWriter writer = new BufferedWriter(new FileWriter(cFile))) {
                            writer.write(result.getDecompiledFunction().getC());
                            writer.write("\n");
                        }
                    }
                }
                count++;
            }
        } finally {
            decompiler.dispose();
        }
    }
}
