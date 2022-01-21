#include <memory>
#include "native_sound.h"


namespace native_sound {

	bool initialized = false;

	int frequency = 0;
	int channels = 64;


	void init() {
	}

	struct sdl_sound : sound {
		virtual ~sdl_sound() override {}
	};

	void play(int channel, sound* arg_s, int volume, int pan) {
	}

	bool is_playing(int channel) {
		return false;
	}

	void stop(int channel) {
	}

	void set_volume(int channel, int volume) {
	}

	std::unique_ptr<sound> load_wav(const void* data, size_t size) {
		return nullptr;
	}


}
