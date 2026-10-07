// Exports the contiguous address ranges Ghidra disassembled in Thumb state
// (TMode=1) and the functions that start in Thumb state, as CSV.
// @category CTR
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.PrintWriter;
import java.math.BigInteger;

public class ExportThumbRanges extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: ExportThumbRanges.java output.csv");
        }
        Register tmode = currentProgram.getRegister("TMode");
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            out.println("kind,start,end,size,name");
            long start = -1;
            long end = -1;
            long count = 0;
            InstructionIterator it = currentProgram.getListing().getInstructions(true);
            while (it.hasNext()) {
                Instruction instruction = it.next();
                BigInteger value = instruction.getValue(tmode, false);
                if (value == null || value.signum() == 0) {
                    continue;
                }
                long at = instruction.getAddress().getOffset();
                long next = at + instruction.getLength();
                if (start >= 0 && at == end) {
                    end = next;
                } else {
                    if (start >= 0) {
                        out.printf("range,0x%08X,0x%08X,%d,%n", start, end - 1, end - start);
                    }
                    start = at;
                    end = next;
                }
                ++count;
            }
            if (start >= 0) {
                out.printf("range,0x%08X,0x%08X,%d,%n", start, end - 1, end - start);
            }
            for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
                Address entry = function.getEntryPoint();
                BigInteger value = currentProgram.getProgramContext().getValue(tmode, entry, false);
                if (value != null && value.signum() != 0) {
                    out.printf("function,0x%08X,0x%08X,%d,%s%n", entry.getOffset(),
                            function.getBody().getMaxAddress().getOffset(),
                            function.getBody().getNumAddresses(), function.getName());
                }
            }
            println("ctr thumb instructions=" + count);
        }
    }
}
