import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.CodeUnitIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class ExportOot3dSelected extends GhidraScript {
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
                        throw new IllegalArgumentException("Expected 8 hex digits in selected export list: " + raw);
                    }
                    entries.add(value);
                }
            }
        }
        return entries;
    }

    private void addNeighborhood(Set<Function> selected, boolean includeCallers, boolean includeCallees) throws Exception {
        List<Function> seed = new ArrayList<>(selected);
        for (Function function : seed) {
            if (includeCallers) {
                selected.addAll(function.getCallingFunctions(monitor));
            }
            if (includeCallees) {
                selected.addAll(function.getCalledFunctions(monitor));
            }
        }
    }

    private int deleteOldDecompile(File decompDir, Function function, int fallbackIndex) {
        String entry = function.getEntryPoint().toString();
        File[] oldFiles = decompDir.listFiles((dir, name) -> name.contains("_" + entry + "_") && name.endsWith(".c"));
        if (oldFiles == null) {
            return fallbackIndex;
        }
        int selectedIndex = fallbackIndex;
        for (File oldFile : oldFiles) {
            String name = oldFile.getName();
            int underscore = name.indexOf('_');
            if (underscore > 0) {
                try {
                    selectedIndex = Integer.parseInt(name.substring(0, underscore));
                } catch (NumberFormatException ignored) {
                    selectedIndex = fallbackIndex;
                }
            }
            if (!oldFile.delete()) {
                println("warning: could not delete stale decompile " + oldFile.getAbsolutePath());
            }
        }
        return selectedIndex;
    }

    private void exportFunction(
        Function function,
        int index,
        File decompDir,
        BufferedWriter funcs,
        BufferedWriter asm,
        DecompInterface decompiler
    ) throws Exception {
        AddressSetView body = function.getBody();
        funcs.write(String.join(",",
            csv(function.getName()),
            csv(function.getEntryPoint().toString()),
            csv(body.getMinAddress().toString()),
            csv(body.getMaxAddress().toString()),
            csv(function.getCallingConventionName()),
            csv(function.getSignature().toString())));
        funcs.write("\n");

        asm.write("\n\n// " + function.getName() + " @ " + function.getEntryPoint() + "\n");
        CodeUnitIterator cuIt = currentProgram.getListing().getCodeUnits(body, true);
        while (cuIt.hasNext() && !monitor.isCancelled()) {
            CodeUnit cu = cuIt.next();
            asm.write(cu.getAddress() + ": " + cu.toString() + "\n");
        }

        DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
        if (result != null && result.decompileCompleted() && result.getDecompiledFunction() != null) {
            int outputIndex = deleteOldDecompile(decompDir, function, index);
            String safeName = function.getName().replaceAll("[^A-Za-z0-9_.-]", "_");
            File cFile = new File(decompDir, String.format("%05d_%s_%s.c", outputIndex, function.getEntryPoint(), safeName));
            try (BufferedWriter writer = new BufferedWriter(new FileWriter(cFile))) {
                writer.write(result.getDecompiledFunction().getC());
                writer.write("\n");
            }
        }
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outArg = args.length > 0 ? args[0] : "ghidra_export";
        String entriesArg = args.length > 1 ? args[1] : "analysis/selected_export_entries.txt";
        boolean includeCallers = args.length > 2 && Boolean.parseBoolean(args[2]);
        boolean includeCallees = args.length > 3 && Boolean.parseBoolean(args[3]);

        File outDir = new File(outArg);
        File decompDir = new File(outDir, "decompiled");
        File entriesFile = new File(entriesArg);
        outDir.mkdirs();
        decompDir.mkdirs();
        if (!entriesFile.isFile()) {
            throw new IllegalArgumentException("selected export entry list does not exist: " + entriesFile.getAbsolutePath());
        }

        FunctionManager functionManager = currentProgram.getFunctionManager();
        Set<Function> selected = new LinkedHashSet<>();
        for (String entry : readEntries(entriesFile)) {
            Address address = toAddr(Long.parseUnsignedLong(entry, 16));
            Function function = functionManager.getFunctionAt(address);
            if (function == null) {
                function = functionManager.getFunctionContaining(address);
                if (function != null) {
                    println("selected address " + address + " resolved to containing function " +
                        function.getName() + " @ " + function.getEntryPoint());
                }
            }
            if (function == null) {
                throw new IllegalArgumentException("No function at or containing selected address " + entry);
            }
            selected.add(function);
        }
        addNeighborhood(selected, includeCallers, includeCallees);

        List<Function> ordered = new ArrayList<>(selected);
        ordered.sort(Comparator.comparing(function -> function.getEntryPoint().toString()));

        DecompInterface decompiler = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        decompiler.setOptions(options);
        decompiler.openProgram(currentProgram);

        try (BufferedWriter funcs = new BufferedWriter(new FileWriter(new File(outDir, "functions_selected.csv")));
             BufferedWriter asm = new BufferedWriter(new FileWriter(new File(outDir, "disassembly_selected.txt")))) {
            funcs.write("name,entry,body_min,body_max,calling_convention,signature\n");
            int index = 99000;
            for (Function function : ordered) {
                if (monitor.isCancelled()) {
                    break;
                }
                exportFunction(function, index, decompDir, funcs, asm, decompiler);
                index++;
            }
            println("selectively exported functions: " + ordered.size());
        } finally {
            decompiler.dispose();
        }
    }
}
