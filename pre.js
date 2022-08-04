
let js_callbacks = {}
Module.setupCallbacks = (callbacks = {}) => {
    Object.assign(js_callbacks, callbacks);
}