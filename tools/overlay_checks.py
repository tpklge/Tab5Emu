"""Classification shared by the overlay verifier and its regression test."""
import struct


def relocated_code_references(data, symbol_values, low, high):
    """ELF32 RELA address-bearing relocations, excluding constants/relax markers."""
    address_types = {1, 2, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 44, 45, 46, 57}
    result = []
    for offset, info, addend in struct.iter_unpack('<IIi', data):
        if info & 255 not in address_types:
            continue
        target = symbol_values[info >> 8] + addend
        if low <= target < high:
            result.append((offset, target))
    return result


def resident_data_references(section_name, data, target_starts):
    # .tab5_res combines executable code and rodata in a writable ELF output
    # section. Its text is verified by the instruction pass. Arbitrary groups
    # of four instruction bytes (possibly crossing RVC boundaries) are not
    # pointers; rodata/vtables in that output section must still be checked.
    if section_name == ".tab5_res.text" or section_name.startswith(".tab5_res.text."):
        return []
    return [(offset, word) for offset in range(0, len(data) - 3, 4)
            if (word := struct.unpack_from("<I", data, offset)[0]) in target_starts]
