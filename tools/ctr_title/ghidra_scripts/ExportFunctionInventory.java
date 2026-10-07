import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class ExportFunctionInventory extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException(
                "ExportFunctionInventory requires profile and output path");
        }

        String profile = args[0];
        Path outputPath = Path.of(args[1]);
        Listing listing = currentProgram.getListing();
        List<Function> functions = new ArrayList<>();
        FunctionIterator iterator = currentProgram.getFunctionManager().getFunctions(true);
        while (iterator.hasNext()) {
            functions.add(iterator.next());
        }

        StringBuilder out = new StringBuilder();
        out.append("profile,entry,end,size,name,namespace,memory_block,is_thunk,thunk_target,")
            .append("parameter_count,calling_convention,return_type,caller_count,callers,")
            .append("callee_count,callees,global_ref_count,global_refs,string_count,strings\n");

        int exported = 0;
        for (Function function : functions) {
            if (monitor.isCancelled()) {
                break;
            }
            exportFunction(out, profile, function, listing);
            exported++;
            if (exported % 1000 == 0) {
                println("ctr function inventory progress=" + exported + "/" + functions.size());
            }
        }

        Path parent = outputPath.toAbsolutePath().getParent();
        if (parent != null) {
            Files.createDirectories(parent);
        }
        Files.writeString(outputPath, out.toString(), StandardCharsets.UTF_8);
        println("ctr exported function inventory profile=" + profile +
            " functions=" + exported + " output=" + outputPath);
    }

    private void exportFunction(StringBuilder out, String profile, Function function,
                                Listing listing) {
        Address entry = function.getEntryPoint();
        Address end = function.getBody().getMaxAddress();
        List<Function> callers = sortedFunctions(function.getCallingFunctions(monitor));
        List<Function> callees = sortedFunctions(function.getCalledFunctions(monitor));
        Set<Address> globalRefs = new LinkedHashSet<>();
        Set<String> strings = new LinkedHashSet<>();

        InstructionIterator instructions = listing.getInstructions(function.getBody(), true);
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            for (Reference reference : instruction.getReferencesFrom()) {
                if (!reference.getReferenceType().isData()) {
                    continue;
                }
                Address target = reference.getToAddress();
                if (!currentProgram.getMemory().contains(target) || function.getBody().contains(target)) {
                    continue;
                }
                globalRefs.add(target);
                Data data = listing.getDataContaining(target);
                if (data != null && data.getValue() instanceof String value && !value.isBlank()) {
                    strings.add(value);
                }
            }
        }

        Function thunk = function.isThunk() ? function.getThunkedFunction(true) : null;
        MemoryBlock block = currentProgram.getMemory().getBlock(entry);
        appendCsv(out, profile);
        appendCsv(out, hex(entry));
        appendCsv(out, hex(end));
        appendCsv(out, Long.toUnsignedString(function.getBody().getNumAddresses()));
        appendCsv(out, function.getName());
        appendCsv(out, function.getParentNamespace().getName(true));
        appendCsv(out, block != null ? block.getName() : "");
        appendCsv(out, Boolean.toString(function.isThunk()));
        appendCsv(out, thunk != null ? hex(thunk.getEntryPoint()) : "");
        appendCsv(out, Integer.toString(function.getParameterCount()));
        appendCsv(out, function.getCallingConventionName());
        appendCsv(out, function.getReturnType().getDisplayName());
        appendCsv(out, Integer.toString(callers.size()));
        appendCsv(out, joinFunctions(callers));
        appendCsv(out, Integer.toString(callees.size()));
        appendCsv(out, joinFunctions(callees));
        appendCsv(out, Integer.toString(globalRefs.size()));
        appendCsv(out, joinAddresses(globalRefs));
        appendCsv(out, Integer.toString(strings.size()));
        appendCsv(out, String.join(";", strings), true);
    }

    private static List<Function> sortedFunctions(Set<Function> source) {
        List<Function> functions = new ArrayList<>(source);
        functions.sort(Comparator.comparing(Function::getEntryPoint));
        return functions;
    }

    private static String joinFunctions(List<Function> functions) {
        List<String> entries = new ArrayList<>();
        for (Function function : functions) {
            entries.add(hex(function.getEntryPoint()));
        }
        return String.join(";", entries);
    }

    private static String joinAddresses(Set<Address> addresses) {
        List<Address> sorted = new ArrayList<>(addresses);
        sorted.sort(Address::compareTo);
        List<String> values = new ArrayList<>();
        for (Address address : sorted) {
            values.add(hex(address));
        }
        return String.join(";", values);
    }

    private static String hex(Address address) {
        return "0x" + address.toString().toUpperCase();
    }

    private static void appendCsv(StringBuilder out, String value) {
        appendCsv(out, value, false);
    }

    private static void appendCsv(StringBuilder out, String value, boolean last) {
        String safe = value == null ? "" : value.replace("\r", " ").replace("\n", " ");
        out.append('"').append(safe.replace("\"", "\"\"")).append('"');
        out.append(last ? '\n' : ',');
    }
}
