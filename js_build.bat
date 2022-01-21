@ECHO OFF
set OPENBW_DIR=./

set OPTIMIZATION=-O0
set PTHREAD=-pthread -s PROXY_TO_PTHREAD
set DEBUG=-g3 -s ASSERTIONS=2 -s EXCEPTION_DEBUG -s DISABLE_EXCEPTION_CATCHING=2
set DEBUG=%DEBUG% -s GL_DEBUG -s DEMANGLE_SUPPORT -v --profiling
set NO_GRAPHICS=-s USE_SDL=0 -D TITAN_HEADLESS

set BUILD_ARGS=-std=c++14 -I %OPENBW_DIR% -I include %OPENBW_DIR%/ui/dlmalloc.c
set BUILD_ARGS=%BUILD_ARGS%  %OPENBW_DIR%/ui/headless.cpp %OPENBW_DIR%/ui/no_sound.cpp %OPENBW_DIR%/ui/no_window.cpp
set BUILD_ARGS=%BUILD_ARGS% -ferror-limit=4  -s ASM_JS=1 -s INITIAL_MEMORY=201326592
set BUILD_ARGS=%BUILD_ARGS% -s INVOKE_RUN=0 --bind -D USE_DL_PREFIX -s ABORTING_MALLOC=0 -DMSPACES -DFOOTERS
set BUILD_ARGS=%BUILD_ARGS% -s MODULARIZE -s EXPORT_NAME=createOpenBW -s EXPORT_ES6 --pre-js pre.js
@REM set BUILD_ARGS=%BUILD_ARGS% %PTHREAD% 
set BUILD_ARGS=%BUILD_ARGS% %OPTIMIZATION% %NO_GRAPHICS%
set BUILD_ARGS=%BUILD_ARGS% %DEBUG% -s ENVIRONMENT=web,node -o web/titan.js
set BUILD_ARGS=%BUILD_ARGS% -s EXPORTED_FUNCTIONS="['_main','_replay_get_value','_replay_set_value','_counts','_load_replay', '_next_frame', '_next_frame_exact', '_get_buffer']"
em++ %BUILD_ARGS% -s EXPORTED_RUNTIME_METHODS="['callMain', 'ALLOC_NORMAL', 'allocate', 'UTF8ToString']"

REm MINIMAL_RUNTIME 

REM browserify -s sidegrade downgrade-replay/src/index.js -o bundle.js