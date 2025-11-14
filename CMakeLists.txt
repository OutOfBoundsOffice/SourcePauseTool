cmake_minimum_required(VERSION 3.10)

project(SPT)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_compile_options("/MP")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/build")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

FILE(GLOB_RECURSE SPT_SOURCES
	spt/cvars.cpp
	spt/feature.cpp
	spt/sptlib-wrapper.cpp
	spt/spt-serverplugin.cpp
	spt/spt-version.cpp
	
	spt/aim/aimstuff.cpp
	
	spt/features/afterframes.cpp
	spt/features/afterticks.cpp
	spt/features/aim.cpp
	spt/features/autocomplete.cpp
	spt/features/autojump.cpp
	spt/features/boog.cpp
	spt/features/camera.cpp
	spt/features/collision_group.cpp
	spt/features/con_notify.cpp
	spt/features/create_collide.cpp
	spt/features/cvar.cpp
	spt/features/demo.cpp
	spt/features/dmomm.cpp
	spt/features/ent_props.cpp
	spt/features/fov.cpp
	spt/features/generic.cpp
	spt/features/grass.cpp
	spt/features/hops_hud.cpp
	spt/features/hud.cpp
	spt/features/ihud.cpp
	spt/features/ipc-spt.cpp
	spt/features/isg.cpp
	spt/features/leafvis.cpp
	spt/features/movement_vars.cpp
	spt/features/overlay.cpp
	spt/features/pause.cpp
	spt/features/playerio.cpp
	spt/features/portalled_pause.cpp
	spt/features/property_getter.cpp
	spt/features/qccmd.cpp
	spt/features/restart.cpp
	spt/features/saveloads.cpp
	spt/features/set_sg.cpp
	spt/features/shadow.cpp
	spt/features/strafehud.cpp
	spt/features/stucksave.cpp
	spt/features/tas.cpp
	spt/features/taslogging.cpp
	spt/features/taspause.cpp
	spt/features/test.cpp
	spt/features/tickrate.cpp
	spt/features/timer.cpp
	spt/features/tracing.cpp
	spt/features/updater.cpp
	spt/features/vag_searcher.cpp
	spt/features/game_fixes/bms_flashlight_fix.cpp
	spt/features/game_fixes/caption_fix.cpp
	spt/features/game_fixes/fastload.cpp
	spt/features/game_fixes/free_oob.cpp
	spt/features/game_fixes/hardlock.cpp
	spt/features/game_fixes/multi_instance.cpp
	spt/features/game_fixes/noclip_fixes.cpp
	spt/features/game_fixes/nosleep.cpp
	spt/features/game_fixes/portal_nogroundsnap.cpp
	spt/features/game_fixes/rng.cpp
	spt/features/game_fixes/snapshot_overflow.cpp
	spt/features/game_fixes/vag_crash.cpp
	spt/features/game_fixes/visual_fixes.cpp
	spt/features/visualizations/draw_ent_collides.cpp
	spt/features/visualizations/draw_line.cpp
	spt/features/visualizations/draw_seams.cpp
	spt/features/visualizations/draw_world_collides.cpp
	spt/features/visualizations/gui_game_detection.cpp
	spt/features/visualizations/map_overlay.cpp
	spt/features/visualizations/mesh_test.cpp
	spt/features/visualizations/oob_ents.cpp
	spt/features/visualizations/portal_placement.cpp
	spt/features/visualizations/sg-collide-vis.cpp
	spt/features/visualizations/vag_trace.cpp
	spt/features/visualizations/fcps/fcps_animate.cpp
	spt/features/visualizations/fcps/fcps_event_queue.cpp
	spt/features/visualizations/fcps/fcps_feature.cpp
	spt/features/visualizations/fcps/fcps_import_export.cpp
	spt/features/visualizations/fcps/fcps_record.cpp
	spt/features/visualizations/imgui/imgui_interface.cpp
	spt/features/visualizations/imgui/spt_imgui_widgets.cpp
	spt/features/visualizations/player_trace/tr_collect.cpp
	spt/features/visualizations/player_trace/tr_entity_cache.cpp
	spt/features/visualizations/player_trace/tr_feature.cpp
	spt/features/visualizations/player_trace/tr_imgui.cpp
	spt/features/visualizations/player_trace/tr_record_cache.cpp
	spt/features/visualizations/player_trace/tr_render_cache.cpp
	spt/features/visualizations/player_trace/tr_state_access.cpp
	spt/features/visualizations/player_trace/import_export/tr_binary_read.cpp
	spt/features/visualizations/player_trace/import_export/tr_binary_read_upgrade.cpp
	spt/features/visualizations/player_trace/import_export/tr_binary_write.cpp
	spt/features/visualizations/renderer/internal/materials_manager.cpp
	spt/features/visualizations/renderer/internal/mesh_builder.cpp
	spt/features/visualizations/renderer/internal/mesh_construction.cpp
	spt/features/visualizations/renderer/internal/mesh_renderer.cpp
	spt/features/visualizations/renderer/internal/static_mesh.cpp
	
	spt/ipc/ipc.cpp
	
	spt/scripts/condition.cpp
	spt/scripts/framebulk_handler.cpp
	spt/scripts/parsed_script.cpp
	spt/scripts/srctas_reader.cpp
	spt/scripts/test_item.cpp
	spt/scripts/tester.cpp
	spt/scripts/tracker.cpp
	spt/scripts/variable_container.cpp
	
	spt/strafe/strafestuff.cpp
	
	spt/utils/convar.cpp
	spt/utils/datamap_wrapper.cpp
	spt/utils/ent_list_client.cpp
	spt/utils/ent_list_server.cpp
	spt/utils/ent_utils.cpp
	spt/utils/file.cpp
	spt/utils/game_detection.cpp
	spt/utils/map_utils.cpp
	spt/utils/math.cpp
	spt/utils/mesh_utils.cpp
	spt/utils/portal_utils.cpp
	spt/utils/serialize.cpp
	spt/utils/signals.cpp
	spt/utils/string_utils.cpp

	spt/vgui/vgui_utils.cpp
	
	# The imgui stuff has some branch dependent code
	thirdparty/imgui/imgui.cpp
	thirdparty/imgui/imgui_demo.cpp
	thirdparty/imgui/imgui_draw.cpp
	thirdparty/imgui/imgui_impl_dx9.cpp
	thirdparty/imgui/imgui_impl_win32.cpp
	thirdparty/imgui/imgui_tables.cpp
	thirdparty/imgui/imgui_widgets.cpp
)

add_library(spt_generic STATIC 	
	thirdparty/md5.cpp
	thirdparty/x86.c
)
target_include_directories(spt_generic PUBLIC thirdparty)
target_compile_options(spt_generic PUBLIC /W4 /wd4471)
target_compile_definitions(spt_generic PUBLIC sptONLY _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
target_link_libraries(spt_generic PUBLIC curl Dbghelp legacy_stdio_definitions mathlib Normaliz SPTLib xz ws2_32)

FILE(GLOB_RECURSE SPTLIB_SOURCES
	SPTLib/Hooks.cpp
	SPTLib/IHookableDirFilter.cpp
	SPTLib/IHookableModule.cpp
	SPTLib/IHookableNameFilter.cpp
	SPTLib/IHookableNameFilterOrdered.cpp
	SPTLib/MemUtils.cpp
	SPTLib/sptlib.cpp
	SPTLib/Windows/DetoursUtils.cpp
	SPTLib/Windows/Hooks_win.cpp
	SPTLib/Windows/MemUtils_win.cpp
	SPTLib/Windows/minhook/src/buffer.c
	SPTLib/Windows/minhook/src/hook.c
	SPTLib/Windows/minhook/src/trampoline.c
	SPTLib/Windows/minhook/src/hde/hde32.c
)

add_library(SPTLib STATIC ${SPTLIB_SOURCES})
target_include_directories(SPTLib PUBLIC SPTLib SPTLib/Windows/minhook/include)

add_library(mathlib STATIC IMPORTED)
set_target_properties(mathlib PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/SDK/orangebox_new/lib/mathlib.lib")
	
add_library(xz STATIC IMPORTED)
set_target_properties(xz PROPERTIES
	IMPORTED_LOCATION   "${CMAKE_SOURCE_DIR}/thirdparty/xz/lib/lzma_dbg.lib"
	IMPORTED_LOCATION_RELEASE "${CMAKE_SOURCE_DIR}/thirdparty/xz/lib/lzma_rel.lib"
)

add_library(curl STATIC IMPORTED)
set_target_properties(curl PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/curl/lib/libcurl_a.lib")
target_link_libraries(curl INTERFACE Advapi32 Crypt32 Wldap32)
target_include_directories(curl INTERFACE thirdparty/curl/include)

add_library(sdk_orangebox STATIC SDK/orangebox_new/src/all_orangebox.cpp)
target_include_directories(sdk_orangebox PUBLIC 
	SDK/orangebox_new/include/common
	SDK/orangebox_new/include/game
	SDK/orangebox_new/include/public
	SDK/orangebox_new/include/public/tier0
	SDK/orangebox_new/include/public/tier1
	SDK/orangebox_new/include/game/server
	SDK/orangebox_new/include/game/client
	SDK/orangebox_new/include/game/shared)
target_compile_definitions(sdk_orangebox PUBLIC SSDK2007 TARGET_NAME="spt.dll")

add_library(spt SHARED ${SPT_SOURCES})
target_precompile_headers(spt PRIVATE spt/utils/stdafx.hpp)
target_include_directories(spt PUBLIC spt . spt/utils)
target_link_libraries(spt PUBLIC sdk_orangebox spt_generic)

add_library(sdk_2013 STATIC SDK/sdk2013_new/src/all_2013.cpp)
target_include_directories(sdk_2013 PUBLIC 
	SDK/sdk2013_new/include/common
	SDK/sdk2013_new/include/public
	SDK/sdk2013_new/include/public/tier0
	SDK/sdk2013_new/include/public/tier1
	SDK/sdk2013_new/include/game/server
	SDK/sdk2013_new/include/game/client
	SDK/sdk2013_new/include/game/shared)
target_compile_definitions(sdk_2013 PUBLIC SSDK2013 TARGET_NAME="spt-2013.dll")

add_library(spt_2013 SHARED ${SPT_SOURCES})
target_precompile_headers(spt_2013 PRIVATE spt/utils/stdafx.hpp)
set_target_properties(spt_2013 PROPERTIES OUTPUT_NAME "spt-2013")
target_include_directories(spt_2013 PUBLIC spt . spt/utils)
target_link_libraries(spt_2013 PUBLIC sdk_2013 spt_generic)

add_library(sdk_oe STATIC SDK/episode1_new/src/all_oe.cpp)
target_include_directories(sdk_oe PUBLIC 
	SDK/episode1_new/include/common
	SDK/episode1_new/include/public
	SDK/episode1_new/include/public/tier0
	SDK/episode1_new/include/public/tier1
	SDK/episode1_new/include/game/server
	SDK/episode1_new/include/game/client
	SDK/episode1_new/include/game/shared)
target_compile_definitions(sdk_oe PUBLIC OE TARGET_NAME="spt-oe.dll")

add_library(spt_oe SHARED ${SPT_SOURCES})
target_precompile_headers(spt_oe PRIVATE spt/utils/stdafx.hpp)
set_target_properties(spt_oe PROPERTIES OUTPUT_NAME "spt-oe")
target_include_directories(spt_oe PUBLIC spt . spt/utils)
target_link_libraries(spt_oe PUBLIC sdk_oe spt_generic)

add_library(sdk_bms STATIC SDK/bms_new/src/all_bms.cpp)
target_include_directories(sdk_bms PUBLIC 
	SDK/bms_new/include/common
	SDK/bms_new/include/public
	SDK/bms_new/include/public/tier0
	SDK/bms_new/include/public/tier1
	SDK/bms_new/include/game/server
	SDK/bms_new/include/game/client
	SDK/bms_new/include/game/shared)
target_compile_definitions(sdk_bms PUBLIC BMS TARGET_NAME="spt-bms.dll")

add_library(spt_bms SHARED ${SPT_SOURCES})
target_precompile_headers(spt_bms PRIVATE spt/utils/stdafx.hpp)
set_target_properties(spt_bms PROPERTIES OUTPUT_NAME "spt-bms")
target_include_directories(spt_bms PUBLIC spt . spt/utils)
target_link_libraries(spt_bms PUBLIC sdk_bms spt_generic)
