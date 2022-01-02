@ECHO OFF
set OPENBW_DIR=./
set BWAPI_DIR=../bwapi

set OPTIMIZATION=-O0
set KEEP_DEBUG_INFO=-g3
set DEBUG=%KEEP_DEBUG_INFO% -s ASSERTIONS=2 -s VERBOSE -s EXCEPTION_DEBUG 
set DEBUG=%DEBUG% -s GL_DEBUG -s SAFE_HEAP -s DEMANGLE_SUPPORT -v --profiling
set GRAPHICS=-s USE_SDL=0 -s -D OPENBW_NO_SDL_IMAGE -D OPENBW_NO_SDL_MIXER 

set BUILD_ARGS=-std=c++14 -I %OPENBW_DIR% -I include %OPENBW_DIR%/ui/dlmalloc.c
set BUILD_ARGS=%BUILD_ARGS% %OPENBW_DIR%/ui/headless.cpp 
set BUILD_ARGS=%BUILD_ARGS% -ferror-limit=4  -s ASM_JS=1 -s TOTAL_MEMORY=201326592
set BUILD_ARGS=%BUILD_ARGS% -s INVOKE_RUN=0 --bind -D USE_DL_PREFIX -s ABORTING_MALLOC=0 -DMSPACES -DFOOTERS
set BUILD_ARGS=%BUILD_ARGS% -s MODULARIZE -s EXPORT_NAME=createOpenBW -s EXPORT_ES6 --pre-js pre.js
set BUILD_ARGS=%BUILD_ARGS% %OPTIMIZATION% %GRAPHICS% %DEBUG% -s ENVIRONMENT=web -o web/titan.js
set BUILD_ARGS=%BUILD_ARGS% -s EXPORTED_FUNCTIONS="['_main','_replay_get_value','_replay_set_value','_player_get_value','_load_replay']"
em++ %BUILD_ARGS% -s EXPORTED_RUNTIME_METHODS="['callMain', 'ALLOC_NORMAL', 'allocate']"

REm MINIMAL_RUNTIME 

REM browserify -s sidegrade downgrade-replay/src/index.js -o bundle.js