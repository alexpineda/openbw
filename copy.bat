set TITAN_PATH=C:\\users\\Game_Master\\Projects\\titan-reactor\\

copy /b web\\titan.wasm %TITAN_PATH%\\bundled
copy /b web\\titan.js %TITAN_PATH%\\src\\renderer\\openbw\\titan.wasm.js
copy /b web\\titan.worker.js %TITAN_PATH%\\bundled