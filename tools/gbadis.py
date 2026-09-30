#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Recursive-descent Thumb/ARM disassembler for GBA ROMs.

It follows branches and calls from the given entry points, resolves PC-relative
literal loads, and marks code that a GSF rip zeroed out ("stripped"). gsfopt
keeps only the bytes a song actually touched, so zeroed code is code the songs
never ran.

    gbadis.py ROM ENTRY [ENTRY ...]      entries in hex; prefix with A for ARM code
    gbadis.py rip.gsflib 0810ce30 0810cac8
"""
import re
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, Cs
from capstone.arm import ARM_OP_IMM

from gbarom import ROM_BASE, load_rom

CONDITIONS = 'eq ne cs hs cc lo mi pl vs vc hi ls ge lt gt le al'.split()


class Disassembler:
    def __init__(self, rom):
        self.rom = rom
        self.insns = {}   # address -> (insn, thumb)
        self.labels = {}
        self.literals = {}
        self.xrefs = {}
        self.stripped = set()  # addresses the walk reached in code that a GSF rip zeroed out
        self.md = {True: Cs(CS_ARCH_ARM, CS_MODE_THUMB), False: Cs(CS_ARCH_ARM, CS_MODE_ARM)}
        for md in self.md.values():
            md.detail = True

    def word(self, addr):
        off = addr - ROM_BASE
        return struct.unpack('<I', self.rom[off:off + 4])[0]

    def in_rom(self, addr):
        return ROM_BASE <= addr <= ROM_BASE + len(self.rom) - 4

    def literal_address(self, ins, thumb):
        m = re.search(r'\[pc, #(-?0x[0-9a-f]+|-?\d+)\]', ins.op_str)
        pc = (ins.address + 4) & ~3 if thumb else ins.address + 8
        return pc + (int(m.group(1), 0) if m else 0)

    def run(self, entries):
        work = list(entries)
        while work:
            addr, thumb = work.pop()
            while addr not in self.insns and addr not in self.stripped and self.in_rom(addr):
                off = addr - ROM_BASE
                if self.rom[off:off + 4] == b'\0\0\0\0':
                    self.stripped.add(addr)
                    self.labels.setdefault(addr, 'STRIPPED_%08x' % addr)
                    break
                ins = next(self.md[thumb].disasm(self.rom[off:off + 4], addr), None)
                if ins is None:
                    self.labels.setdefault(addr, 'BAD_%08x' % addr)
                    break
                self.insns[addr] = (ins, thumb)
                m = ins.mnemonic
                if m.startswith('ldr') and '[pc' in ins.op_str:
                    lit = self.literal_address(ins, thumb)
                    if self.in_rom(lit):
                        self.literals[lit] = self.word(lit)
                # ARM code has conditional calls too, such as bleq.
                is_call = m in ('bl', 'blx') or (m.startswith('bl') and m[2:] in CONDITIONS)
                is_branch = is_call or m == 'b' or (m.startswith('b') and m[1:] in CONDITIONS)
                if is_branch and ins.operands and ins.operands[0].type == ARM_OP_IMM:
                    target = ins.operands[0].imm
                    self.labels.setdefault(target, ('sub_%08x' if is_call else 'loc_%08x') % target)
                    self.xrefs.setdefault(target, []).append(addr)
                    work.append((target, (not thumb) if m == 'blx' else thumb))
                ends = (m in ('b', 'bx') or (m in ('pop', 'ldm') and 'pc' in ins.op_str) or
                        (m in ('mov', 'add', 'ldr') and ins.op_str.startswith('pc,')))
                if ends:
                    break
                addr += ins.size

    def listing(self):
        prev = None
        for addr in sorted(set(self.insns) | set(self.literals) | self.stripped):
            if prev is not None and addr > prev:
                print('        ; gap %x bytes' % (addr - prev))
            if addr in self.labels:
                refs = self.xrefs.get(addr, [])
                callers = '   ; from ' + ' '.join('%x' % r for r in refs[:6]) if refs else ''
                print('\n%s:%s' % (self.labels[addr], callers))
            if addr in self.insns:
                ins, thumb = self.insns[addr]
                comment = ''
                if ins.mnemonic.startswith('ldr') and '[pc' in ins.op_str:
                    lit = self.literal_address(ins, thumb)
                    if self.in_rom(lit):
                        comment = ' ; =0x%08x' % self.word(lit)
                ops = ins.op_str
                if ins.operands and ins.operands[0].type == ARM_OP_IMM and ins.mnemonic.startswith('b'):
                    ops = self.labels.get(ins.operands[0].imm, ops)
                mode = '' if thumb else 'A:'
                print('%08x: %-10s %s%-7s %s%s' % (addr, ins.bytes.hex(), mode, ins.mnemonic, ops, comment))
                prev = addr + ins.size
            elif addr in self.literals:
                print('%08x: .word 0x%08x' % (addr, self.literals[addr]))
                prev = addr + 4
            else:
                print('%08x: <stripped>' % addr)
                prev = addr + 2


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    dis = Disassembler(load_rom(sys.argv[1]))
    entries = []
    for arg in sys.argv[2:]:
        thumb = not arg.upper().startswith('A')
        addr = int(arg.lstrip('Aa'), 16) & ~1
        dis.labels[addr] = 'sub_%08x' % addr
        entries.append((addr, thumb))
    dis.run(entries)
    dis.listing()


if __name__ == '__main__':
    main()
