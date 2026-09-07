import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

public class FindOot3dOffsetAccesses extends GhidraScript {
    private String csv(String value) {
        if (value == null) {
            return "";
        }
        return "\"" + value.replace("\"", "\"\"") + "\"";
    }

    private List<String> parseNeedles(String value) {
        List<String> needles = new ArrayList<>();
        for (String raw : value.split("[,\\s]+")) {
            String needle = raw.trim().toLowerCase();
            if (needle.isEmpty()) {
                continue;
            }
            if (!needle.startsWith("0x") && needle.matches("[0-9a-f]+")) {
                needle = "0x" + needle;
            }
            needles.add(needle);
        }
        return needles;
    }

    private boolean mnemonicLooksRelevant(String mnemonic) {
        String lower = mnemonic.toLowerCase();
        return lower.startsWith("ldr") || lower.startsWith("str") || lower.startsWith("add") ||
            lower.startsWith("sub") || lower.startsWith("vldr") || lower.startsWith("vstr");
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outArg = args.length > 0 ? args[0] : "analysis/offset_accesses.csv";
        String needlesArg = args.length > 1 ? args[1] : "0x1a8 0x1b5 0x1b8 0x1bc";
        List<String> needles = parseNeedles(needlesArg);

        File outFile = new File(outArg);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("function,entry,address,mnemonic,instruction,matched_offset\n");
            InstructionIterator iterator = currentProgram.getListing().getInstructions(true);
            while (iterator.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = iterator.next();
                String text = instruction.toString().toLowerCase();
                String mnemonic = instruction.getMnemonicString();
                if (!mnemonicLooksRelevant(mnemonic)) {
                    continue;
                }
                for (String needle : needles) {
                    String decimalNeedle = "";
                    try {
                        decimalNeedle = Long.toString(Long.parseUnsignedLong(needle.substring(2), 16));
                    } catch (RuntimeException ignored) {
                    }
                    boolean matched = text.contains("#" + needle) || text.contains(" " + needle) ||
                        (!decimalNeedle.isEmpty() && text.contains("#" + decimalNeedle));
                    if (!matched) {
                        continue;
                    }
                    Function function = getFunctionContaining(instruction.getAddress());
                    writer.write(String.join(",",
                        csv(function == null ? "" : function.getName()),
                        csv(function == null ? "" : function.getEntryPoint().toString()),
                        csv(instruction.getAddress().toString()),
                        csv(mnemonic),
                        csv(instruction.toString()),
                        csv(needle)));
                    writer.write("\n");
                }
            }
        }
        println("exported offset accesses: " + outFile.getAbsolutePath());
    }
}
