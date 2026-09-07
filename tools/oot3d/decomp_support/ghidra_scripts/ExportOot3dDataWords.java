import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class ExportOot3dDataWords extends GhidraScript {
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
                        throw new IllegalArgumentException("Expected 8 hex digits in data export list: " + raw);
                    }
                    entries.add(value);
                }
            }
        }
        return entries;
    }

    private String bytesHex(Memory memory, Address address, int count) throws Exception {
        byte[] bytes = new byte[count];
        memory.getBytes(address, bytes);
        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < bytes.length; i++) {
            if (i > 0) {
                builder.append(' ');
            }
            builder.append(String.format("%02x", bytes[i] & 0xff));
        }
        return builder.toString();
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outArg = args.length > 0 ? args[0] : "analysis/data_words.csv";
        String entriesArg = args.length > 1 ? args[1] : "analysis/selected_export_entries.txt";
        int byteCount = args.length > 2 ? Integer.parseInt(args[2]) : 16;

        File outFile = new File(outArg);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
        File entriesFile = new File(entriesArg);
        if (!entriesFile.isFile()) {
            throw new IllegalArgumentException("data export entry list does not exist: " + entriesFile.getAbsolutePath());
        }

        Memory memory = currentProgram.getMemory();
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(outFile))) {
            writer.write("address,bytes,u32,float32\n");
            for (String entry : readEntries(entriesFile)) {
                Address address = toAddr(Long.parseUnsignedLong(entry, 16));
                int word = memory.getInt(address);
                writer.write(String.join(",",
                    csv(address.toString()),
                    csv(bytesHex(memory, address, byteCount)),
                    csv(String.format("0x%08x", word)),
                    csv(Float.toString(Float.intBitsToFloat(word)))));
                writer.write("\n");
            }
        }
        println("exported data words: " + outFile.getAbsolutePath());
    }
}
