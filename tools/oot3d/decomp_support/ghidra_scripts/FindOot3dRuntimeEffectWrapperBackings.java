import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public class FindOot3dRuntimeEffectWrapperBackings extends GhidraScript {
    private static final String WRAPPER_ENTRY = "0034897c";

    private static class Row {
        String functionName;
        String functionEntry;
        String callsite;
        int callIndex;
        String managerArg;
        String descriptorArg;
        String backingArg;
        String trailingArg;
        String classification;
        String snippet;
    }

    private String csv(String value) {
        if (value == null) {
            return "";
        }
        return "\"" + value.replace("\"", "\"\"").replace("\r", " ").replace("\n", " ") + "\"";
    }

    private int findStatementEnd(String text, int start) {
        int depth = 0;
        for (int i = start; i < text.length(); i++) {
            char ch = text.charAt(i);
            if (ch == '(') {
                depth++;
            } else if (ch == ')') {
                depth = Math.max(0, depth - 1);
            } else if (ch == ';' && depth == 0) {
                return i + 1;
            }
        }
        return Math.min(text.length(), start + 320);
    }

    private List<String> splitArgs(String call) {
        int open = call.indexOf('(');
        int close = call.lastIndexOf(')');
        List<String> args = new ArrayList<>();
        if (open < 0 || close <= open) {
            return args;
        }

        int depth = 0;
        int start = open + 1;
        for (int i = open + 1; i < close; i++) {
            char ch = call.charAt(i);
            if (ch == '(' || ch == '[') {
                depth++;
            } else if (ch == ')' || ch == ']') {
                depth = Math.max(0, depth - 1);
            } else if (ch == ',' && depth == 0) {
                args.add(call.substring(start, i).trim());
                start = i + 1;
            }
        }
        args.add(call.substring(start, close).trim());
        return args;
    }

    private String snippet(String text, int index) {
        int start = Math.max(0, index - 180);
        int end = Math.min(text.length(), index + 360);
        return text.substring(start, end);
    }

    private String callsiteKey(Function function) {
        return function.getEntryPoint().toString();
    }

    private Map<String, List<String>> collectCallsites() {
        Map<String, List<String>> result = new HashMap<>();
        Address target = toAddr(WRAPPER_ENTRY);
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
        while (references.hasNext() && !monitor.isCancelled()) {
            Reference reference = references.next();
            Function caller = currentProgram.getFunctionManager().getFunctionContaining(reference.getFromAddress());
            if (caller == null) {
                continue;
            }
            result.computeIfAbsent(callsiteKey(caller), unused -> new ArrayList<>())
                  .add(reference.getFromAddress().toString());
        }
        for (List<String> callsites : result.values()) {
            callsites.sort(String::compareTo);
        }
        return result;
    }

    private String classify(List<String> args) {
        if (args.size() < 3) {
            return "unparsed_wrapper_call";
        }
        String backing = args.get(2).toLowerCase(Locale.ROOT).replaceAll("\\s+", " ");
        if (backing.equals("0") || backing.equals("(undefined4 *)0x0")) {
            return "null_backing_wrapper_allocates_default_autoclass1";
        }
        if (backing.contains("+ 0x178") || backing.contains("+0x178")) {
            return "external_actor_effect_backing_field_0x178";
        }
        if (backing.contains("local") || backing.contains("param_")) {
            return "external_dynamic_backing_expression";
        }
        return "external_backing_unclassified";
    }

    private List<Row> scanFunction(Function function, String text, List<String> callsites) {
        List<Row> rows = new ArrayList<>();
        if (function.getEntryPoint().toString().equals(WRAPPER_ENTRY)) {
            return rows;
        }

        String needle = "fun_" + WRAPPER_ENTRY + "(";
        int index = text.indexOf(needle);
        int callIndex = 0;
        while (index >= 0) {
            int end = findStatementEnd(text, index);
            String call = text.substring(index, end);
            List<String> args = splitArgs(call);

            Row row = new Row();
            row.functionName = function.getName();
            row.functionEntry = function.getEntryPoint().toString();
            row.callsite = callIndex < callsites.size() ? callsites.get(callIndex) : "";
            row.callIndex = callIndex;
            row.managerArg = args.size() > 0 ? args.get(0) : "";
            row.descriptorArg = args.size() > 1 ? args.get(1) : "";
            row.backingArg = args.size() > 2 ? args.get(2) : "";
            row.trailingArg = args.size() > 3 ? args.get(3) : "";
            row.classification = classify(args);
            row.snippet = snippet(text, index);
            rows.add(row);

            callIndex++;
            index = text.indexOf(needle, index + needle.length());
        }
        return rows;
    }

    @Override
    protected void run() throws Exception {
        String outArg = getScriptArgs().length > 0
            ? getScriptArgs()[0]
            : "analysis/runtime_effect_wrapper_backings.csv";
        File outFile = new File(outArg);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        DecompInterface decompiler = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        decompiler.setOptions(options);
        decompiler.openProgram(currentProgram);

        Map<String, List<String>> callsites = collectCallsites();
        List<Row> rows = new ArrayList<>();
        try {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function function = it.next();
                DecompileResults result = decompiler.decompileFunction(function, 30, monitor);
                if (result == null || !result.decompileCompleted() ||
                    result.getDecompiledFunction() == null) {
                    continue;
                }
                String text = result.getDecompiledFunction().getC().toLowerCase(Locale.ROOT);
                rows.addAll(scanFunction(function, text, callsites.getOrDefault(callsiteKey(function), new ArrayList<>())));
            }
        } finally {
            decompiler.dispose();
        }

        rows.sort(Comparator
            .comparing((Row row) -> row.classification)
            .thenComparing(row -> row.functionEntry)
            .thenComparingInt(row -> row.callIndex));

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("function,entry,callsite,call_index,manager_arg,descriptor_arg,backing_arg,trailing_arg,classification,snippet\n");
            for (Row row : rows) {
                writer.write(String.join(",",
                    csv(row.functionName),
                    csv(row.functionEntry),
                    csv(row.callsite),
                    Integer.toString(row.callIndex),
                    csv(row.managerArg),
                    csv(row.descriptorArg),
                    csv(row.backingArg),
                    csv(row.trailingArg),
                    csv(row.classification),
                    csv(row.snippet)));
                writer.write("\n");
            }
        }
        println("runtime effect wrapper backing calls: " + rows.size() + " -> " + outFile.getAbsolutePath());
    }
}
