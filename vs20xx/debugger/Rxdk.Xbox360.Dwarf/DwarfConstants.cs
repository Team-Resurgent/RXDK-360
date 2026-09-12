// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.

namespace Rxdk.Xbox360.Dwarf
{
    // The DWARF v4 constants we consume. Values per the DWARF 4 spec.
    internal static class DW_TAG
    {
        public const int compile_unit = 0x11;
        public const int subprogram = 0x2e;
        public const int variable = 0x34;
        public const int formal_parameter = 0x05;
        public const int base_type = 0x24;
        public const int pointer_type = 0x0f;
        public const int const_type = 0x26;
        public const int typedef = 0x16;
        public const int structure_type = 0x13;
        public const int lexical_block = 0x0b;
    }

    internal static class DW_AT
    {
        public const int name = 0x03;
        public const int byte_size = 0x0b;
        public const int encoding = 0x3e;
        public const int low_pc = 0x11;
        public const int high_pc = 0x12;
        public const int language = 0x13;
        public const int comp_dir = 0x1b;
        public const int stmt_list = 0x10;
        public const int decl_file = 0x3a;
        public const int decl_line = 0x3b;
        public const int type = 0x49;
        public const int location = 0x02;
        public const int frame_base = 0x40;
        public const int external = 0x3f;
    }

    internal static class DW_FORM
    {
        public const int addr = 0x01;
        public const int block2 = 0x03;
        public const int block4 = 0x04;
        public const int data2 = 0x05;
        public const int data4 = 0x06;
        public const int data8 = 0x07;
        public const int @string = 0x08;
        public const int block = 0x09;
        public const int block1 = 0x0a;
        public const int data1 = 0x0b;
        public const int flag = 0x0c;
        public const int sdata = 0x0d;
        public const int strp = 0x0e;
        public const int udata = 0x0f;
        public const int ref_addr = 0x10;
        public const int ref1 = 0x11;
        public const int ref2 = 0x12;
        public const int ref4 = 0x13;
        public const int ref8 = 0x14;
        public const int ref_udata = 0x15;
        public const int indirect = 0x16;
        public const int sec_offset = 0x17;
        public const int exprloc = 0x18;
        public const int flag_present = 0x19;
        public const int data16 = 0x1e;
        public const int line_strp = 0x1f;
        public const int implicit_const = 0x21;
        public const int strx = 0x1a;
        public const int addrx = 0x1b;
        public const int strx1 = 0x25;
        public const int strx2 = 0x26;
        public const int strx3 = 0x27;
        public const int strx4 = 0x28;
        public const int addrx1 = 0x29;
        public const int addrx2 = 0x2a;
        public const int addrx3 = 0x2b;
        public const int addrx4 = 0x2c;
    }

    // Line-number program opcodes.
    internal static class DW_LNS
    {
        public const int copy = 1;
        public const int advance_pc = 2;
        public const int advance_line = 3;
        public const int set_file = 4;
        public const int set_column = 5;
        public const int negate_stmt = 6;
        public const int set_basic_block = 7;
        public const int const_add_pc = 8;
        public const int fixed_advance_pc = 9;
        public const int set_prologue_end = 10;
        public const int set_epilogue_begin = 11;
        public const int set_isa = 12;
    }

    internal static class DW_LNE
    {
        public const int end_sequence = 1;
        public const int set_address = 2;
        public const int define_file = 3;
        public const int set_discriminator = 4;
    }
}
