import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class FindOot3dPicaRegisterWriters extends GhidraScript {
    private static final long GENERIC_SCALAR_WRITER_ADDRESS = 0x00307bd8L;
    private static final long GENERIC_VECTOR_WRITER_ADDRESS = 0x00307c94L;
    private static final int LOOKBACK_INSTRUCTION_COUNT = 64;

    private static final Pattern MOV_IMMEDIATE =
        Pattern.compile("^mov[a-z.]*\\s+(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern MOVW_IMMEDIATE =
        Pattern.compile("^movw[a-z.]*\\s+(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern MOVT_IMMEDIATE =
        Pattern.compile("^movt[a-z.]*\\s+(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern COPY_REGISTER =
        Pattern.compile("^(?:cpy|mov)[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d))$");
    private static final Pattern ADD_IMMEDIATE =
        Pattern.compile("^add[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern SUB_IMMEDIATE =
        Pattern.compile("^sub[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern LDR_LITERAL =
        Pattern.compile("^ldr[a-z.]*\\s+(r(?:1[0-2]|\\d)),\\[(?:0x)?([0-9a-f]+)\\]!?$");

    private static class KnownValue {
        boolean known;
        long value;
        String source;

        static KnownValue unknown() {
            KnownValue value = new KnownValue();
            value.known = false;
            value.source = "unknown";
            return value;
        }

        static KnownValue known(long rawValue, String source) {
            KnownValue value = new KnownValue();
            value.known = true;
            value.value = rawValue & 0xffffffffL;
            value.source = source;
            return value;
        }
    }

    private static class WriterTarget {
        String kind;
        long address;
        boolean hasSequentialArgument;
        boolean argumentIsPicaRegister;
        String argumentSemantic;
        long[] fixedPicaRegisters;
        long[] fixedPicaHeaders;

        WriterTarget(String kind, long address, boolean hasSequentialArgument,
                     boolean argumentIsPicaRegister, String argumentSemantic,
                     long[] fixedPicaRegisters, long[] fixedPicaHeaders) {
            this.kind = kind;
            this.address = address;
            this.hasSequentialArgument = hasSequentialArgument;
            this.argumentIsPicaRegister = argumentIsPicaRegister;
            this.argumentSemantic = argumentSemantic;
            this.fixedPicaRegisters = fixedPicaRegisters;
            this.fixedPicaHeaders = fixedPicaHeaders;
        }
    }

    private String csv(String value) {
        if (value == null) {
            return "";
        }
        return "\"" + value.replace("\"", "\"\"").replace("\r", " ").replace("\n", " ") + "\"";
    }

    private long parseNumber(String text) {
        String normalized = text.toLowerCase(Locale.ROOT);
        if (normalized.startsWith("0x")) {
            return Long.parseUnsignedLong(normalized.substring(2), 16);
        }
        return Long.parseUnsignedLong(normalized, 10);
    }

    private int regIndex(String reg) {
        if (reg == null || reg.length() < 2 || reg.charAt(0) != 'r') {
            return -1;
        }
        try {
            int index = Integer.parseInt(reg.substring(1));
            return index >= 0 && index <= 12 ? index : -1;
        } catch (NumberFormatException ex) {
            return -1;
        }
    }

    private String regName(Object object) {
        if (!(object instanceof Register)) {
            return null;
        }
        String name = ((Register)object).getName().toLowerCase(Locale.ROOT);
        if (regIndex(name) < 0) {
            return null;
        }
        return name;
    }

    private Set<String> writtenRegisters(Instruction instruction) {
        Set<String> registers = new HashSet<>();
        for (Object object : instruction.getResultObjects()) {
            String reg = regName(object);
            if (reg != null) {
                registers.add(reg);
            }
        }
        return registers;
    }

    private long readU32(Address address) {
        try {
            Memory memory = currentProgram.getMemory();
            return memory.getInt(address) & 0xffffffffL;
        } catch (Exception ex) {
            return -1;
        }
    }

    private boolean applyKnownAssignment(String text, KnownValue[] regs) {
        Matcher matcher = MOV_IMMEDIATE.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            regs[dst] = KnownValue.known(parseNumber(matcher.group(2)), "mov_imm");
            return true;
        }

        matcher = MOVW_IMMEDIATE.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            regs[dst] = KnownValue.known(parseNumber(matcher.group(2)) & 0xffffL, "movw_imm");
            return true;
        }

        matcher = MOVT_IMMEDIATE.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            long immediate = parseNumber(matcher.group(2)) & 0xffffL;
            long low = regs[dst].known ? regs[dst].value & 0xffffL : 0;
            regs[dst] = KnownValue.known(low | (immediate << 16), "movt_imm");
            return true;
        }

        matcher = COPY_REGISTER.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            int src = regIndex(matcher.group(2));
            if (regs[src].known) {
                regs[dst] = KnownValue.known(regs[src].value, "copy_" + matcher.group(2));
            } else {
                regs[dst] = KnownValue.unknown();
            }
            return true;
        }

        matcher = ADD_IMMEDIATE.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            int src = regIndex(matcher.group(2));
            if (regs[src].known) {
                regs[dst] = KnownValue.known(regs[src].value + parseNumber(matcher.group(3)), "add_imm");
            } else {
                regs[dst] = KnownValue.unknown();
            }
            return true;
        }

        matcher = SUB_IMMEDIATE.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            int src = regIndex(matcher.group(2));
            if (regs[src].known) {
                regs[dst] = KnownValue.known(regs[src].value - parseNumber(matcher.group(3)), "sub_imm");
            } else {
                regs[dst] = KnownValue.unknown();
            }
            return true;
        }

        matcher = LDR_LITERAL.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            Address literalAddress = toAddr(Long.parseUnsignedLong(matcher.group(2), 16));
            long value = readU32(literalAddress);
            if (value >= 0) {
                regs[dst] = KnownValue.known(value, "ldr_literal_" + literalAddress);
            } else {
                regs[dst] = KnownValue.unknown();
            }
            return true;
        }

        return false;
    }

    private List<Instruction> instructionsBefore(Function function, Address callAddress) {
        List<Instruction> list = new ArrayList<>();
        InstructionIterator iterator = currentProgram.getListing().getInstructions(function.getBody(), true);
        while (iterator.hasNext() && !monitor.isCancelled()) {
            Instruction instruction = iterator.next();
            if (instruction.getAddress().compareTo(callAddress) >= 0) {
                break;
            }
            list.add(instruction);
        }
        int start = Math.max(0, list.size() - LOOKBACK_INSTRUCTION_COUNT);
        return new ArrayList<>(list.subList(start, list.size()));
    }

    private KnownValue[] analyzeWindow(List<Instruction> window) {
        KnownValue[] regs = new KnownValue[13];
        for (int i = 0; i < regs.length; i++) {
            regs[i] = KnownValue.unknown();
        }

        for (Instruction instruction : window) {
            Set<String> writes = writtenRegisters(instruction);
            for (String reg : writes) {
                regs[regIndex(reg)] = KnownValue.unknown();
            }
            String text = instruction.toString().toLowerCase(Locale.ROOT).trim().replaceAll("\\s+", " ");
            applyKnownAssignment(text, regs);
        }
        return regs;
    }

    private String formatValue(KnownValue value) {
        return value.known ? String.format("0x%08x", value.value) : "";
    }

    private boolean coversRegister(KnownValue reg, KnownValue count, KnownValue sequential, long targetRegister) {
        if (!reg.known) {
            return false;
        }
        long start = reg.value;
        if (start == targetRegister) {
            return true;
        }
        if (!count.known || !sequential.known || sequential.value == 0 || count.value == 0) {
            return false;
        }
        long endExclusive = start + count.value;
        return start <= targetRegister && targetRegister < endExclusive;
    }

    private boolean fixedRegistersContain(WriterTarget target, long targetRegister) {
        for (long reg : target.fixedPicaRegisters) {
            if (reg == targetRegister) {
                return true;
            }
        }
        return false;
    }

    private String formatHexList(long[] values) {
        List<String> formatted = new ArrayList<>();
        for (long value : values) {
            formatted.add(String.format("0x%08x", value & 0xffffffffL));
        }
        return String.join(";", formatted);
    }

    private String classify(WriterTarget target, KnownValue reg, KnownValue count, KnownValue sequential) {
        if (!target.argumentIsPicaRegister) {
            if (fixedRegistersContain(target, 0x130L)) {
                return "fixed_register_covers_fragop_shadow";
            }
            return "fixed_vsh_float_uniform_upload";
        }
        if (!reg.known) {
            return "dynamic_register";
        }
        if (coversRegister(reg, count, sequential, 0x130L)) {
            return "covers_fragop_shadow";
        }
        if (coversRegister(reg, count, sequential, 0x08bL)) {
            return "covers_texunit0_shadow";
        }
        if (coversRegister(reg, count, sequential, 0x04dL) ||
            coversRegister(reg, count, sequential, 0x04eL) ||
            coversRegister(reg, count, sequential, 0x06dL)) {
            return "covers_depth_map_shadow";
        }
        return "constant_register";
    }

    private String windowText(List<Instruction> window) {
        List<String> lines = new ArrayList<>();
        int start = Math.max(0, window.size() - 16);
        for (int i = start; i < window.size(); i++) {
            Instruction instruction = window.get(i);
            lines.add(instruction.getAddress() + ": " + instruction.toString());
        }
        return String.join(" | ", lines);
    }

    @Override
    protected void run() throws Exception {
        String outArg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "analysis/pica_register_writer_scan.csv";
        File outFile = new File(outArg);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        WriterTarget[] targets = {
            new WriterTarget("generic_scalar_writer_00307bd8", GENERIC_SCALAR_WRITER_ADDRESS, true,
                             true, "pica_register_id", new long[] {}, new long[] {}),
            new WriterTarget("generic_vector_uniform_writer_00307c94", GENERIC_VECTOR_WRITER_ADDRESS, false,
                             false, "vsh_float_uniform_index",
                             new long[] { 0x02c0L, 0x02c1L },
                             new long[] { 0x000f02c0L, 0x000f02c1L }),
        };
        ReferenceManager referenceManager = currentProgram.getReferenceManager();
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("writer_kind,writer_address,call_address,function,entry,argument_semantic,argument_value,count,sequential,argument_source,count_source,sequential_source,fixed_pica_registers,fixed_pica_headers,classification,window\n");
            int rows = 0;
            for (WriterTarget target : targets) {
                ReferenceIterator references = referenceManager.getReferencesTo(toAddr(target.address));
                while (references.hasNext() && !monitor.isCancelled()) {
                    Reference reference = references.next();
                    Instruction instruction = currentProgram.getListing().getInstructionAt(reference.getFromAddress());
                    if (instruction == null) {
                        continue;
                    }
                    String mnemonic = instruction.getMnemonicString().toLowerCase(Locale.ROOT);
                    if (!mnemonic.startsWith("bl") && !mnemonic.equals("b")) {
                        continue;
                    }
                    Function function = getFunctionContaining(reference.getFromAddress());
                    if (function == null) {
                        continue;
                    }
                    List<Instruction> window = instructionsBefore(function, reference.getFromAddress());
                    KnownValue[] regs = analyzeWindow(window);
                    KnownValue registerId = regs[1];
                    KnownValue count = regs[2];
                    KnownValue sequential = target.hasSequentialArgument
                        ? regs[3]
                        : KnownValue.known(1, "implicit_vector_writer_range");
                    writer.write(String.join(",",
                        csv(target.kind),
                        csv(String.format("0x%08x", target.address)),
                        csv(reference.getFromAddress().toString()),
                        csv(function.getName()),
                        csv(function.getEntryPoint().toString()),
                        csv(target.argumentSemantic),
                        csv(formatValue(registerId)),
                        csv(formatValue(count)),
                        csv(formatValue(sequential)),
                        csv(registerId.source),
                        csv(count.source),
                        csv(sequential.source),
                        csv(formatHexList(target.fixedPicaRegisters)),
                        csv(formatHexList(target.fixedPicaHeaders)),
                        csv(classify(target, registerId, count, sequential)),
                        csv(windowText(window))));
                    writer.write("\n");
                    rows++;
                }
            }
            println("pica register writer calls scanned: " + rows + " -> " + outFile.getAbsolutePath());
        }
    }
}
