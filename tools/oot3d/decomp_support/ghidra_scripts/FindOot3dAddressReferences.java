import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class FindOot3dAddressReferences extends GhidraScript {
    private String csv(String value) {
        if (value == null) {
            return "";
        }
        return "\"" + value.replace("\"", "\"\"") + "\"";
    }

    private List<Address> parseAddresses(String value) {
        List<Address> addresses = new ArrayList<>();
        for (String raw : value.split("[,\\s]+")) {
            String text = raw.trim();
            if (!text.isEmpty()) {
                addresses.add(toAddr(text));
            }
        }
        return addresses;
    }

    private void writeReference(BufferedWriter writer, Address root,
                                Address target, Reference reference,
                                int depth) throws Exception {
        Address source = reference.getFromAddress();
        Function function = getFunctionContaining(source);
        Instruction instruction = getInstructionAt(source);
        writer.write(String.join(",",
            csv(root.toString()),
            csv(target.toString()),
            Integer.toString(depth),
            csv(source.toString()),
            csv(reference.getReferenceType().toString()),
            csv(function == null ? "" : function.getName()),
            csv(function == null ? "" : function.getEntryPoint().toString()),
            csv(instruction == null ? "" : instruction.toString())));
        writer.write("\n");
    }

    private void visit(BufferedWriter writer, ReferenceManager references,
                       Address root, Address target, int depth,
                       int maxDepth, Set<String> visited) throws Exception {
        String visitKey = target.toString() + ":" + depth;
        if (!visited.add(visitKey)) {
            return;
        }
        ReferenceIterator iterator = references.getReferencesTo(target);
        while (iterator.hasNext() && !monitor.isCancelled()) {
            Reference reference = iterator.next();
            writeReference(writer, root, target, reference, depth);
            Address source = reference.getFromAddress();
            if (depth < maxDepth && getFunctionContaining(source) == null) {
                visit(writer, references, root, source, depth + 1,
                      maxDepth, visited);
            }
        }
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outArg = args.length > 0
            ? args[0] : "analysis/address_references.csv";
        String addressesArg = args.length > 1 ? args[1] : "0x004eb7d4";
        int maxDepth = args.length > 2 ? Integer.parseInt(args[2]) : 2;

        File outFile = new File(outArg);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        ReferenceManager references = currentProgram.getReferenceManager();
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("root,target,depth,source,reference_type,function,entry,instruction\n");
            for (Address root : parseAddresses(addressesArg)) {
                visit(writer, references, root, root, 0, maxDepth,
                      new HashSet<>());
            }
        }
        println("exported address references: " + outFile.getAbsolutePath());
    }
}
