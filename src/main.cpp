/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/11/24 by you                       #+#    #+#             */
/*   Updated: 2025/11/29 by you                       ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"
#include "WebServer.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
	std::string configPath = "config/default.conf";
	if (argc > 1)
		configPath = argv[1];

	try
	{
		Config config;
		config.load(configPath);

		const std::vector<ServerConfig> &servers = config.getServers();
		if (servers.empty())
		{
			std::cerr << "No servers defined in configuration" << std::endl;
			return 1;
		}

		WebServer server(servers);
		server.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}

