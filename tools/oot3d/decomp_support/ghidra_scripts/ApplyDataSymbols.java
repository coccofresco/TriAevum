import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.List;

public class ApplyDataSymbols extends GhidraScript {
    private static class DataSymbol {
        String address;
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

    private List<DataSymbol> readSymbols(File csv) throws Exception {
        List<DataSymbol> symbols = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(new FileReader(csv))) {
            String header = reader.readLine();
            if (header == null || !header.startsWith("address,old_name,new_name,")) {
                throw new IllegalArgumentException("Unexpected data symbol CSV header: " + header);
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
                DataSymbol symbol = new DataSymbol();
                symbol.address = fields.get(0);
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
        String csvArg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "symbols/data_symbols.csv";
        File csv = new File(csvArg);
        if (!csv.isFile()) {
            throw new IllegalArgumentException("data symbol CSV does not exist: " + csv.getAbsolutePath());
        }

        SymbolTable symbols = currentProgram.getSymbolTable();
        Listing listing = currentProgram.getListing();
        int applied = 0;
        for (DataSymbol symbol : readSymbols(csv)) {
            Address address = toAddr(Long.parseUnsignedLong(symbol.address, 16));
            symbols.createLabel(address, symbol.newName, SourceType.USER_DEFINED);
            listing.setComment(
                address,
                CodeUnit.PLATE_COMMENT,
                "Data symbol: " + symbol.oldName + " -> " + symbol.newName + "\n" +
                "Kind: " + symbol.kind + "\n" +
                "Confidence: " + symbol.confidence + "\n" +
                "Source: " + symbol.sourceFile + "\n" +
                "Notes: " + symbol.notes
            );
            applied++;
        }

        println("applied data symbols: " + applied);
    }
}
