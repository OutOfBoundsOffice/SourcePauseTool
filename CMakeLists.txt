cmake_minimum_required(VERSION 3.10)

project(SPT LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_compile_options(/MP)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)

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
	IMPORTED_LOCATION_DEBUG   "${CMAKE_SOURCE_DIR}/thirdparty/xz/lib/lzma_dbg.lib"
	IMPORTED_LOCATION_RELEASE "${CMAKE_SOURCE_DIR}/thirdparty/xz/lib/lzma_rel.lib"
	IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/thirdparty/xz/lib/lzma_rel.lib"
)

add_library(curl STATIC IMPORTED)
set_target_properties(curl PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/curl/lib/libcurl_a.lib")
target_link_libraries(curl INTERFACE Advapi32 Crypt32 Wldap32)
target_include_directories(curl INTERFACE thirdparty/curl/include)

set(SPT_SOURCES

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
	
	thirdparty/imgui/imgui.cpp
	thirdparty/imgui/imgui_demo.cpp
	thirdparty/imgui/imgui_draw.cpp
	thirdparty/imgui/imgui_impl_dx9.cpp
	thirdparty/imgui/imgui_impl_win32.cpp
	thirdparty/imgui/imgui_tables.cpp
	thirdparty/imgui/imgui_widgets.cpp

	thirdparty/md5.cpp
	thirdparty/x86.c
)

function(add_engine_branch dllName sdkDir engineBranchDefines)
	# SDK lib
	set(sdk_target ${dllName}_sdk)
	add_library(${sdk_target} STATIC "${sdkDir}/src/all.cpp")
	target_compile_definitions(${sdk_target} PRIVATE ${engineBranchDefines})
	target_include_directories(${sdk_target} PUBLIC
		${sdkDir}/include/common
		${sdkDir}/include/public
		${sdkDir}/include/public/tier0
		${sdkDir}/include/public/tier1
		${sdkDir}/include/game/server
		${sdkDir}/include/game/client
		${sdkDir}/include/game/shared
	)
	target_compile_options(${sdk_target} PRIVATE /W3 /wd5033 /wd4273)

	# main DLL
	add_library(${dllName} SHARED ${SPT_SOURCES})

	target_compile_definitions(${dllName} PRIVATE
		${engineBranchDefines}
		TARGET_NAME="${dllname}.dll"
		_CRT_SECURE_NO_DEPRECATE
		_CRT_NONSTDC_NO_DEPRECATE
	)
	target_include_directories(${dllName} PUBLIC
		${CMAKE_SOURCE_DIR}
		${CMAKE_SOURCE_DIR}/spt
		${CMAKE_SOURCE_DIR}/spt/utils
		${CMAKE_SOURCE_DIR}/thirdparty
	)
	target_compile_options(${dllName} PRIVATE /W4 /wd4471)
	target_link_libraries(${dllName} PUBLIC
		curl
		Dbghelp
		legacy_stdio_definitions
		mathlib
		Normaliz
		SPTLib
		xz
		ws2_32
		${sdk_target}
	)
endfunction()

add_engine_branch(spt "${CMAKE_SOURCE_DIR}/SDK/orangebox_new" "SSDK2007")
add_engine_branch(spt-2013 "${CMAKE_SOURCE_DIR}/SDK/sdk2013_new" "SSDK2013")
add_engine_branch(spt-bms "${CMAKE_SOURCE_DIR}/SDK/bms_new" "BMS")
add_engine_branch(spt-oe "${CMAKE_SOURCE_DIR}/SDK/episode1_new" "OE")
