#include "stdinc.hpp"
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "command.hpp"
#include "gsc.hpp"
#include "scripting.hpp"

#include <utils/string.hpp>
#include <utils/memory.hpp>
#include <utils/hook.hpp>

namespace command
{
	std::unordered_map<std::string, std::function<void(params&)>> handlers;
	std::unordered_map<std::string, std::function<void(int, params_sv&)>> handlers_sv;

	std::vector<std::string> script_commands;
	std::vector<std::string> script_sv_commands;
	utils::memory::allocator allocator;

	utils::hook::detour client_command_hook;

	game::CmdArgs* get_cmd_args()
	{
		return reinterpret_cast<game::CmdArgs*>(game::Sys_GetValue(4));
	}

	void main_handler()
	{
		params params = {};

		const auto command = utils::string::to_lower(params[0]);

		if (handlers.find(command) != handlers.end())
		{
			handlers[command](params);
		}
	}

	void client_command_stub(const int client_num)
	{
		params_sv params = {};

		const auto command = utils::string::to_lower(params[0]);
		if (handlers_sv.find(command) != handlers_sv.end())
		{
			handlers_sv[command](client_num, params);
		}

		client_command_hook.invoke<void>(client_num);
	}

	params::params()
		: nesting_(get_cmd_args()->nesting)
	{
	}

	int params::size() const
	{
		const auto cmd_args = get_cmd_args();
		return cmd_args->argc[cmd_args->nesting];
	}

	const char* params::get(int index) const
	{
		if (index >= this->size())
		{
			return "";
		}

		const auto cmd_args = get_cmd_args();
		return cmd_args->argv[this->nesting_][index];
	}

	std::string params::join(int index) const
	{
		std::string result = {};

		for (auto i = index; i < this->size(); i++)
		{
			if (i > index)
			{
				result.append(" ");
			}

			result.append(this->get(i));
		}

		return result;
	}

	params_sv::params_sv()
		: nesting_(game::sv_cmd_args->nesting)
	{
	}

	int params_sv::size() const
	{
		return game::sv_cmd_args->argc[this->nesting_];
	}

	const char* params_sv::get(const int index) const
	{
		if (index >= this->size())
		{
			return "";
		}

		return game::sv_cmd_args->argv[this->nesting_][index];
	}

	std::string params_sv::join(const int index) const
	{
		std::string result = {};

		for (auto i = index; i < this->size(); i++)
		{
			if (i > index)
			{
				result.append(" ");
			}

			result.append(this->get(i));
		}

		return result;
	}

	void add_raw(const char* name, void (*callback)())
	{
		game::Cmd_AddCommandInternal(name, callback, utils::memory::get_allocator()->allocate<game::cmd_function_t>());
	}

	void add(const char* name, std::function<void(params&)> callback)
	{
		const auto command = utils::string::to_lower(name);

		if (handlers.find(command) == handlers.end())
		{
			add_raw(name, main_handler);
		}

		handlers[command] = callback;
	}

	void add_sv(const std::string& name, std::function<void(int, const params_sv&)> callback)
	{
		const auto command = utils::string::to_lower(name);
		if (handlers_sv.find(command) == handlers_sv.end())
		{
			handlers_sv[command] = std::move(callback);
		}
	}

	void add_script_command(const std::string& name, const std::function<void(const params&)>& callback)
	{
		script_commands.push_back(name);
		const auto name_ = allocator.duplicate_string(name);
		add(name_, callback);
	}

	void add_script_sv_command(const std::string& name, const std::function<void(int, const params_sv&)>& callback)
	{
		script_sv_commands.push_back(name);
		add_sv(name, callback);
	}

	void clear_script_commands()
	{
		for (const auto& name : script_commands)
		{
			handlers.erase(name);
			game::Cmd_RemoveCommand(name.data());
		}

		for (const auto& name : script_sv_commands)
		{
			handlers_sv.erase(name);
		}

		allocator.clear();
		script_commands.clear();
		script_sv_commands.clear();
	}

	void execute(std::string command, const bool sync)
	{
		command += "\n";

		if (sync)
		{
			game::Cmd_ExecuteSingleCommand(0, 0, command.data());
		}
		else
		{
			game::Cbuf_AddText(0, command.data());
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			scripting::on_shutdown(clear_script_commands);
			client_command_hook.create(SELECT_VALUE(0x4AF770, 0x63DB70), client_command_stub);

			gsc::function::add_multiple([](const std::string& command)
			{
				execute(command, false);
			}, "executecommand", "command::execute");

			gsc::function::add_multiple([](const std::string& name, const scripting::function& function)
			{
				command::add_script_command(name, [function](const command::params& params)
				{
					scripting::array array;

					for (auto i = 0; i < params.size(); i++)
					{
						array.push(params[i]);
					}

					function({array});
				});
			}, "addcommand", "command::add");

			gsc::function::add_multiple([](const std::string& name, const scripting::function& function)
			{
				command::add_script_sv_command(name, [function](const int client_num, const command::params_sv& params)
				{
					const scripting::entity player = game::Scr_GetEntityId(game::SCRIPTINSTANCE_SERVER, client_num, 0, 0);

					scripting::array array;

					for (auto i = 0; i < params.size(); i++)
					{
						array.push(params[i]);
					}

					function(player, {array});
				});
			}, "addclientcommand", "command::add_sv");
		}
	};
}

REGISTER_COMPONENT(command::component)