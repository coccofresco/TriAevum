import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

public class FindOot3dLightSettingConsumers extends GhidraScript {
    private static class MatchRow {
        String name;
        String entry;
        boolean has3235;
        boolean has3236;
        boolean has3258;
        boolean has3237;
        boolean has31b0;
        boolean has3000;
        boolean hasRel235;
        boolean hasRel236;
        boolean hasRel258;
        int score;
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

    private int firstIndex(String text, String... needles) {
        int best = -1;
        for (String needle : needles) {
            int index = text.indexOf(needle);
            if (index >= 0 && (best < 0 || index < best)) {
                best = index;
            }
        }
        return best;
    }

    private String snippet(String text) {
        int index = firstIndex(text, "0x3235", "0x3236", "0x3258", "0x235", "0x236", "0x258");
        if (index < 0) {
            return "";
        }
        int start = Math.max(0, index - 180);
        int end = Math.min(text.length(), index + 360);
        return text.substring(start, end);
    }

    private MatchRow classify(Function function, String text) {
        MatchRow row = new MatchRow();
        row.name = function.getName();
        row.entry = function.getEntryPoint().toString();
        row.has3235 = text.contains("0x3235");
        row.has3236 = text.contains("0x3236");
        row.has3258 = text.contains("0x3258");
        row.has3237 = text.contains("0x3237");
        row.has31b0 = text.contains("0x31b0");
        row.has3000 = text.contains("0x3000");
        row.hasRel235 = hasAny(text, "0x235", "+ 0x235", "+0x235");
        row.hasRel236 = hasAny(text, "0x236", "+ 0x236", "+0x236");
        row.hasRel258 = hasAny(text, "0x258", "+ 0x258", "+0x258");

        row.score = 0;
        row.score += row.has3235 ? 4 : 0;
        row.score += row.has3236 ? 4 : 0;
        row.score += row.has3258 ? 4 : 0;
        row.score += row.has3237 ? 2 : 0;
        row.score += row.has31b0 ? 2 : 0;
        row.score += row.has3000 ? 1 : 0;
        row.score += row.hasRel235 ? 2 : 0;
        row.score += row.hasRel236 ? 2 : 0;
        row.score += row.hasRel258 ? 2 : 0;
        if ((row.has3235 || row.hasRel235) && (row.has3236 || row.hasRel236) && (row.has3258 || row.hasRel258)) {
            row.score += 8;
            row.classification = "current_previous_blend_candidate";
        } else if (row.has3258 || row.hasRel258) {
            row.classification = "blend_writer_or_local_runtime";
        } else if (row.has3237 || row.has31b0) {
            row.classification = "target_or_environment_state";
        } else {
            row.classification = "weak_offset_match";
        }
        row.snippet = snippet(text);
        return row;
    }

    @Override
    protected void run() throws Exception {
        String outArg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "analysis/light_setting_consumer_candidates.csv";
        int minScore = getScriptArgs().length > 1 ? Integer.parseInt(getScriptArgs()[1]) : 4;
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

        List<MatchRow> rows = new ArrayList<>();
        try {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function function = it.next();
                DecompileResults result = decompiler.decompileFunction(function, 30, monitor);
                if (result == null || !result.decompileCompleted() || result.getDecompiledFunction() == null) {
                    continue;
                }
                MatchRow row = classify(function, result.getDecompiledFunction().getC().toLowerCase());
                if (row.score >= minScore) {
                    rows.add(row);
                }
            }
        } finally {
            decompiler.dispose();
        }

        rows.sort(Comparator
            .comparingInt((MatchRow row) -> row.score).reversed()
            .thenComparing(row -> row.entry));

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("name,entry,score,classification,has_3235,has_3236,has_3258,has_3237,has_31b0,has_3000,has_rel_235,has_rel_236,has_rel_258,snippet\n");
            for (MatchRow row : rows) {
                writer.write(String.join(",",
                    csv(row.name),
                    csv(row.entry),
                    Integer.toString(row.score),
                    csv(row.classification),
                    Boolean.toString(row.has3235),
                    Boolean.toString(row.has3236),
                    Boolean.toString(row.has3258),
                    Boolean.toString(row.has3237),
                    Boolean.toString(row.has31b0),
                    Boolean.toString(row.has3000),
                    Boolean.toString(row.hasRel235),
                    Boolean.toString(row.hasRel236),
                    Boolean.toString(row.hasRel258),
                    csv(row.snippet)));
                writer.write("\n");
            }
        }
        println("light-setting consumer candidates: " + rows.size() + " -> " + outFile.getAbsolutePath());
    }
}
