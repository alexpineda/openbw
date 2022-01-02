#include "common.h"
#include "../bwgame.h"
#include "../replay.h"

namespace bwgame
{

	struct apm_t
	{
		a_deque<int> history;
		int current_apm = 0;
		int last_frame_div = 0;
		static const int resolution = 1;
		void add_action(int frame)
		{
			if (!history.empty() && frame / resolution == last_frame_div)
			{
				++history.back();
			}
			else
			{
				if (history.size() >= 10 * 1000 / 42 / resolution)
					history.pop_front();
				history.push_back(1);
				last_frame_div = frame / 12;
			}
		}
		void update(int frame)
		{
			if (history.empty() || frame / resolution != last_frame_div)
			{
				if (history.size() >= 10 * 1000 / 42 / resolution)
					history.pop_front();
				history.push_back(0);
				last_frame_div = frame / resolution;
			}
			if (frame % resolution)
				return;
			if (history.size() == 0)
			{
				current_apm = 0;
				return;
			}
			int sum = 0;
			for (auto &v : history)
				sum += v;
			current_apm = (int)(sum * ((int64_t)256 * 60 * 1000 / 42 / resolution) / history.size() / 256);
		}
	};

	struct played_sound
	{
		int id;
		int x;
		int y;
		int unit_type_id = -1;
	};

	struct titan_replay_functions : replay_functions
	{
		game_player player;
		replay_state current_replay_state;
		action_state current_action_state;
		std::array<apm_t, 12> apm;
		std::vector<played_sound> played_sounds;

		titan_replay_functions(game_player player) : replay_functions(player.st(), current_action_state, current_replay_state), player(std::move(player))
		{
		}

		virtual void play_sound(int id, xy position, const unit_t *source_unit, bool add_race_index) override
		{
			played_sound ps = played_sound();
			ps.id = add_race_index ? id + 1 : id;
			ps.x = position.x;
			ps.y = position.y;

			if (source_unit != nullptr)
			{
				const unit_type_t *unit_type = source_unit->unit_type;
				ps.unit_type_id = (int)unit_type->id;
			}
			played_sounds.push_back(ps);
		}

		virtual void on_action(int owner, int action) override
		{
			apm.at(owner).add_action(st.current_frame);
		}

		fp8 game_speed = fp8::integer(1);

		std::chrono::high_resolution_clock clock;
		std::chrono::high_resolution_clock::time_point last_draw;
		std::chrono::high_resolution_clock::time_point last_input_poll;
		std::chrono::high_resolution_clock::time_point last_fps;
		int fps_counter = 0;
		int replay_frame = 0;
		bool is_paused = false;

		template <typename cb_F>
		void async_read_file(a_string filename, cb_F cb)
		{
			auto uptr = std::make_unique<cb_F>(std::move(cb));
			auto f = [](void *ptr, uint8_t *data, size_t size)
			{
				cb_F *cb_p = (cb_F *)ptr;
				std::unique_ptr<cb_F> uptr(cb_p);
				(*cb_p)(data, size);
			};
			auto *a = filename.c_str();
			auto *b = (void (*)(void *, uint8_t *, size_t))f;
			auto *c = uptr.release();
			EM_ASM_({ js_download_file($0, $1, $2); }, a, b, c);
		}

		void reset()
		{
			apm = {};
			auto &game = *st.game;
			st = state();
			game = game_state();
			replay_st = replay_state();
			action_st = action_state();

			int replay_frame = 0;
			st.global = &global_st;
			st.game = &game;
		}
	};

}
