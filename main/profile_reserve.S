// The Paludo allocator rounds the canonical ESP image to 64 KiB and shrinks
// smaller images' reservations. Keep this experimental profile in the same
// app4 reservation as stable, preserving all other apps and data partitions.
// This is part of the hashed image's flash rodata, not trailing file padding
// (the Launcher's parser correctly refuses trailing data). No RAM is allocated.
.section .rodata.tab5_profile_reserve,"aR",@progbits
.balign 16
.global tab5_profile_reserve
.type tab5_profile_reserve,@object
tab5_profile_reserve:
#if defined(TAB5_SD_CORES)
#if TAB5_SD_RESERVE_BYTES > 0
  .space TAB5_SD_RESERVE_BYTES,255
#endif
#elif defined(ENABLE_N64)
  .space TAB5_N64_RESERVE_BYTES,255
#else
  .space 32768,255
#endif
.size tab5_profile_reserve,.-tab5_profile_reserve
