import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class ApplyFunctionSplits extends GhidraScript {
    private static class SplitSymbol {
        String entry;
        String oldName;
        String newName;
        String kind;
        String confidence;
        String sourceFile;
        String notes;
    }

    private static final List<String> TRUTHY = List.of("1", "true", "yes", "y", "approved", "promote");

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

    private Map<String, Integer> headerIndex(String header) {
        List<String> fields = parseCsvLine(header);
        Map<String, Integer> indexes = new HashMap<>();
        for (int i = 0; i < fields.size(); i++) {
            indexes.put(fields.get(i).trim(), i);
        }
        return indexes;
    }

    private String field(List<String> fields, Map<String, Integer> indexes, String name) {
        Integer index = indexes.get(name);
        if (index == null || index >= fields.size()) {
            return "";
        }
        return fields.get(index).trim();
    }

    private boolean approved(List<String> fields, Map<String, Integer> indexes) {
        String value = field(fields, indexes, "approved");
        return value.isEmpty() || TRUTHY.contains(value.toLowerCase());
    }

    private List<SplitSymbol> readSymbols(File csv) throws Exception {
        List<SplitSymbol> symbols = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(new FileReader(csv))) {
            String header = reader.readLine();
            if (header == null) {
                throw new IllegalArgumentException("Empty function split CSV: " + csv.getAbsolutePath());
            }
            Map<String, Integer> indexes = headerIndex(header);
            for (String required : List.of("entry", "new_name", "source_file", "confidence", "notes")) {
                if (!indexes.containsKey(required)) {
                    throw new IllegalArgumentException("Function split CSV missing required column: " + required);
                }
            }
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.trim().isEmpty()) {
                    continue;
                }
                List<String> fields = parseCsvLine(line);
                if (!approved(fields, indexes)) {
                    continue;
                }
                SplitSymbol symbol = new SplitSymbol();
                symbol.entry = field(fields, indexes, "entry").toLowerCase().replace("0x", "");
                symbol.oldName = field(fields, indexes, "old_name");
                symbol.newName = field(fields, indexes, "new_name");
                symbol.kind = field(fields, indexes, "kind");
                symbol.confidence = field(fields, indexes, "confidence");
                symbol.sourceFile = field(fields, indexes, "source_file");
                symbol.notes = field(fields, indexes, "notes");
                if (symbol.kind.isEmpty()) {
                    symbol.kind = "function";
                }
                if (!symbol.kind.equals("function")) {
                    continue;
                }
                if (!symbol.entry.matches("[0-9a-f]{8}")) {
                    throw new IllegalArgumentException("Expected 8 hex digits for entry: " + symbol.entry);
                }
                symbols.add(symbol);
            }
        }
        return symbols;
    }

    private Function getOrCreateFunction(FunctionManager functions, SplitSymbol symbol) throws Exception {
        Address entry = toAddr(Long.parseUnsignedLong(symbol.entry, 16));
        Function function = functions.getFunctionAt(entry);
        if (function != null) {
            return function;
        }

        Function containing = functions.getFunctionContaining(entry);
        if (containing != null) {
            println(
                "creating split inside containing function " +
                containing.getName() + " @ " + containing.getEntryPoint() +
                " for " + symbol.newName + " @ " + symbol.entry
            );
        }

        function = createFunction(entry, symbol.newName);
        if (function == null) {
            return functions.getFunctionAt(entry);
        }
        return function;
    }

    private void annotate(Function function, SplitSymbol symbol, String previousName) throws Exception {
        Address entry = function.getEntryPoint();
        function.setName(symbol.newName, SourceType.USER_DEFINED);
        function.setComment(
            "Function split: " + symbol.oldName + " -> " + symbol.newName + "\n" +
            "Confidence: " + symbol.confidence + "\n" +
            "Source: " + symbol.sourceFile + "\n" +
            "Notes: " + symbol.notes + "\n" +
            "Previous Ghidra name at apply time: " + previousName
        );
        currentProgram.getListing().setComment(
            entry,
            CodeUnit.PLATE_COMMENT,
            "Function split: " + symbol.newName + "\n" +
            "Confidence: " + symbol.confidence + "\n" +
            "Source: " + symbol.sourceFile
        );
    }

    @Override
    protected void run() throws Exception {
        String csvArg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "analysis/direct_target_split_symbol_candidates.csv";
        File csv = new File(csvArg);
        if (!csv.isFile()) {
            throw new IllegalArgumentException("function split CSV does not exist: " + csv.getAbsolutePath());
        }

        FunctionManager functions = currentProgram.getFunctionManager();
        int createdOrFound = 0;
        int failed = 0;
        for (SplitSymbol symbol : readSymbols(csv)) {
            Function before = functions.getFunctionAt(toAddr(Long.parseUnsignedLong(symbol.entry, 16)));
            Function function = getOrCreateFunction(functions, symbol);
            if (function == null) {
                println("failed to create function at " + symbol.entry + " for " + symbol.newName);
                failed++;
                continue;
            }
            String previousName = before == null ? function.getName() : before.getName();
            annotate(function, symbol, previousName);
            println((before == null ? "created" : "updated") + " split function " + symbol.entry + ": " + symbol.newName);
            createdOrFound++;
        }

        println("applied function splits: " + createdOrFound);
        if (failed != 0) {
            throw new IllegalStateException("Failed function splits: " + failed);
        }
    }
}
