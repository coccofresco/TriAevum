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

public class FindOot3dDirectPicaCommandWriters extends GhidraScript {
    private static final long COMMAND_POINTER_COMMIT_ADDRESS = 0x003084dcL;
    private static final long TARGET_REGISTER = 0x0130L;

    private static final Pattern MOV_IMMEDIATE =
        Pattern.compile("^mov[a-z.]*\\s+(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern MOVW_IMMEDIATE =
        Pattern.compile("^movw[a-z.]*\\s+(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern MOVT_IMMEDIATE =
        Pattern.compile("^movt[a-z.]*\\s+(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern COPY_REGISTER =
        Pattern.compile("^(?:cpy|mov)[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d))$");
    private static final Pattern MOV_SHIFTED_REGISTER =
        Pattern.compile("^mov[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d)),\\s*lsl\\s*#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern ADD_IMMEDIATE =
        Pattern.compile("^add[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern SUB_IMMEDIATE =
        Pattern.compile("^sub[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern ORR_IMMEDIATE =
        Pattern.compile("^orr[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d)),#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern ORR_REGISTER =
        Pattern.compile("^orr[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d))$");
    private static final Pattern ORR_SHIFTED_REGISTER =
        Pattern.compile("^orr[a-z.]*\\s+(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d)),(r(?:1[0-2]|\\d)),\\s*lsl\\s*#(0x[0-9a-f]+|\\d+)$");
    private static final Pattern LDR_LITERAL =
        Pattern.compile("^ldr[a-z.]*\\s+(r(?:1[0-2]|\\d)),\\[(?:0x)?([0-9a-f]+)\\]!?$");
    private static final Pattern STR_REGISTER =
        Pattern.compile("^str[a-z.]*\\s+(r(?:1[0-2]|\\d)),\\[(r(?:1[0-2]|\\d))(?:,#(0x[0-9a-f]+|\\d+))?\\]!?$");
    private static final Pattern STM =
        Pattern.compile("^stm(?:ia|ib)[a-z.]*\\s+(r(?:1[0-2]|\\d)),\\{([^}]+)\\}$");

    private static class KnownValue {
        boolean known;
        long value;
        String source;

        static KnownValue unknown() {
            KnownValue result = new KnownValue();
            result.known = false;
            result.source = "unknown";
            return result;
        }

        static KnownValue known(long rawValue, String source) {
            KnownValue result = new KnownValue();
            result.known = true;
            result.value = rawValue & 0xffffffffL;
            result.source = source;
            return result;
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
            regs[regIndex(matcher.group(1))] = KnownValue.known(parseNumber(matcher.group(2)), "mov_imm");
            return true;
        }

        matcher = MOVW_IMMEDIATE.matcher(text);
        if (matcher.matches()) {
            regs[regIndex(matcher.group(1))] = KnownValue.known(parseNumber(matcher.group(2)) & 0xffffL, "movw_imm");
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
            regs[dst] = regs[src].known ? KnownValue.known(regs[src].value, "copy_" + matcher.group(2)) : KnownValue.unknown();
            return true;
        }

        matcher = MOV_SHIFTED_REGISTER.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            int src = regIndex(matcher.group(2));
            long shift = parseNumber(matcher.group(3));
            regs[dst] = regs[src].known ? KnownValue.known(regs[src].value << shift, "mov_lsl_" + matcher.group(2)) : KnownValue.unknown();
            return true;
        }

        matcher = ADD_IMMEDIATE.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            int src = regIndex(matcher.group(2));
            regs[dst] = regs[src].known ? KnownValue.known(regs[src].value + parseNumber(matcher.group(3)), "add_imm") : KnownValue.unknown();
            return true;
        }

        matcher = SUB_IMMEDIATE.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            int src = regIndex(matcher.group(2));
            regs[dst] = regs[src].known ? KnownValue.known(regs[src].value - parseNumber(matcher.group(3)), "sub_imm") : KnownValue.unknown();
            return true;
        }

        matcher = ORR_IMMEDIATE.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            int src = regIndex(matcher.group(2));
            regs[dst] = regs[src].known ? KnownValue.known(regs[src].value | parseNumber(matcher.group(3)), "orr_imm") : KnownValue.unknown();
            return true;
        }

        matcher = ORR_REGISTER.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            int srcA = regIndex(matcher.group(2));
            int srcB = regIndex(matcher.group(3));
            regs[dst] = regs[srcA].known && regs[srcB].known
                ? KnownValue.known(regs[srcA].value | regs[srcB].value, "orr_reg")
                : KnownValue.unknown();
            return true;
        }

        matcher = ORR_SHIFTED_REGISTER.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            int srcA = regIndex(matcher.group(2));
            int srcB = regIndex(matcher.group(3));
            long shift = parseNumber(matcher.group(4));
            regs[dst] = regs[srcA].known && regs[srcB].known
                ? KnownValue.known(regs[srcA].value | (regs[srcB].value << shift), "orr_lsl_reg")
                : KnownValue.unknown();
            return true;
        }

        matcher = LDR_LITERAL.matcher(text);
        if (matcher.matches()) {
            int dst = regIndex(matcher.group(1));
            Address literalAddress = toAddr(Long.parseUnsignedLong(matcher.group(2), 16));
            long value = readU32(literalAddress);
            regs[dst] = value >= 0 ? KnownValue.known(value, "ldr_literal_" + literalAddress) : KnownValue.unknown();
            return true;
        }

        return false;
    }

    private String formatKnown(KnownValue value) {
        return value.known ? String.format("0x%08x", value.value) : "";
    }

    private boolean looksLikePicaHeader(long value) {
        long register = value & 0xffffL;
        long mask = (value >>> 16) & 0xfL;
        long countMinusOne = (value >>> 20) & 0x7ffL;
        if (mask == 0 || register > 0x2ffL) {
            return false;
        }
        return countMinusOne < 0x200L;
    }

    private boolean coversTargetRegister(long header) {
        long register = header & 0xffffL;
        long count = ((header >>> 20) & 0x7ffL) + 1;
        boolean sequential = (header & 0x80000000L) != 0;
        if (register == TARGET_REGISTER) {
            return true;
        }
        return sequential && register <= TARGET_REGISTER && TARGET_REGISTER < register + count;
    }

    private String classify(KnownValue value) {
        if (!value.known) {
            return "unknown_store_value";
        }
        if (!looksLikePicaHeader(value.value)) {
            return "known_payload_or_non_pica_header";
        }
        if (coversTargetRegister(value.value)) {
            return "covers_fragop_shadow";
        }
        long register = value.value & 0xffffL;
        if (register == 0x04dL || register == 0x04eL || register == 0x06dL) {
            return "known_shadow_depth_header";
        }
        if (register == 0x08bL) {
            return "known_texunit0_shadow_header";
        }
        return "known_pica_header";
    }

    private String decodeRegister(KnownValue value) {
        return value.known && looksLikePicaHeader(value.value) ? String.format("0x%03x", value.value & 0xffffL) : "";
    }

    private String decodeCount(KnownValue value) {
        return value.known && looksLikePicaHeader(value.value) ? Long.toString(((value.value >>> 20) & 0x7ffL) + 1) : "";
    }

    private String decodeSequential(KnownValue value) {
        return value.known && looksLikePicaHeader(value.value) ? Boolean.toString((value.value & 0x80000000L) != 0) : "";
    }

    private void writeStoreRow(BufferedWriter writer, Function function, Address commitAddress, Instruction store,
                               String storeKind, String sourceRegister, String baseRegister, long wordOffset,
                               KnownValue value) throws Exception {
        String classification = classify(value);
        if ("known_payload_or_non_pica_header".equals(classification)) {
            return;
        }
        writer.write(String.join(",",
            csv(function.getName()),
            csv(function.getEntryPoint().toString()),
            csv(commitAddress.toString()),
            csv(store.getAddress().toString()),
            csv(storeKind),
            csv(sourceRegister),
            csv(baseRegister),
            csv(Long.toString(wordOffset)),
            csv(formatKnown(value)),
            csv(value.source),
            csv(decodeRegister(value)),
            csv(decodeCount(value)),
            csv(decodeSequential(value)),
            csv(classification),
            csv(store.toString())));
        writer.write("\n");
    }

    private List<Instruction> functionInstructions(Function function) {
        List<Instruction> result = new ArrayList<>();
        InstructionIterator iterator = currentProgram.getListing().getInstructions(function.getBody(), true);
        while (iterator.hasNext() && !monitor.isCancelled()) {
            result.add(iterator.next());
        }
        return result;
    }

    private void scanFunctionUntilCommit(BufferedWriter writer, Function function, Address commitAddress) throws Exception {
        KnownValue[] regs = new KnownValue[13];
        for (int i = 0; i < regs.length; i++) {
            regs[i] = KnownValue.unknown();
        }

        List<Instruction> instructions = functionInstructions(function);
        for (Instruction instruction : instructions) {
            if (instruction.getAddress().compareTo(commitAddress) >= 0) {
                break;
            }

            String text = instruction.toString().toLowerCase(Locale.ROOT).trim().replaceAll("\\s+", " ");

            Matcher strMatcher = STR_REGISTER.matcher(text);
            if (strMatcher.matches()) {
                int src = regIndex(strMatcher.group(1));
                long byteOffset = strMatcher.group(3) == null ? 0 : parseNumber(strMatcher.group(3));
                writeStoreRow(writer, function, commitAddress, instruction, "str", strMatcher.group(1),
                              strMatcher.group(2), byteOffset / 4, regs[src]);
            }

            Matcher stmMatcher = STM.matcher(text);
            if (stmMatcher.matches()) {
                boolean incrementBefore = text.startsWith("stmib");
                long wordOffset = incrementBefore ? 1 : 0;
                String[] rawRegs = stmMatcher.group(2).split(",");
                for (String rawReg : rawRegs) {
                    String sourceRegister = rawReg.trim();
                    int src = regIndex(sourceRegister);
                    if (src >= 0) {
                        writeStoreRow(writer, function, commitAddress, instruction, text.substring(0, 5).trim(),
                                      sourceRegister, stmMatcher.group(1), wordOffset, regs[src]);
                    }
                    wordOffset++;
                }
            }

            Set<String> writes = writtenRegisters(instruction);
            for (String reg : writes) {
                regs[regIndex(reg)] = KnownValue.unknown();
            }
            applyKnownAssignment(text, regs);
        }
    }

    @Override
    protected void run() throws Exception {
        String outArg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "analysis/direct_pica_command_writer_scan.csv";
        File outFile = new File(outArg);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        ReferenceManager referenceManager = currentProgram.getReferenceManager();
        int commitRefs = 0;
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("function,entry,commit_address,store_address,store_kind,source_register,base_register,word_offset,store_value,store_value_source,decoded_register,decoded_count,sequential,classification,instruction\n");
            ReferenceIterator references = referenceManager.getReferencesTo(toAddr(COMMAND_POINTER_COMMIT_ADDRESS));
            while (references.hasNext() && !monitor.isCancelled()) {
                Reference reference = references.next();
                Instruction branch = currentProgram.getListing().getInstructionAt(reference.getFromAddress());
                if (branch == null) {
                    continue;
                }
                String mnemonic = branch.getMnemonicString().toLowerCase(Locale.ROOT);
                if (!mnemonic.startsWith("b")) {
                    continue;
                }
                Function function = getFunctionContaining(reference.getFromAddress());
                if (function == null) {
                    continue;
                }
                scanFunctionUntilCommit(writer, function, reference.getFromAddress());
                commitRefs++;
            }
        }
        println("direct PICA command writer commit refs scanned: " + commitRefs + " -> " + outFile.getAbsolutePath());
    }
}
