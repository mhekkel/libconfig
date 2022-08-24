/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Maarten L. Hekkelman
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <cassert>
#include <cstring>

#include <any>
#include <charconv>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <cfg/text.hpp>
#include <cfg/utilities.hpp>

namespace cfg
{

enum class config_error
{
	unknown_option = 1,
	option_does_not_accept_argument,
	missing_argument_for_option,
	option_not_specified,
};

class config_category_impl : public std::error_category
{
  public:
	const char *name() const noexcept override
	{
		return "configuration";
	}

	std::string message(int ev) const override
	{
		switch (static_cast<config_error>(ev))
		{
			case config_error::unknown_option:
				return "unknown option";
			case config_error::option_does_not_accept_argument:
				return "option does not accept argument";
			case config_error::missing_argument_for_option:
				return "missing argument for option";
			case config_error::option_not_specified:
				return "option was not specified";
			default:
				assert(false);
				return "unknown error code";
		}
	}

	bool equivalent(const std::error_code &code, int condition) const noexcept override
	{
		return false;
	}
};

inline std::error_category &config_category()
{
	static config_category_impl instance;
	return instance;
}

inline std::error_code make_error_code(config_error e)
{
	return std::error_code(static_cast<int>(e), config_category());
}

inline std::error_condition make_error_condition(config_error e)
{
	return std::error_condition(static_cast<int>(e), config_category());
}

// --------------------------------------------------------------------

class config
{
  public:
	template <typename T, typename = void>
	struct option_traits;

	struct option_base
	{
		std::string m_name;
		std::string m_desc;
		char m_short_name;
		bool m_is_flag = true, m_has_default = false, m_hidden;
		int m_seen = 0;

		option_base(const option_base &rhs) = default;

		option_base(std::string_view name, std::string_view desc, bool hidden)
			: m_name(name)
			, m_desc(desc)
			, m_short_name(0)
			, m_hidden(hidden)
		{
			if (m_name.length() == 1)
				m_short_name = m_name.front();
			else if (m_name.length() > 2 and m_name[m_name.length() - 2] == ',')
			{
				m_short_name = m_name.back();
				m_name.erase(m_name.end() - 2, m_name.end());
			}
		}

		virtual ~option_base() = default;

		virtual void set_value(std::string_view value, std::error_code &ec)
		{
			assert(false);
		}

		virtual std::any get_value() const
		{
			return {};
		}

		virtual std::string get_default_value() const
		{
			return {};
		}

		virtual option_base *clone() const
		{
			return new option_base(*this);
		}

		uint32_t width() const
		{
			uint32_t result = m_name.length();
			if (result <= 1)
				result = 2;
			else if (m_short_name != 0)
				result += 7;
			if (not m_is_flag)
			{
				result += 4;
				if (m_has_default)
					result += 4 + get_default_value().length();
			}
			return result + 6;
		}

		void write(std::ostream &os, uint32_t width) const
		{
			uint32_t w2 = 2;
			os << "  ";
			if (m_short_name)
			{
				os << '-' << m_short_name;
				w2 += 2;
				if (m_name.length() > 1)
				{
					os << " [ --" << m_name << " ]";
					w2 += 7 + m_name.length();
				}
			}
			else
			{
				os << "--" << m_name;
				w2 += 2 + m_name.length();
			}

			if (not m_is_flag)
			{
				os << " arg";
				w2 += 4;

				if (m_has_default)
				{
					auto default_value = get_default_value();
					os << " (=" << default_value << ')';
					w2 += 4 + default_value.length();
				}
			}

			auto leading_spaces = width;
			if (w2 + 2 > width)
				os << std::endl;
			else
				leading_spaces = width - w2;

			word_wrapper ww(m_desc, get_terminal_width() - width);
			for (auto line : ww)
			{
				os << std::string(leading_spaces, ' ') << line << std::endl;
				leading_spaces = width;
			}
		}
	};

	template <typename T>
	struct option : public option_base
	{
		using traits_type = option_traits<T>;
		using value_type = typename option_traits<T>::value_type;

		std::optional<value_type> m_value;

		option(const option &rhs) = default;

		option(std::string_view name, std::string_view desc, bool hidden)
			: option_base(name, desc, hidden)
		{
			m_is_flag = false;
		}

		option(std::string_view name, const value_type &default_value, std::string_view desc, bool hidden)
			: option(name, desc, hidden)
		{
			m_has_default = true;
			m_value = default_value;
		}

		void set_value(std::string_view argument, std::error_code &ec) override
		{
			m_value = traits_type::set_value(argument, ec);
		}

		std::any get_value() const override
		{
			std::any result;
			if (m_value)
				result = *m_value;
			return result;
		}

		std::string get_default_value() const override
		{
			if constexpr (std::is_same_v<value_type, std::string>)
				return *m_value;
			else
				return traits_type::to_string(*m_value);
		}

		option_base *clone() const override
		{
			return new option(*this);
		}
	};

	template <typename... Options>
	void init(Options... options)
	{
		m_impl.reset(new config_impl(std::forward<Options>(options)...));
	}

	void parse(int argc, const char *const argv[], bool ignore_unknown = false)
	{
		std::error_code ec;
		parse(argc, argv, ignore_unknown, ec);
		if (ec)
			throw std::system_error(ec);
	}

	void parse(int argc, const char *const argv[], bool ignore_unknown, std::error_code &ec)
	{
		using namespace std::literals;

		enum class State
		{
			options,
			operands
		} state = State::options;

		for (int i = 1; i < argc and not ec; ++i)
		{
			const char *arg = argv[i];

			if (arg == nullptr) // should not happen
				break;

			if (state == State::options)
			{
				if (*arg != '-') // according to POSIX this is the end of options, start operands
				                 // state = State::operands;
				{                // however, people nowadays expect to be able to mix operands and options
					m_impl->m_operands.emplace_back(arg);
					continue;
				}
				else if (arg[1] == '-' and arg[2] == 0)
				{
					state = State::operands;
					continue;
				}
			}

			if (state == State::operands)
			{
				m_impl->m_operands.emplace_back(arg);
				continue;
			}

			option_base *opt = nullptr;
			std::string_view opt_arg;

			assert(*arg == '-');
			++arg;

			if (*arg == '-') // double --, start of new argument
			{
				++arg;

				assert(*arg != 0); // this should not happen, as it was checked for before

				std::string_view s_arg(arg);
				std::string_view::size_type p = s_arg.find('=');

				if (p != std::string_view::npos)
				{
					opt_arg = s_arg.substr(p + 1);
					s_arg = s_arg.substr(0, p);
				}

				opt = m_impl->get_option(s_arg);
				if (opt == nullptr)
				{
					if (not ignore_unknown)
						ec = make_error_code(config_error::unknown_option);
					continue;
				}

				if (opt->m_is_flag)
				{
					if (not opt_arg.empty())
						ec = make_error_code(config_error::option_does_not_accept_argument);

					++opt->m_seen;
					continue;
				}

				++opt->m_seen;
			}
			else // single character options
			{
				bool expect_option_argument = false;

				while (*arg != 0 and not ec)
				{
					opt = m_impl->get_option(*arg++);

					if (opt == nullptr)
					{
						if (not ignore_unknown)
							ec = make_error_code(config_error::unknown_option);
						continue;
					}

					++opt->m_seen;
					if (opt->m_is_flag)
						continue;

					opt_arg = arg;
					expect_option_argument = true;
					break;
				}

				if (not expect_option_argument)
					continue;
			}

			if (opt_arg.empty() and i + 1 < argc) // So, the = character was not present, the next arg must be the option argument
			{
				++i;
				opt_arg = argv[i];
			}

			if (opt_arg.empty())
				ec = make_error_code(config_error::missing_argument_for_option);
			else
				opt->set_value(opt_arg, ec);
		}
	}

	static config &instance()
	{
		static std::unique_ptr<config> s_instance;
		if (not s_instance)
			s_instance.reset(new config);
		return *s_instance;
	}

	bool has(std::string_view name) const
	{
		auto opt = m_impl->get_option(name);
		return opt != nullptr and (opt->m_seen > 0 or opt->m_has_default);
	}

	int count(std::string_view name) const
	{
		auto opt = m_impl->get_option(name);
		return opt ? opt->m_seen : 0;
	}

	template <typename T>
	auto get(const std::string &name) const
	{
		auto opt = m_impl->get_option(name);
		if (opt == nullptr)
			throw std::system_error(make_error_code(config_error::unknown_option), name);

		std::any value = opt->get_value();

		if (not value.has_value())
			throw std::system_error(make_error_code(config_error::option_not_specified), name);

		return std::any_cast<T>(value);
	}

	const std::vector<std::string> &operands() const
	{
		return m_impl->m_operands;
	}

	friend std::ostream &operator<<(std::ostream &os, const config &conf)
	{
		uint32_t terminal_width = get_terminal_width();
		uint32_t options_width = 0;

		for (auto &opt : conf.m_impl->m_options)
		{
			if (options_width < opt->width())
				options_width = opt->width();
		}

		if (options_width > terminal_width / 2)
			options_width = terminal_width / 2;

		for (auto &opt : conf.m_impl->m_options)
		{
			opt->write(os, options_width);
		}

		return os;
	}

  private:
	config() = default;
	config(const config &) = delete;
	config &operator=(const config &) = delete;

	struct config_impl
	{
		template <typename... Options>
		config_impl(Options... options)
		{
			(m_options.push_back(options.clone()), ...);
		}

		~config_impl()
		{
			for (auto opt : m_options)
				delete opt;
		}

		option_base *get_option(std::string_view name) const
		{
			for (auto &o : m_options)
			{
				if (o->m_name == name)
					return &*o;
			}
			return nullptr;
		}

		option_base *get_option(char short_name) const
		{
			for (auto &o : m_options)
			{
				if (o->m_short_name == short_name)
					return &*o;
			}
			return nullptr;
		}

		std::vector<option_base *> m_options;
		std::vector<std::string> m_operands;
	};

	std::unique_ptr<config_impl> m_impl;
};

template <>
struct config::option<void> : public option_base
{
	option(const option &rhs) = default;

	option(std::string_view name, std::string_view desc, bool hidden)
		: option_base(name, desc, hidden)
	{
	}

	virtual option_base *clone() const
	{
		return new option(*this);
	}
};

template <typename T>
struct config::option_traits<T, typename std::enable_if_t<std::is_arithmetic_v<T>>>
{
	using value_type = T;

	static value_type set_value(std::string_view argument, std::error_code &ec)
	{
		value_type value;
		auto r = charconv<value_type>::from_chars(argument.begin(), argument.end(), value);
		if (r.ec != std::errc())
			ec = std::make_error_code(r.ec);
		return value;
	}

	static std::string to_string(const T& value)
	{
		char b[32];
		auto r = charconv<value_type>::to_chars(b, b + sizeof(b), value);
		if (r.ec != std::errc())
			throw std::system_error(std::make_error_code(r.ec));
		return { b, r.ptr };
	}
};

template <>
struct config::option_traits<std::filesystem::path>
{
	using value_type = std::filesystem::path;

	static value_type set_value(std::string_view argument, std::error_code &ec)
	{
		return value_type{ argument };
	}

	static std::string to_string(const std::filesystem::path& value)
	{
		return value.string();
	}
};

template <typename T>
struct config::option_traits<T, typename std::enable_if_t<not std::is_arithmetic_v<T> and std::is_assignable_v<std::string, T>>>
{
	using value_type = std::string;

	static value_type set_value(std::string_view argument, std::error_code &ec)
	{
		return value_type{ argument };
	}

	static std::string to_string(const T& value)
	{
		return { value };
	}
};

template <typename T = void>
auto make_option(std::string_view name, std::string_view description)
{
	return config::option<T>(name, description, false);
}

template <typename T = void>
auto make_hidden_option(std::string_view name, std::string_view description)
{
	return config::option<T>(name, description, true);
}

template <typename T>
auto make_option(std::string_view name, const T &v, std::string_view description)
{
	return config::option<T>(name, v, description, false);
}

template <typename T>
auto make_hidden_option(std::string_view name, const T &v, std::string_view description)
{
	return config::option<T>(name, v, description, true);
}

} // namespace cfg

namespace std
{

template <>
struct is_error_condition_enum<cfg::config_error>
	: public true_type
{
};

} // namespace std