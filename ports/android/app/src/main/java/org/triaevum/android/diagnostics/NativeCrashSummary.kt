package org.triaevum.android.diagnostics

/**
 * The redacted summary of a native crash: what Android's tombstone (the protobuf that
 * `ApplicationExitInfo.getTraceInputStream` returns for a native crash, API 31+) says about the
 * signal and the crashing thread's frames, reduced to what a support report needs and nothing
 * that identifies the user: the signal and its code, whether the fault address was in the null
 * page, the crashing thread's name and up to [MAX_FRAMES] frames as library name, module-relative
 * PC, symbol and build ID. Library paths become file names; libraries that are neither the app's
 * own nor the system's are "<other>". No abort message, registers, memory, maps, open files,
 * command line or log buffers are read into it.
 *
 * The parser reads only the protobuf wire format of the fields it uses
 * (system/core/debuggerd/proto/tombstone.proto) and skips everything else.
 */
data class NativeCrashSummary(
    val signal: Int,
    val signalName: String,
    val code: Int,
    val codeName: String,
    /** Null when the tombstone records no fault address. */
    val faultInNullPage: Boolean?,
    val threadName: String,
    val frames: List<Frame>,
) {
    data class Frame(val module: String, val relativePc: Long, val symbol: String?, val buildId: String?)

    /** The summary as report lines (the support bundle's native-crash.txt and the report section). */
    fun format(header: String): String {
        val out = StringBuilder(header).append('\n')
        out.append("Signal: ").append(signal)
        if (signalName.isNotEmpty()) out.append(" (").append(signalName).append(')')
        out.append(", code ").append(code)
        if (codeName.isNotEmpty()) out.append(" (").append(codeName).append(')')
        when (faultInNullPage) {
            true -> out.append(", fault address in the null page")
            false -> out.append(", fault address outside the null page")
            null -> {}
        }
        out.append('\n')
        if (threadName.isNotEmpty()) out.append("Thread: ").append(threadName).append('\n')
        if (frames.isEmpty()) out.append("No frames recorded.\n")
        frames.forEachIndexed { index, frame ->
            out.append(String.format(java.util.Locale.ROOT, "  #%02d %s +0x%x", index, frame.module, frame.relativePc))
            frame.symbol?.let { out.append(" (").append(it).append(')') }
            frame.buildId?.let { out.append(" [").append(it).append(']') }
            out.append('\n')
        }
        return out.toString()
    }

    companion object {
        const val MAX_FRAMES = 12
        const val MAX_TOMBSTONE_BYTES = 8 * 1024 * 1024
        private const val MAX_SYMBOL = 120
        private const val OTHER = "<other>"
        private val SYSTEM_PREFIXES = listOf("/system/", "/vendor/", "/apex/", "/odm/", "/product/", "/system_ext/")

        /**
         * Parses a tombstone. [appLibraries] are the file names of the app's own libraries (the
         * native library folder), shown with their symbols; system libraries are shown by file name.
         * Null when the bytes are not a tombstone with a crashing thread or a signal.
         */
        fun parse(bytes: ByteArray, appLibraries: Set<String>): NativeCrashSummary? = try {
            parseOrThrow(bytes, appLibraries)
        } catch (_: IllegalArgumentException) {
            null
        }

        private fun parseOrThrow(bytes: ByteArray, appLibraries: Set<String>): NativeCrashSummary? {
            var tid = -1L
            var signal: ByteArray? = null
            val threads = HashMap<Long, ByteArray>()
            Wire(bytes, 0, bytes.size).forEach { field, wire, reader ->
                when {
                    field == 6 && wire == VARINT -> tid = reader.varint()
                    field == 10 && wire == LENGTH -> signal = reader.bytes()
                    field == 16 && wire == LENGTH -> {
                        // map<uint32, Thread>: entries with key 1 and value 2.
                        var key = -1L
                        var value: ByteArray? = null
                        reader.message().forEach { entryField, entryWire, entry ->
                            when {
                                entryField == 1 && entryWire == VARINT -> key = entry.varint()
                                entryField == 2 && entryWire == LENGTH -> value = entry.bytes()
                                else -> entry.skip(entryWire)
                            }
                        }
                        value?.let { threads[key] = it }
                    }
                    else -> reader.skip(wire)
                }
            }
            if (signal == null && threads.isEmpty()) return null
            var number = 0
            var name = ""
            var code = 0
            var codeName = ""
            var hasFault = false
            var faultAddress = 0L
            signal?.let { data ->
                Wire(data, 0, data.size).forEach { field, wire, reader ->
                    when {
                        field == 1 && wire == VARINT -> number = reader.varint().toInt()
                        field == 2 && wire == LENGTH -> name = printable(reader.string(), 16)
                        field == 3 && wire == VARINT -> code = reader.varint().toInt()
                        field == 4 && wire == LENGTH -> codeName = printable(reader.string(), 32)
                        field == 8 && wire == VARINT -> hasFault = reader.varint() != 0L
                        field == 9 && wire == VARINT -> faultAddress = reader.varint()
                        else -> reader.skip(wire)
                    }
                }
            }
            var threadName = ""
            val frames = ArrayList<Frame>()
            (threads[tid] ?: threads.values.firstOrNull())?.let { data ->
                Wire(data, 0, data.size).forEach { field, wire, reader ->
                    when {
                        field == 2 && wire == LENGTH -> threadName = printable(reader.string(), 32)
                        field == 4 && wire == LENGTH -> {
                            val frame = reader.bytes()
                            if (frames.size < MAX_FRAMES) frames += frame(frame, appLibraries)
                        }
                        else -> reader.skip(wire)
                    }
                }
            }
            return NativeCrashSummary(
                signal = number,
                signalName = name,
                code = code,
                codeName = codeName,
                faultInNullPage = if (hasFault) java.lang.Long.compareUnsigned(faultAddress, 0x1000L) < 0 else null,
                threadName = threadName,
                frames = frames,
            )
        }

        private fun frame(data: ByteArray, appLibraries: Set<String>): Frame {
            var relativePc = 0L
            var function = ""
            var functionOffset = 0L
            var file = ""
            var buildId = ""
            Wire(data, 0, data.size).forEach { field, wire, reader ->
                when {
                    field == 1 && wire == VARINT -> relativePc = reader.varint()
                    field == 4 && wire == LENGTH -> function = reader.string()
                    field == 5 && wire == VARINT -> functionOffset = reader.varint()
                    field == 6 && wire == LENGTH -> file = reader.string()
                    field == 8 && wire == LENGTH -> buildId = reader.string()
                    else -> reader.skip(wire)
                }
            }
            val module = module(file, appLibraries)
            val known = module != OTHER
            val symbol = if (known && function.isNotEmpty()) {
                printable(function, MAX_SYMBOL) + (if (functionOffset != 0L) "+$functionOffset" else "")
            } else {
                null
            }
            val id = buildId.takeIf { known && it.isNotEmpty() && it.all { c -> c in '0'..'9' || c in 'a'..'f' } }?.take(40)
            return Frame(module, relativePc, symbol, id)
        }

        /** A library's file name when it is the app's own or the system's; never a path. */
        fun module(file: String, appLibraries: Set<String>): String {
            if (file.isEmpty()) return OTHER
            // A library loaded from inside the APK: "<apk>!/lib/arm64-v8a/libX.so".
            val inner = file.substringAfterLast("!/", file)
            val name = inner.substringAfterLast('/')
            if (name in appLibraries) return name
            if (SYSTEM_PREFIXES.any { file.startsWith(it) } && name.isNotEmpty()) return printable(name, 64)
            return OTHER
        }

        private fun printable(text: String, limit: Int): String =
            text.filter { it in ' '..'~' }.take(limit)

        private const val VARINT = 0
        private const val FIXED64 = 1
        private const val LENGTH = 2
        private const val FIXED32 = 5
    }

    /** A bounded reader of one protobuf message's fields. */
    private class Wire(private val data: ByteArray, private var position: Int, private val end: Int) {
        fun forEach(visit: (field: Int, wire: Int, reader: Wire) -> Unit) {
            while (position < end) {
                val key = varint()
                val field = (key ushr 3).toInt()
                val wire = (key and 7L).toInt()
                if (field <= 0) throw IllegalArgumentException("invalid field")
                visit(field, wire, this)
            }
        }

        fun varint(): Long {
            var result = 0L
            var shift = 0
            while (shift < 64) {
                if (position >= end) throw IllegalArgumentException("truncated varint")
                val byte = data[position++].toInt()
                result = result or ((byte and 0x7f).toLong() shl shift)
                if (byte and 0x80 == 0) return result
                shift += 7
            }
            throw IllegalArgumentException("varint too long")
        }

        private fun length(): Int {
            val length = varint()
            if (length < 0 || length > end - position) throw IllegalArgumentException("length outside message")
            return length.toInt()
        }

        fun bytes(): ByteArray {
            val length = length()
            return data.copyOfRange(position, position + length).also { position += length }
        }

        fun string(): String = String(bytes(), Charsets.UTF_8)

        fun message(): Wire {
            val length = length()
            return Wire(data, position, position + length).also { position += length }
        }

        fun skip(wire: Int) {
            when (wire) {
                VARINT -> varint()
                FIXED64 -> advance(8)
                LENGTH -> advance(length())
                FIXED32 -> advance(4)
                else -> throw IllegalArgumentException("unsupported wire type $wire")
            }
        }

        private fun advance(count: Int) {
            if (count > end - position) throw IllegalArgumentException("field outside message")
            position += count
        }
    }
}
