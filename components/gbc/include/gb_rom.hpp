#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace tab5::gb {
// Validate the geometry used by this gnuboy loader before it reads the header
// or builds bank pointers. No logo or checksum requirement: homebrew is valid.
inline const char *rom_error(std::span<const uint8_t> rom) {
  if (rom.size() < 0x150)
    return "ROM GB/GBC truncada: cabecalho incompleto.";
  switch (rom[0x147]) {
  case 0x00: case 0x01: case 0x02: case 0x03: case 0x05: case 0x06:
  case 0x08: case 0x09: case 0x0f: case 0x10: case 0x11: case 0x12:
  case 0x13: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d:
  case 0x1e: case 0xfe: case 0xff:
    break;
  default:
    return "Mapper GB/GBC ainda nao suportado por este nucleo.";
  }
  if (rom[0x148] > 8)
    return "Tamanho de ROM GB/GBC nao suportado por este nucleo.";
  const size_t declared = size_t(32768) << rom[0x148];
  if (rom.size() < declared)
    return "ROM GB/GBC truncada: faltam bancos declarados no cabecalho.";
  if (rom[0x149] > 5)
    return "Tamanho de RAM GB/GBC invalido no cabecalho.";
  return nullptr;
}
} // namespace tab5::gb
