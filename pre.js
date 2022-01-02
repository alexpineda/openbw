
let js_fatal_error, js_pre_main_loop, js_post_main_loop, js_file_size, js_read_data, js_load_done;

Module.setupCallbacks = (_js_fatal_error, _js_pre_main_loop, _js_post_main_loop, _js_file_size, _js_read_data, _js_load_done) => {
    console.log("setup Callbacks");
    js_fatal_error = _js_fatal_error;
    js_pre_main_loop = _js_pre_main_loop;
    js_post_main_loop = _js_post_main_loop;
    js_file_size = _js_file_size;
    js_read_data = _js_read_data;
    js_load_done = _js_load_done;

    // ENVIRONMENT_IS_PTHREAD=true will have been preset in worker.js. Make it false in the main runtime thread.
    if (Module['ENVIRONMENT_IS_PTHREAD']) {
    console.log("WE're IN A THREAD!!")
    }
}