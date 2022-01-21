
#include <memory>
#include "native_window.h"

namespace native_window {

	struct window_impl {
		void destroy(){}
		bool create(const char* title, int x, int y, int width, int height){return false;}
		void get_cursor_pos(int* x, int* y){}
		bool peek_message(event_t& e){return false;}
		bool show_cursor(bool show){return false;}
		bool get_key_state(int scancode){return false;}
		bool get_mouse_button_state(int button){return false;}
		void update_surface(){}
		explicit operator bool() const{return false;}
	};
    
    window::window() {
        impl = std::make_unique<window_impl>();
    }

    window::~window() {
    }

    window::window(window&& n) {
        impl = std::move(n.impl);
    }

    void window::destroy() {
        impl->destroy();
    }

    bool window::create(const char* title, int x, int y, int width, int height) {
        return impl->create(title, x, y, width, height);
    }

    void window::get_cursor_pos(int* x, int* y) {
        return impl->get_cursor_pos(x, y);
    }

    bool window::peek_message(event_t& e) {
        return impl->peek_message(e);
    }

    bool window::show_cursor(bool show) {
        return impl->show_cursor(show);
    }

    bool window::get_key_state(int scancode) {
        return impl->get_key_state(scancode);
    }

    bool window::get_mouse_button_state(int button) {
        return impl->get_mouse_button_state(button);
    }

    void window::update_surface() {
        return impl->update_surface();
    }

    window::operator bool() const {
        return (bool)*impl;
    }
}