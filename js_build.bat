@ECHO OFF
set OPENBW_DIR=./
set BWAPI_DIR=../bwapi

set BUILD_ARGS=-std=c++14 -I %OPENBW_DIR% -I %BWAPI_DIR%/bwapi/BWAPI/Source/BW/ -I %BWAPI_DIR%/bwapi/Util/Source/ -I include %OPENBW_DIR%/ui/dlmalloc.c 
set BUILD_ARGS=%BUILD_ARGS% %OPENBW_DIR%/ui/sdl2.cpp %OPENBW_DIR%/ui/gfxtest.cpp %BWAPI_DIR%/bwapi/BWAPI/Source/BW/Bitmap.cpp 
set BUILD_ARGS=%BUILD_ARGS% -ferror-limit=4 -O3 --memory-init-file 0 -s ASM_JS=1 -s USE_SDL=2 -o test.html -s TOTAL_MEMORY=201326592
set BUILD_ARGS=%BUILD_ARGS% -s INVOKE_RUN=0 --bind  -s DISABLE_EXCEPTION_CATCHING=0 -D OPENBW_NO_SDL_IMAGE -D USE_DL_PREFIX 
set BUILD_ARGS=%BUILD_ARGS% -s ABORTING_MALLOC=0 -DMSPACES -DFOOTERS -DOPENBW_NO_SDL_MIXER=1 -s ASSERTIONS=1 
Rem Debug flags
set BUILD_ARGS=%BUILD_ARGS% -O2 -g4 -s ASSERTIONS=2 -s DEMANGLE_SUPPORT=1 -s SAFE_HEAP=1 
em++ %BUILD_ARGS% -s EXPORTED_FUNCTIONS="['_main','_ui_resize','_replay_get_value','_replay_set_value','_player_get_value','_load_replay']" -s EXTRA_EXPORTED_RUNTIME_METHODS="['callMain']"

REm MINIMAL_RUNTIME 

REM browserify -s sidegrade downgrade-replay/src/index.js -o bundle.js