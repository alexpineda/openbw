@ECHO OFF
set OPENBW_DIR=./

set SLOW=-O0 -s DISABLE_EXCEPTION_CATCHING=2 -g3 -s ASSERTIONS=1 -s EXCEPTION_DEBUG 
set SLOW=%SLOW% -s GL_DEBUG -s DEMANGLE_SUPPORT -v
set FAST=-O3 -s DISABLE_EXCEPTION_CATCHING=0 -g0 -s ASSERTIONS=0 --closure 0
set OPTIMIZATION=%FAST%

set PTHREAD=-pthread -s PROXY_TO_PTHREAD -s PTHREAD_POOL_SIZE=4 -s ALLOW_BLOCKING_ON_MAIN_THREAD=0
set OFFSCREEN=OFFSCREENCANVAS_SUPPORT=1 -s OFFSCREENCANVASES_TO_PTHREAD="#canvas"

set HEADLESS=-s USE_SDL=0 -D TITAN_HEADLESS
set USE_WEBGL=-s MAX_WEBGL_VERSION=2 -s MIN_WEBGL_VERSION=2 -s USE_SDL=2

set BUILD_ARGS=-std=c++14 -I %OPENBW_DIR% -I include %OPENBW_DIR%/ui/dlmalloc.c
set BUILD_ARGS=%BUILD_ARGS%  %OPENBW_DIR%/ui/openbw.cpp %OPENBW_DIR%/ui/no_sound.cpp %OPENBW_DIR%/ui/no_window.cpp
set BUILD_ARGS=%BUILD_ARGS% -ferror-limit=4  -s ASM_JS=1 -s INITIAL_MEMORY=402653184
set BUILD_ARGS=%BUILD_ARGS% -s INVOKE_RUN=0 --bind -D USE_DL_PREFIX -s ABORTING_MALLOC=0 -DMSPACES -DFOOTERS
set BUILD_ARGS=%BUILD_ARGS% -s MODULARIZE -s EXPORT_NAME=createOpenBW -s EXPORT_ES6 -s USE_ES6_IMPORT_META=0 
@REM set BUILD_ARGS=%BUILD_ARGS% %PTHREAD% 
set BUILD_ARGS=%BUILD_ARGS% %OPTIMIZATION% %HEADLESS%
set BUILD_ARGS=%BUILD_ARGS% -s ENVIRONMENT=web,node -o web/titan.js --pre-js pre.js
set BUILD_ARGS=%BUILD_ARGS% -s EXPORTED_FUNCTIONS="['_main','_replay_get_value','_replay_set_value','_counts','_load_replay', '_next_frame', '_get_buffer', '_get_fow_ptr', '_load_map', '_create_unit', '_next_no_replay', '_load_replay_with_height_map']"
em++ %BUILD_ARGS% -s EXPORTED_RUNTIME_METHODS="['callMain', 'ALLOC_NORMAL', 'allocate', 'UTF8ToString']"

REm MINIMAL_RUNTIME 

REM browserify -s sidegrade downgrade-replay/src/index.js -o bundle.js