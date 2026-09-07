# Azahar PICA Shader Decompiler Import

This directory contains the minimal source subset used to translate native PICA
vertex programs into cacheable GLSL for the OOT3D native application.

- Azahar source revision: `beb5681ee7f85586501b16b083a961b707092cd7`
- Imported Azahar files: `glsl_shader_decompiler.cpp` and
  `glsl_shader_decompiler.h`
- Azahar license for those files: GPLv2 or later, retained in their headers
- Imported nihstro files: `bit_field.h` and `shader_bytecode.h`
- nihstro license: 3-clause BSD, copied as `NIHSTRO_LICENSE.txt`

The compatibility headers in this directory expose only the types and macros
required by the imported decompiler. They do not import Azahar's renderer,
memory system, frontend, UI, network stack, or emulator scheduler.
