
let js_callbacks = {}
// let _scriptDir  = "./titan.js";
Module.setupCallbacks = (callbacks = {}) => {
    console.log("setup Callbacks");
    Object.assign(js_callbacks, callbacks);

    // js_fatal_error,
    //     js_pre_main_loop,
    //     js_post_main_loop,
    //     js_file_size,
    //     js_read_data,
    //     js_load_done,
    //     js_file_index
}