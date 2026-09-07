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

public class FindOot3dPicaPacketCopies extends GhidraScript {
    private static final long PACKET_COPY_ADDRESS = 0x00307af4L;
    private static final long TARGET_REGISTER = 0x0130L;
    private static final int LOOKBACK_INSTRUCTION_COUNT = 96;

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

    private boolean looksLikePicaHeader(long value) {
        long register = value & 0xffffL;
        long mask = (value >>> 16) & 0xfL;
        long countMinusOne = (value >>> 20) & 0x7ffL;
        return mask != 0 && register <= 0x2ffL && countMinusOne < 0x200L;
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

    private String summarizeStaticPacket(KnownValue source, KnownValue length) {
        if (!source.known || !length.known || length.value == 0 || length.value > 0x400) {
            return "";
        }
        List<String> words = new ArrayList<>();
        for (long offset = 0; offset + 4 <= length.value; offset += 4) {
            long value = readU32(toAddr(source.value + offset));
            if (value < 0) {
                words.add(String.format("0x%04x:<unreadable>", offset));
            } else if (looksLikePicaHeader(value)) {
                String classification = coversTargetRegister(value) ? "covers_fragop_shadow" : "pica_header";
                words.add(String.format("0x%04x:0x%08x:%s:reg=0x%03x:count=%d:seq=%s",
                    offset, value, classification, value & 0xffffL, ((value >>> 20) & 0x7ffL) + 1,
                    Boolean.toString((value & 0x80000000L) != 0)));
            }
        }
        return String.join(";", words);
    }

    private String formatKnown(KnownValue value) {
        return value.known ? String.format("0x%08x", value.value) : "";
    }

    private String classifyCopy(KnownValue source, KnownValue length, String packetSummary) {
        if (packetSummary.contains("covers_fragop_shadow")) {
            return "static_packet_covers_fragop_shadow";
        }
        if (source.known && length.known && !packetSummary.isEmpty()) {
            return "static_packet_with_pica_headers";
        }
        if (source.known && length.known) {
            return "static_or_resolved_packet_no_pica_headers";
        }
        return "dynamic_packet_copy";
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
        String outArg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "analysis/pica_packet_copy_scan.csv";
        File outFile = new File(outArg);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        ReferenceManager referenceManager = currentProgram.getReferenceManager();
        int rows = 0;
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("call_address,function,entry,context_arg,source_arg,length_arg,context_source,source_source,length_source,classification,static_packet_headers,window\n");
            ReferenceIterator references = referenceManager.getReferencesTo(toAddr(PACKET_COPY_ADDRESS));
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
                KnownValue context = regs[0];
                KnownValue source = regs[1];
                KnownValue length = regs[2];
                String packetSummary = summarizeStaticPacket(source, length);
                writer.write(String.join(",",
                    csv(reference.getFromAddress().toString()),
                    csv(function.getName()),
                    csv(function.getEntryPoint().toString()),
                    csv(formatKnown(context)),
                    csv(formatKnown(source)),
                    csv(formatKnown(length)),
                    csv(context.source),
                    csv(source.source),
                    csv(length.source),
                    csv(classifyCopy(source, length, packetSummary)),
                    csv(packetSummary),
                    csv(windowText(window))));
                writer.write("\n");
                rows++;
            }
        }
        println("PICA packet copy calls scanned: " + rows + " -> " + outFile.getAbsolutePath());
    }
}
