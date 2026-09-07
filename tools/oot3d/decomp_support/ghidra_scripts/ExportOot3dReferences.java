import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class ExportOot3dReferences extends GhidraScript {
    private String csv(String value) {
        if (value == null) {
            return "";
        }
        return "\"" + value.replace("\"", "\"\"") + "\"";
    }

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
                        throw new IllegalArgumentException("Expected 8 hex digits in reference export list: " + raw);
                    }
                    entries.add(value);
                }
            }
        }
        return entries;
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outArg = args.length > 0 ? args[0] : "analysis/references.csv";
        String entriesArg = args.length > 1 ? args[1] : "analysis/selected_export_entries.txt";

        File outFile = new File(outArg);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
        File entriesFile = new File(entriesArg);
        if (!entriesFile.isFile()) {
            throw new IllegalArgumentException("reference export entry list does not exist: " + entriesFile.getAbsolutePath());
        }

        ReferenceManager referenceManager = currentProgram.getReferenceManager();
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("target,from_address,from_function,from_function_entry,reference_type,operand_index\n");
            for (String entry : readEntries(entriesFile)) {
                Address target = toAddr(Long.parseUnsignedLong(entry, 16));
                ReferenceIterator iterator = referenceManager.getReferencesTo(target);
                while (iterator.hasNext() && !monitor.isCancelled()) {
                    Reference reference = iterator.next();
                    Function function = getFunctionContaining(reference.getFromAddress());
                    writer.write(String.join(",",
                        csv(target.toString()),
                        csv(reference.getFromAddress().toString()),
                        csv(function == null ? "" : function.getName()),
                        csv(function == null ? "" : function.getEntryPoint().toString()),
                        csv(reference.getReferenceType().toString()),
                        csv(Integer.toString(reference.getOperandIndex()))));
                    writer.write("\n");
                }
            }
        }
        println("exported references: " + outFile.getAbsolutePath());
    }
}
