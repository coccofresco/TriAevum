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
import java.util.Map;

public class FindOot3dDescriptorPacketCopies extends GhidraScript {
    private static class MatchRow {
        String name;
        String entry;
        String helper;
        String callsite;
        int callIndex;
        String arg0;
        String arg1;
        String arg2;
        boolean mentionsCtx358;
        boolean mentionsCtx354;
        boolean mentionsDrawHandle10;
        boolean mentionsMode2600;
        boolean mentionsProviderStack;
        boolean mentionsPacketPrep;
        boolean mentionsRuntimePacketPack;
        String classification;
        String snippet;
    }

    private String csv(String value) {
        if (value == null) {
            return "";
        }
        return "\"" + value.replace("\"", "\"\"").replace("\r", " ").replace("\n", " ") + "\"";
    }

    private boolean hasAny(String text, String... needles) {
        for (String needle : needles) {
            if (text.contains(needle)) {
                return true;
            }
        }
        return false;
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
        return Math.min(text.length(), start + 240);
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
        int start = Math.max(0, index - 220);
        int end = Math.min(text.length(), index + 420);
        return text.substring(start, end);
    }

    private String callsiteKey(Function function, String helper) {
        return function.getEntryPoint().toString() + "|" + helper;
    }

    private Map<String, List<String>> collectCallsites(String helper) {
        Map<String, List<String>> result = new HashMap<>();
        Address target = toAddr(helper);
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
        while (references.hasNext() && !monitor.isCancelled()) {
            Reference reference = references.next();
            Function caller = currentProgram.getFunctionManager().getFunctionContaining(reference.getFromAddress());
            if (caller == null) {
                continue;
            }
            String key = callsiteKey(caller, helper);
            result.computeIfAbsent(key, unused -> new ArrayList<>()).add(reference.getFromAddress().toString());
        }
        for (List<String> callsites : result.values()) {
            callsites.sort(String::compareTo);
        }
        return result;
    }

    private String classify(MatchRow row) {
        if ("00348a64".equals(row.helper)) {
            if (row.mentionsMode2600 && row.mentionsProviderStack) {
                return "default_packet_ctxb_descriptor_binding";
            }
            return "ctxb_descriptor_binding_not_packet_value_writer";
        }
        if (row.mentionsCtx358 || row.mentionsDrawHandle10 || row.mentionsPacketPrep || row.mentionsRuntimePacketPack) {
            return "packet_copy_candidate_needs_owner_dataflow";
        }
        if (row.mentionsMode2600 || row.mentionsCtx354 || row.mentionsProviderStack) {
            return "default_packet_copy_or_related_init";
        }
        return "record_copy_owner_unclassified";
    }

    private List<MatchRow> scanFunction(Function function, String text, String helper, Map<String, List<String>> callsites) {
        List<MatchRow> rows = new ArrayList<>();
        if (function.getEntryPoint().toString().equals(helper)) {
            return rows;
        }
        String needle = "fun_" + helper + "(";
        int index = text.indexOf(needle);
        int callIndex = 0;
        List<String> functionCallsites = callsites.getOrDefault(callsiteKey(function, helper), new ArrayList<>());
        while (index >= 0) {
            int end = findStatementEnd(text, index);
            String call = text.substring(index, end);
            List<String> args = splitArgs(call);
            MatchRow row = new MatchRow();
            row.name = function.getName();
            row.entry = function.getEntryPoint().toString();
            row.helper = helper;
            row.callsite = callIndex < functionCallsites.size() ? functionCallsites.get(callIndex) : "";
            row.callIndex = callIndex;
            row.arg0 = args.size() > 0 ? args.get(0) : "";
            row.arg1 = args.size() > 1 ? args.get(1) : "";
            row.arg2 = args.size() > 2 ? args.get(2) : "";
            row.mentionsCtx358 = hasAny(text, "0x358", "+ 0x358", "+0x358");
            row.mentionsCtx354 = hasAny(text, "0x354", "+ 0x354", "+0x354");
            row.mentionsDrawHandle10 = hasAny(call, "+ 0x10", "+0x10") || hasAny(text, "0030f4d0", "0x0030f4d0");
            row.mentionsMode2600 = text.contains("0x2600");
            row.mentionsProviderStack = hasAny(text, "0x5d0", "+ 0x5d0", "+0x5d0");
            row.mentionsPacketPrep = hasAny(text, "003130a4", "0x003130a4", "fun_003130a4");
            row.mentionsRuntimePacketPack = hasAny(text, "003fa5d0", "0x003fa5d0", "fun_003fa5d0");
            row.snippet = snippet(text, index);
            row.classification = classify(row);
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
            : "analysis/descriptor_packet_copy_candidates.csv";
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

        Map<String, List<String>> callsitesB90 = collectCallsites("00348b90");
        Map<String, List<String>> callsitesA64 = collectCallsites("00348a64");

        List<MatchRow> rows = new ArrayList<>();
        try {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function function = it.next();
                DecompileResults result = decompiler.decompileFunction(function, 30, monitor);
                if (result == null || !result.decompileCompleted() || result.getDecompiledFunction() == null) {
                    continue;
                }
                String text = result.getDecompiledFunction().getC().toLowerCase();
                rows.addAll(scanFunction(function, text, "00348b90", callsitesB90));
                rows.addAll(scanFunction(function, text, "00348a64", callsitesA64));
            }
        } finally {
            decompiler.dispose();
        }

        rows.sort(Comparator
            .comparing((MatchRow row) -> row.helper)
            .thenComparing(row -> row.entry)
            .thenComparingInt(row -> row.callIndex));

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("name,entry,helper,callsite,call_index,arg0,arg1,arg2,classification,mentions_ctx_358,mentions_ctx_354,mentions_draw_handle_10,mentions_mode_2600,mentions_provider_stack,mentions_packet_prep,mentions_runtime_packet_pack,snippet\n");
            for (MatchRow row : rows) {
                writer.write(String.join(",",
                    csv(row.name),
                    csv(row.entry),
                    csv(row.helper),
                    csv(row.callsite),
                    Integer.toString(row.callIndex),
                    csv(row.arg0),
                    csv(row.arg1),
                    csv(row.arg2),
                    csv(row.classification),
                    Boolean.toString(row.mentionsCtx358),
                    Boolean.toString(row.mentionsCtx354),
                    Boolean.toString(row.mentionsDrawHandle10),
                    Boolean.toString(row.mentionsMode2600),
                    Boolean.toString(row.mentionsProviderStack),
                    Boolean.toString(row.mentionsPacketPrep),
                    Boolean.toString(row.mentionsRuntimePacketPack),
                    csv(row.snippet)));
                writer.write("\n");
            }
        }
        println("descriptor/packet copy candidates: " + rows.size() + " -> " + outFile.getAbsolutePath());
    }
}
