#!/usr/bin/env python3
"""Stage pinned Daedalus graphics; upstream remains byte-for-byte preserved."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["HLEGraphics/" + name for name in (
    "DLParser.cpp", "BaseRenderer.cpp", "Microcode.cpp", "RDPStateManager.cpp",
    "RDP.cpp", "TextureInfo.cpp", "uCodes/Ucode.cpp")]+["Graphics/ColourValue.cpp"]


def replace(text, old, new):
    if old not in text:
        raise ValueError("Daedalus adapter anchor changed: " + old[:90])
    return text.replace(old, new)


def stage(output):
    source = ROOT / 'experiments/daedalus/upstream'
    output = Path(output)
    manifest = json.loads((source / 'UPSTREAM.json').read_text())
    for name, sha in manifest['files'].items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != sha:
            raise ValueError('Daedalus source changed: ' + name)
    shutil.copytree(source / 'Source', output, dirs_exist_ok=True)
    for p in output.rglob('*'):
        if not p.is_file() or p.suffix not in ('.cpp', '.h', '.inl'):
            continue
        text = re.sub(r'defined\s*\(\s*DAEDALUS_GL\s*\)',
                      'defined(DAEDALUS_SOFTWARE)', p.read_text())
        # Every pushed list is checked even with upstream assertions disabled.
        text = text.replace('gDlistStackPointer++;', '''{ if (gDlistStackPointer < 0 || unsigned(gDlistStackPointer + 1) >= sizeof(gDlistStack.address)/sizeof(gDlistStack.address[0])) {
            tab5_daedalus_fail(); return;
        }
        gDlistStackPointer++; }''')
        # Tri batching bypasses FetchNextCommand in upstream. Bound its reads
        # and include these commands in the same task budget.
        text = re.sub(r'(\s*)(command\.inst\.cmd0\s*=\s*\*pCmdBase\+\+;)',
                      r'\1if (!IsAddressValid(pc, 8, "batch") || !tab5_daedalus_command()) { pc += 8; break; }\1\2', text)
        if p.name == 'DLParser.cpp':
            text = replace(text, 'static inline bool IsAddressValid(u32 address, u32 size, const char * name)',
                           'extern void tab5_daedalus_fail();\nextern bool tab5_daedalus_command();\nextern bool tab5_daedalus_failed();\nstatic inline bool IsAddressValid(u32 address, u32 size, const char * name)')
            text = replace(text, 'if ((address + size) > MAX_RAM_ADDRESS)',
                           'if (address > MAX_RAM_ADDRESS || size > MAX_RAM_ADDRESS - address)')
            text = replace(text, 'DBGConsole_Msg(0,"%s: Address is out of range (0x%08x)", name, address);',
                           'tab5_daedalus_fail();\n\t\tDBGConsole_Msg(0,"%s: Address is out of range (0x%08x)", name, address);')
            text = replace(text, 'if ((address + (n * size)) > MAX_RAM_ADDRESS)',
                           'if (address > MAX_RAM_ADDRESS || n > (MAX_RAM_ADDRESS - address)/size)')
            text = replace(text, 'if ((n + v0) > kMaxN64Vertices)',
                           'if (v0 > kMaxN64Vertices || n > kMaxN64Vertices - v0)')
            text = replace(text, 'u32 & pc( gDlistStack.address[gDlistStackPointer] );',
                           'if (!tab5_daedalus_command()) return false;\n\tu32 & pc( gDlistStack.address[gDlistStackPointer] );\n\tif (pc & 3) { tab5_daedalus_fail(); return false; }')
            text = replace(text, 'while(gDlistStackPointer >= 0)',
                           'while(gDlistStackPointer >= 0 && !tab5_daedalus_failed())')
            text = replace(text, 'u32 zi_width_in_dwords = g_CI.Width >> 1;',
                           'if (g_CI.Width > 640 || y1 > 1024 || !IsAddressValid(g_CI.Address, y1 * g_CI.Width * 2, "depth clear")) return;\n\tu32 zi_width_in_dwords = g_CI.Width >> 1;')
            text = replace(text, 'x1 = std::min(std::max(x0, scissors.left), scissors.right);',
                           'x1 = std::min(std::max(x1, scissors.left), scissors.right);')
            text += '''\nextern "C" u32 tab5_daedalus_ci_address(){return g_CI.Address;}
extern "C" u32 tab5_daedalus_ci_width(){return g_CI.Width;}
extern "C" u32 tab5_daedalus_ci_size(){return g_CI.Size;}
'''
        if p.name == 'Microcode.cpp':
            text = replace(text, 'while (p+1 < e)', 'while (p+1 < e && i < data_size)')
            text = replace(text, 'static const std::unordered_map<u32, MicrocodeData> gMicrocodeDataMap = {',
                           'static const struct { u32 hash; MicrocodeData data; } gMicrocodeDataMap[] = {')
            text = re.sub(r'std::make_pair\((0x[0-9a-f]+), (MicrocodeData\{[^}]+\})\)', r'{\1, \2}', text)
            text = replace(text, 'auto it = gMicrocodeDataMap.find(code_hash);\n\tif (it != gMicrocodeDataMap.end())',
                           'for (const auto &entry : gMicrocodeDataMap) if (entry.hash == code_hash)')
            text = replace(text, 'const MicrocodeData& data = it->second;', 'const MicrocodeData& data = entry.data;')
        if p.name == 'BaseRenderer.cpp':
            text = replace(text, 'f32 fScaleX = (f32)ScaleX / 1024.0f;', 'f32 fScaleX = ScaleX ? (f32)ScaleX / 1024.0f : 1.0f;')
        p.write_text(text)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    stage(parser.parse_args().output)
