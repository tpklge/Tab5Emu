# Adapt only the pinned feeder's timing constants, without changing the
# component manager's files. The complete upstream implementation is retained.
idf_component_get_property(tab5_codec_dir espp__codec COMPONENT_DIR)
set(tab5_feed_source "${tab5_codec_dir}/include/i2s_tx_feed.hpp")
file(SHA256 "${tab5_feed_source}" tab5_feed_sha)
if(NOT tab5_feed_sha STREQUAL "5c6aedcbd54a6524b25dc4637201996f86702a155effc97335ed07a0f8487a18")
  message(FATAL_ERROR "Audio feeder changed; audit the local timing adaptation")
endif()
set(tab5_audio_compat "${CMAKE_BINARY_DIR}/tab5-audio-policy")
idf_build_get_property(tab5_audio_python PYTHON)
execute_process(COMMAND ${tab5_audio_python} "${CMAKE_SOURCE_DIR}/tools/stage_audio_policy.py"
  "${tab5_feed_source}" "${tab5_audio_compat}/i2s_tx_feed.hpp" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${tab5_feed_source}" "${CMAKE_SOURCE_DIR}/tools/stage_audio_policy.py")
idf_component_get_property(tab5_audio_lib espp__audio COMPONENT_LIB)
target_include_directories(${tab5_audio_lib} BEFORE PUBLIC "${tab5_audio_compat}")
