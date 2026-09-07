import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.List;

public class ApplyManualSymbols extends GhidraScript {
    private static class ManualSymbol {
        String entry;
        String oldName;
        String newName;
        String kind;
        String confidence;
        String sourceFile;
        String notes;
    }

    private List<String> parseCsvLine(String line) {
        List<String> fields = new ArrayList<>();
        StringBuilder current = new StringBuilder();
        boolean quoted = false;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (quoted) {
                if (c == '"') {
                    if (i + 1 < line.length() && line.charAt(i + 1) == '"') {
                        current.append('"');
                        i++;
                    } else {
                        quoted = false;
                    }
                } else {
                    current.append(c);
                }
            } else if (c == '"') {
                quoted = true;
            } else if (c == ',') {
                fields.add(current.toString());
                current.setLength(0);
            } else {
                current.append(c);
            }
        }
        fields.add(current.toString());
        return fields;
    }

    private List<ManualSymbol> readSymbols(File csv) throws Exception {
        List<ManualSymbol> symbols = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(new FileReader(csv))) {
            String header = reader.readLine();
            if (header == null || !header.startsWith("entry,old_name,new_name,")) {
                throw new IllegalArgumentException("Unexpected manual symbol CSV header: " + header);
            }
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.trim().isEmpty()) {
                    continue;
                }
                List<String> fields = parseCsvLine(line);
                if (fields.size() < 7) {
                    throw new IllegalArgumentException("Expected 7 CSV fields, got " + fields.size() + ": " + line);
                }
                ManualSymbol symbol = new ManualSymbol();
                symbol.entry = fields.get(0);
                symbol.oldName = fields.get(1);
                symbol.newName = fields.get(2);
                symbol.kind = fields.get(3);
                symbol.confidence = fields.get(4);
                symbol.sourceFile = fields.get(5);
                symbol.notes = fields.get(6);
                symbols.add(symbol);
            }
        }
        return symbols;
    }

    @Override
    protected void run() throws Exception {
        String csvArg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "symbols/manual_symbols.csv";
        File csv = new File(csvArg);
        if (!csv.isFile()) {
            throw new IllegalArgumentException("manual symbol CSV does not exist: " + csv.getAbsolutePath());
        }

        FunctionManager functions = currentProgram.getFunctionManager();
        int applied = 0;
        int missing = 0;
        for (ManualSymbol symbol : readSymbols(csv)) {
            Address entry = toAddr(Long.parseUnsignedLong(symbol.entry, 16));
            Function function = functions.getFunctionAt(entry);
            if (function == null) {
                println("missing function at " + symbol.entry + " for " + symbol.newName);
                missing++;
                continue;
            }

            String originalName = function.getName();
            function.setName(symbol.newName, SourceType.USER_DEFINED);
            function.setComment(
                "Manual symbol: " + symbol.oldName + " -> " + symbol.newName + "\n" +
                "Confidence: " + symbol.confidence + "\n" +
                "Source: " + symbol.sourceFile + "\n" +
                "Notes: " + symbol.notes + "\n" +
                "Previous Ghidra name at apply time: " + originalName
            );
            currentProgram.getListing().setComment(
                entry,
                CodeUnit.PLATE_COMMENT,
                "Manual symbol: " + symbol.newName + "\n" +
                "Confidence: " + symbol.confidence + "\n" +
                "Source: " + symbol.sourceFile
            );
            applied++;
        }

        println("applied manual symbols: " + applied);
        if (missing != 0) {
            throw new IllegalStateException("Missing functions while applying manual symbols: " + missing);
        }
    }
}
