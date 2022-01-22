#include "common.h"
#include "ui.h"
#include "../bwgame.h"
#include "../replay.h"

namespace bwgame
{

	struct played_sound_t
	{
		int32_t id;
		int32_t x;
		int32_t y;
		int32_t unit_type_id = -1;
	};

	struct titan_replay_functions : ui_functions
	{
		game_player player;
		std::vector<played_sound_t> played_sounds;
		std::vector<int> deleted_images;
		std::vector<int> deleted_sprites;
		std::vector<int> deleted_units;

		titan_replay_functions(game_player player) : ui_functions(std::move(player))
		{
		}

		virtual void play_sound(int id, xy position, const unit_t *source_unit, bool add_race_index) override
		{
			played_sound_t ps;
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

		virtual void on_image_destroy(image_t *image) override
		{
			deleted_images.push_back(image->index);
		}

		virtual void on_sprite_destroy(sprite_t *sprite) override
		{
			deleted_sprites.push_back(sprite->index);
		}

		virtual void on_kill_unit(unit_t *u) override
		{
			deleted_units.push_back(u->index);
		}

		void update() {}

		fp8 game_speed = fp8::integer(1);

		void reset()
		{
			deleted_images.clear();
			deleted_sprites.clear();
			deleted_units.clear();
			played_sounds.clear();

			apm = {};
			auto &game = *st.game;
			st = state();
			game = game_state();
			replay_st = replay_state();
			action_st = action_state();

			replay_frame = 0;
			st.global = &global_st;
			st.game = &game;
		}
	};

}
