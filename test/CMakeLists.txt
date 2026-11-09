cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

enable_testing()
include(GoogleTest)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(personal_update)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(url_utils)
add_subdirectory(opds_feed_error)
add_subdirectory(html_to_plain_text)
add_subdirectory(library_text)
add_subdirectory(library_format)
add_subdirectory(library_index_file)
add_subdirectory(library_builder)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(credential_integrity)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(koreader_xpath_resolver)
add_subdirectory(progress_comparison)
add_subdirectory(font_cache_manager)
add_subdirectory(css_parser)
add_subdirectory(chapter_html_slim_parser)
add_subdirectory(korean_line_breaking)
add_subdirectory(em_dash_line_breaking)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)
add_subdirectory(content_opf_parser)
add_subdirectory(chapter_position)
add_subdirectory(book_metadata_cache)
add_subdirectory(home_button)
add_subdirectory(button_press_navigation)

add_subdirectory(inflate_stream)
add_subdirectory(inflate_reader)
add_subdirectory(zip_file)
add_subdirectory(fs_helpers)
add_subdirectory(http_header)
add_subdirectory(txt_to_html)

add_subdirectory(absolute_grayscale)

add_subdirectory(gif_support)

add_subdirectory(bmp_to_bmp)

add_subdirectory(sd_card_font)
add_subdirectory(sd_font_registry)
add_subdirectory(clipping_preview)
add_subdirectory(glyph_bitmap)

add_subdirectory(clipping_store)
add_subdirectory(reader_session)
add_subdirectory(plugin_catalog_paging)
add_subdirectory(plugin_manifest_classification)
add_subdirectory(plugin_picker_layout)
add_subdirectory(protected_paths)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME FileTransferShortcutDispatch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/transfer_shortcut/test_dispatch.py)
add_test(NAME FileBrowserMenuDispatch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/file_browser_menu/test_dispatch.py)
add_test(NAME RenderModeSectionHeader COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/chapter_html_slim_parser/test_render_mode_cache.py)
add_test(NAME X4WorkflowPolicy COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/x4_workflows/test_workflows.py)
add_test(NAME ReaderBarsMenuHandoff COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bars_menu/test_handoff.py)
add_test(NAME DeviceDiagnosticsActivity COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/device_diagnostics/test_activity.py)
add_test(NAME StatsReaderMenu COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stats_menu/test_menu.py)
add_test(NAME UiScaleBootFonts COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_scale_boot.py)
add_test(NAME LyraHomeUiScale COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_home_ui_scale.py)
add_test(NAME ClassicHomeUiScale COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_classic_home_scale.py)
add_test(NAME MinimalHomeRender COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_minimal_home.py)
add_test(NAME DashboardHomeRender COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_dashboard_home.py)
add_test(NAME BookProgressValidation COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_progress_validation.py)
add_test(NAME HomeThumbnailFailure COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_home_thumbnail_failure.py)
add_test(NAME HomeReaderShortcuts COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_home_reader_shortcuts.py)
add_test(NAME HomeActionPicker COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/home_picker/test_handoff.py)
add_test(NAME ReaderBarSlotPicker COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/section_time/test_picker.py)
add_test(NAME ReaderSectionTimeGate COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/section_time/test_render_gate.py)
add_test(NAME ReaderSectionTimeBuildRefresh COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/section_time/test_build_refresh.py)
add_test(NAME EndMenuHomeGesture COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_end_menu_home_gesture.py)
add_test(NAME StatsFrequencyRows COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_stats_frequency_rows.py)
add_test(NAME TimeLeftProgress COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_timeleft_progress.py)
add_test(NAME ReadingStatsSleepLayout COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reading_stats_sleep/test_render.py)
add_test(NAME ReadingStatsSleepFlow COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reading_stats_sleep/test_flow.py)
add_test(NAME translation_spacing COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_translation_spacing.py)
add_test(NAME TrustedTimeTimezone COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_trusted_time.py)
add_test(NAME RemoteButtonInputIntegration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_remote_button_input.py)
add_test(NAME confirmation_translations COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_confirmation_translations.py)
add_test(NAME BleLearning COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ble_learning.py)
add_subdirectory(ble_link_note_title)
add_subdirectory(ble_chapter_build_gate)
add_subdirectory(ble_remote_turn_gate)

add_subdirectory(${REPO_ROOT}/lib/BlePageTurner/test BlePageTurnerTests)

add_test(NAME selection_buttons COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_selection_buttons.py)
add_test(NAME list_viewport COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_list_viewport.py)
add_test(NAME clipping_highlight_planes COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clipping_highlight_planes.py)
add_test(NAME clipping_selection_text COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clipping_selection_text.py)

add_test(NAME recursive_dictionary COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_recursive_dictionary.py)

add_subdirectory(reader_personalization)
FetchContent_GetProperties(ArduinoJson)
add_test(NAME EmbeddedEdgeMigration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/embedded_edge/test_settings_flow.py ${arduinojson_SOURCE_DIR}/src)
add_subdirectory(book_reading_stats_atomic)

add_test(NAME ReadingStatsOverlay COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_reading_stats_overlay.py)

add_test(NAME ReaderEndShortcuts COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_reader_end_shortcuts.py)

add_subdirectory(reader_gestures)

add_test(NAME ReaderLightGestures COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_reader_light_gestures.py)

add_test(NAME MappedTouchAdapter COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_mapped_touch_adapter.py)

add_test(NAME ReaderGestureRouting COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_reader_gesture_routing.py)

add_subdirectory(quick_actions)
add_test(NAME QuickActionDispatch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_quick_action_dispatch.py)

add_subdirectory(quick_lock)

add_subdirectory(book_reader_settings)
add_subdirectory(settings_recovery_plan)

add_test(NAME BookFontFallback COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_font_fallback.py)
add_test(NAME LibraryFilters COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/library_filters/test_library_filters.py)
add_test(NAME FileBrowserViewPreferences COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/library_filters/test_browser_rows.py)
add_test(NAME LibraryEpubMetadataFallback COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/library_metadata/test_epub_metadata.py)
add_test(NAME LibraryRecentCoverGrid COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/recent_grid/test_grid.py)
add_test(NAME LibraryBookStatistics COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_library_book_stats.py)
add_test(NAME LibraryShelfOptions COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/library_shelf_options/test_options.py)
add_test(NAME LibraryShelfTransitions COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/library_shelf_options/test_transitions.py)
add_test(NAME CrossInkShortcutPreview COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shortcut_migration/test_preview.py)
add_test(NAME CrossInkBookSettingsPreview COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/book_settings_migration/test_preview.py)
add_test(NAME CrossInkBookSettingsDecoder COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/book_settings_migration/test_firmware_decoder.py)

add_test(NAME ProtectedThumbnail COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_protected_thumbnail.py)


add_test(NAME LibraryTabLock COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_library_tab_lock.py)

add_subdirectory(reading_insights)

add_subdirectory(saved_items)
add_subdirectory(reading_charts)
add_test(NAME MetadataCheckedLookup COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_metadata_lookup.py)
if(EXISTS "${REPO_ROOT}/.pio/libdeps/x4pro/SdFat/src/FatLib/FatFileLFN.cpp")
  add_test(NAME SdFatLfnLookup COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_sdfat_lfn_lookup.py)
  add_test(NAME SdFatExFatLookup COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_sdfat_exfat_lookup.py)
  add_test(NAME SdFatWrapperLookup COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_sdfat_wrapper_lookup.py)
  add_test(NAME StatsSafeReplaceReal COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_stats_safe_replace_real.py)
  add_test(NAME StatsSafeReplaceRealGlobal COMMAND ${CMAKE_COMMAND} -E env STATS_TEST_PAYLOAD=159
           ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_stats_safe_replace_real.py)
  add_test(NAME StatsBackupDirectFat COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stats_backup_real/test_direct_backup.py fat)
  add_test(NAME StatsBackupDirectExFat COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stats_backup_real/test_direct_backup.py exfat)
else()
  message(STATUS "SdFatLfnLookup requires the X4 Pro SdFat dependency and is not registered")
endif()

add_test(NAME SavedItemsShortcutDispatch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/saved_shortcut/test_dispatch.py)

add_test(NAME LibraryMenuDispatch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/library_menu/test_dispatch.py)
