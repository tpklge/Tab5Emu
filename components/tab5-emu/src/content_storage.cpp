#include "content_storage.hpp"
#include "psa/crypto.h"
#include <array>
#include <vector>

bool tab5::content::digest_bytes(const void *data,size_t size,std::array<uint8_t,32> &digest) {
  size_t written=0;
  return psa_crypto_init()==PSA_SUCCESS &&
    psa_hash_compute(PSA_ALG_SHA_256,static_cast<const uint8_t *>(data),size,
                     digest.data(),digest.size(),&written)==PSA_SUCCESS && written==digest.size();
}

std::string tab5::content::rom_sha256(const std::string &path) {
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) return {};
  psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
  bool ok = psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&operation, PSA_ALG_SHA_256) == PSA_SUCCESS;
  std::vector<uint8_t> buffer(4096);
  size_t count;
  while (ok && (count = fread(buffer.data(), 1, buffer.size(), file)) != 0)
    ok = psa_hash_update(&operation, buffer.data(), count) == PSA_SUCCESS;
  if (ferror(file)) ok = false;
  fclose(file);
  std::array<uint8_t, 32> hash;
  size_t length = 0;
  ok = ok && psa_hash_finish(&operation, hash.data(), hash.size(), &length) == PSA_SUCCESS && length == 32;
  psa_hash_abort(&operation);
  if (!ok) return {};
  std::string result;
  for (auto byte : hash) {
    result.push_back("0123456789abcdef"[byte >> 4]);
    result.push_back("0123456789abcdef"[byte & 15]);
  }
  return result;
}
