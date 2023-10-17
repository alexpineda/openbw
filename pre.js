
let js_callbacks = {}
Module.setupCallbacks = (callbacks = {}) => {
    Object.assign(js_callbacks, callbacks);
}
Module.setupCallback = (key, fn) => {
    js_callbacks[key] = fn;
}