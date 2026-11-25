#pragma once

#include "cart.hpp"
#if defined(ENABLE_GBC)
#include "gameboy.hpp"
#include "gb_storage.hpp"
#include "gb_rom.hpp"
#include "gb_memory.hpp"
#endif

class GbcCart : public Cart {
public:
  // an emulator core: a state loads right after the game starts
  virtual bool supports_resume() const override { return true; }

  explicit GbcCart(const Cart::Config& config)
    : Cart(config) {
    handle_video_setting();
    init();
  }

  virtual ~GbcCart() override {
    deinit();
  }

  // cppcheck-suppress uselessOverride
  virtual void reset() override {
    Cart::reset();
#if defined(ENABLE_GBC)
    reset_gameboy();
#endif
  }

  // cppcheck-suppress uselessOverride
  virtual void load() override {
#if defined(ENABLE_GBC)
    if (load_gameboy_checked(get_save_path())) Cart::load();
#else
    Cart::load();
#endif
  }

  // cppcheck-suppress uselessOverride
  virtual void save() override {
#if defined(ENABLE_GBC)
    if (save_gameboy_checked(get_save_path(true))) Cart::save();
#else
    Cart::save();
#endif
  }

  void init() {
#if defined(ENABLE_GBC)
    if (const auto *error = tab5::gb::rom_error({romdata_, rom_size_bytes_})) {
      load_error_ = error;
      return;
    }
    if (!init_gameboy_checked(get_rom_filename(), romdata_, rom_size_bytes_)) {
      load_error_ = "Memoria insuficiente para abrir GB/GBC. Feche e tente novamente.";
      return;
    }
    initialized_ = true;
    if (!initialize_gameboy_storage(console_info(console_of(info_.platform)).key,info_.content_hash))
      load_error_ = "Falha ao ler o save GB/GBC. Arquivos preservados.";
#endif
  }

  void deinit() {
#if defined(ENABLE_GBC)
    if (!initialized_) return;
    if (gameboy_storage_ready()) (void)flush_gameboy_battery();
    release_gameboy_storage();
    deinit_gameboy();
    initialized_ = false;
#endif
  }

  // cppcheck-suppress uselessOverride
  virtual bool run() override {
#if defined(ENABLE_GBC)
    run_gameboy_tracked();
#endif
    return Cart::run();
  }
  bool loaded() const override {
#if defined(ENABLE_GBC)
    return initialized_ && gameboy_storage_ready();
#else
    return false;
#endif
  }
  std::string load_error() const override { return load_error_; }
  uint32_t game_frames() const override {
#if defined(ENABLE_GBC)
    return gameboy_frames();
#else
    return 0;
#endif
  }

protected:
  bool initialized_{false};
  std::string load_error_{"Falha ao abrir o jogo GB/GBC."};
  bool persist_before_exit() override {
#if defined(ENABLE_GBC)
    return flush_gameboy_battery();
#else
    return true;
#endif
  }
  // GB
  static constexpr size_t GAMEBOY_WIDTH = 160;
  static constexpr size_t GAMEBOY_HEIGHT = 144;

  // cppcheck-suppress uselessOverride
  virtual void pre_menu() override {
#if defined(ENABLE_GBC)
    pause_gameboy_clock();
#endif
    Cart::pre_menu();
#if defined(ENABLE_GBC)
    logger_.info("gbc::pre_menu()");
#endif
  }

  // cppcheck-suppress uselessOverride
  virtual void post_menu() override {
    Cart::post_menu();
#if defined(ENABLE_GBC)
    resume_gameboy_clock();
    logger_.info("gbc::post_menu()");
#endif
  }

  virtual void set_original_video_setting() override {
#if defined(ENABLE_GBC)
    display_original(GAMEBOY_WIDTH, GAMEBOY_HEIGHT);
#endif
  }

  virtual std::pair<size_t, size_t> get_video_size() const override {
    return std::make_pair(GAMEBOY_WIDTH, GAMEBOY_HEIGHT);
  }

  // cppcheck-suppress uselessOverride
  virtual std::span<uint8_t> get_video_buffer() const override {
#if defined(ENABLE_GBC)
    return get_gameboy_video_buffer();
#else
    return std::span<uint8_t>();
#endif
  }

  virtual void set_fit_video_setting() override {
#if defined(ENABLE_GBC)
    display_fit(GAMEBOY_WIDTH, GAMEBOY_HEIGHT);
#endif
  }

  virtual void set_fill_video_setting() override {
#if defined(ENABLE_GBC)
    display_fill();
#endif
  }

  virtual std::string get_save_extension() const override {
    switch (info_.platform) {
    case Emulator::GAMEBOY:
      return "_gb.sav";
    case Emulator::GAMEBOY_COLOR:
      return "_gbc.sav";
    default:
      return Cart::get_save_extension();
    }
  }
};
