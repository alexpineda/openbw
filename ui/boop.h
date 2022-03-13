
#include "common.h"
#include "drawing.h"
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

	struct player_data_t
	{
		int minerals;
		int gas;
		int supply;
		int supply_max;
		int worker_supply;
		int army_supply;
		int apm;
	};

	struct titan_replay_functions : ui_functions
	{
		game_player player;
		std::vector<played_sound_t> played_sounds;
		std::vector<int> deleted_images;
		std::vector<int> deleted_sprites;
		std::vector<int> deleted_units;
		std::vector<int> deleted_bullets;
		std::array<player_data_t, 8> player_data;
		std::vector<uint8_t> fow;

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

		virtual void on_bullet_destroy(bullet_t *bullet) override
		{
			deleted_bullets.push_back(bullet->sprite->index);
		}

		virtual void on_sprite_destroy(sprite_t *sprite) override
		{
			deleted_sprites.push_back(sprite->index);
		}

		virtual void on_kill_unit(unit_t *u) override
		{

			deleted_units.push_back(get_unit_id(u).raw_value);
		}

		fp8 game_speed = fp8::integer(1);

		void generate_player_data()
		{
			for (int player = 0; player < 8; player++)
			{
				const int pos = player * 8;
				auto &p = player_data[player];

				double worker_supply = 0.0;
				for (const unit_t *u : ptr(st.player_units.at(player)))
				{
					if (!ut_worker(u))
						continue;
					if (!u_completed(u))
						continue;
					worker_supply += u->unit_type->supply_required.raw_value / 2.0;
				}

				double army_supply = 0.0;
				for (const unit_t *u : ptr(st.player_units.at(player)))
				{
					if (ut_worker(u))
						continue;
					if (!u_completed(u))
						continue;
					army_supply += u->unit_type->supply_required.raw_value / 2.0;
				}

				p.minerals = st.current_minerals.at(player);
				p.gas = st.current_gas.at(player);
				p.supply = 0;
				p.supply_max = 0;
				for (int r = 0; r < 3; r++) {
					p.supply += st.supply_used.at(player)[r].raw_value / 2.0;
					p.supply_max += std::min(st.supply_available.at(player)[r].raw_value / 2.0, 200.0);
				}
				p.worker_supply = worker_supply;
				p.army_supply = army_supply;
				p.apm = apm[player].current_apm;
			}
		}

		void reset()
		{
			deleted_images.clear();
			deleted_sprites.clear();
			deleted_units.clear();
			deleted_bullets.clear();
			played_sounds.clear();
			player_data.fill({});
			fow.clear();

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
