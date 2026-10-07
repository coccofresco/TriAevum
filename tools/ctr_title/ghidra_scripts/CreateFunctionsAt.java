// Disassembles and creates a function at each address of a seed list (one
// "0x%08X" per line; an odd address is Thumb), for code Ghidra's analysis
// cannot reach, such as the constructors of a self-relative static-
// initializer table (tools/ctr_title/init_array_entries.py). Analysis then
// follows the new functions' callees.
// @category CTR
import ghidra.app.cmd.disassemble.ArmDisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class CreateFunctionsAt extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: CreateFunctionsAt.java seeds.txt");
        }
        List<String> lines = Files.readAllLines(Paths.get(args[0]));
        int created = 0;
        int existing = 0;
        for (String line : lines) {
            String text = line.trim();
            if (text.isEmpty()) {
                continue;
            }
            long value = Long.decode(text);
            boolean thumb = (value & 1L) != 0L;
            Address entry = toAddr(value & ~1L);
            if (getFunctionAt(entry) != null) {
                ++existing;
                continue;
            }
            if (getInstructionAt(entry) == null) {
                ArmDisassembleCommand command = new ArmDisassembleCommand(entry, null, thumb);
                command.applyTo(currentProgram, monitor);
            }
            if (createFunction(entry, null) != null) {
                ++created;
            } else {
                println("ctr seed failed at " + entry);
            }
        }
        println("ctr seeds created=" + created + " existing=" + existing);
    }
}
