import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.LinkedHashSet;
import java.util.Set;

public class CreateOot3dFunctionsAtEntries extends GhidraScript {
    private Set<String> readEntries(File entriesFile) throws Exception {
        Set<String> entries = new LinkedHashSet<>();
        try (BufferedReader reader = new BufferedReader(new FileReader(entriesFile))) {
            String line;
            while ((line = reader.readLine()) != null) {
                int comment = line.indexOf('#');
                if (comment >= 0) {
                    line = line.substring(0, comment);
                }
                for (String raw : line.split("[,\\s]+")) {
                    String value = raw.replace("\uFEFF", "").trim().toLowerCase();
                    if (value.isEmpty()) {
                        continue;
                    }
                    if (value.startsWith("0x")) {
                        value = value.substring(2);
                    }
                    if (!value.matches("[0-9a-f]{8}")) {
                        throw new IllegalArgumentException("Expected 8 hex digits in entry list: " + raw);
                    }
                    entries.add(value);
                }
            }
        }
        return entries;
    }

    @Override
    protected void run() throws Exception {
        String entriesArg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "analysis/selected_export_entries.txt";
        File entriesFile = new File(entriesArg);
        if (!entriesFile.isFile()) {
            throw new IllegalArgumentException("entry list does not exist: " + entriesFile.getAbsolutePath());
        }

        FunctionManager functions = currentProgram.getFunctionManager();
        int existing = 0;
        int created = 0;
        for (String entryText : readEntries(entriesFile)) {
            Address entry = toAddr(Long.parseUnsignedLong(entryText, 16));
            Function function = functions.getFunctionContaining(entry);
            if (function != null) {
                existing++;
                continue;
            }

            if (currentProgram.getListing().getInstructionAt(entry) == null) {
                disassemble(entry);
            }
            Function createdFunction = createFunction(entry, "FUN_" + entryText);
            if (createdFunction == null) {
                throw new IllegalStateException("Could not create function at " + entryText);
            }
            createdFunction.setName("FUN_" + entryText, SourceType.ANALYSIS);
            created++;
        }

        println("OOT3D function entry promotion complete: existing=" + existing + ", created=" + created);
    }
}
