// Headless Ghidra pre-script for any 3DS title's decompressed .code loaded
// with BinaryLoader at its text address. The ExHeader code-set layout comes
// from the arguments (tools/ctr_title/ghidra_inventory.py reads it from the
// process manifest), so no title layout is hard-coded here.
//
// Arguments: prefix profile text rodata data bss bss_size (hex or decimal).
// Blocks are named <prefix>_text, <prefix>_rodata, <prefix>_data, <prefix>_bss.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

public class ImportCtrCodeSet extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 7) {
            throw new IllegalArgumentException(
                "ImportCtrCodeSet needs prefix profile text rodata data bss bss_size");
        }
        String prefix = args[0];
        String profile = args[1];
        long text = parse(args[2]);
        long rodata = parse(args[3]);
        long data = parse(args[4]);
        long bss = parse(args[5]);
        long bssSize = parse(args[6]);

        currentProgram.setName(prefix + "_code_" + profile);
        Memory memory = currentProgram.getMemory();
        splitAt(memory, rodata);
        splitAt(memory, data);
        configureBlock(memory, text, prefix + "_text", true, false, true);
        configureBlock(memory, rodata, prefix + "_rodata", true, false, false);
        configureBlock(memory, data, prefix + "_data", true, true, false);
        if (bssSize > 0) createVirtualBlock(prefix + "_bss", bss, bssSize);
        // CTR application address space (the same for every title).
        createVirtualBlock(prefix + "_ipc_mapping", 0x04000000L, 0x04000000L);
        createVirtualBlock(prefix + "_heap", 0x08000000L, 0x08000000L);
        createVirtualBlock(prefix + "_shared_memory", 0x10000000L, 0x04000000L);
        createVirtualBlock(prefix + "_linear_heap", 0x14000000L, 0x08000000L);
        createVirtualBlock(prefix + "_vram", 0x1f000000L, 0x00600000L);
        println("ctr code-set import prefix=" + prefix + " profile=" + profile);
    }

    private static long parse(String value) {
        return value.startsWith("0x") || value.startsWith("0X")
            ? Long.parseUnsignedLong(value.substring(2), 16) : Long.parseLong(value);
    }

    private Address address(long value) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
    }

    private void splitAt(Memory memory, long value) throws Exception {
        MemoryBlock block = memory.getBlock(address(value));
        if (block != null && !block.getStart().equals(address(value))) memory.split(block, address(value));
    }

    private void configureBlock(Memory memory, long value, String name, boolean read,
                                boolean write, boolean execute) throws Exception {
        MemoryBlock block = memory.getBlock(address(value));
        if (block == null) throw new IllegalStateException("missing code-set block at " + address(value));
        block.setName(name);
        block.setRead(read);
        block.setWrite(write);
        block.setExecute(execute);
        println("ctr segment name=" + name + " start=" + block.getStart() + " end=" + block.getEnd());
    }

    private void createVirtualBlock(String name, long start, long size) throws Exception {
        Memory memory = currentProgram.getMemory();
        if (memory.getBlock(address(start)) != null) return;
        MemoryBlock block = memory.createUninitializedBlock(name, address(start), size, false);
        block.setRead(true);
        block.setWrite(true);
        block.setExecute(false);
    }
}
