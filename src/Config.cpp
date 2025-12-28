/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/11/29 by you                       #+#    #+#             */
/*   Updated: 2025/11/29 by you                       ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstddef>
#include <cstdlib>

/*
    Classe Config

    - Ouvre le fichier de configuration
    - Cherche tous les blocs "server { ... }"
    - Pour chaque bloc :
         -> crée un ServerConfig
         -> parseServerBlock(...)
         -> push_back dans _servers
*/

Config::Config()
	: _servers()
{
}

Config::~Config()
{
}

std::string Config::trim(const std::string &s) const
{
	std::size_t start = 0;
	while (start < s.size() &&
	       (s[start] == ' ' || s[start] == '\t' ||
	        s[start] == '\r' || s[start] == '\n'))
		++start;

	if (start == s.size())
		return std::string();

	std::size_t end = s.size();
	while (end > start &&
	       (s[end - 1] == ' ' || s[end - 1] == '\t' ||
	        s[end - 1] == '\r' || s[end - 1] == '\n'))
		--end;

	return s.substr(start, end - start);
}

void Config::load(const std::string &path)
{
	_servers.clear();

	std::ifstream in(path.c_str());
	if (!in)
		throw std::runtime_error("Could not open config file: " + path);

	std::string line;

	while (std::getline(in, line))
	{
		line = trim(line);

		if (line.empty() || line[0] == '#')
			continue;

		if (line == "server{" || line == "server {")
		{
			// On a trouvé un nouveau bloc server
			ServerConfig server;
			parseServerBlock(in, server);
			_servers.push_back(server);
		}
		else
		{
			// Pour l'instant on ignore les directives globales (hors server)
			continue;
		}
	}

	if (_servers.empty())
		throw std::runtime_error("No 'server { ... }' block found in config");
}

/*
    parseServerBlock()

    Lit toutes les lignes entre "server {" et la "}" correspondante
    et remplit l'objet ServerConfig passé en paramètre.
*/
void Config::parseServerBlock(std::istream &in, ServerConfig &server)
{
	std::string line;

	while (std::getline(in, line))
	{
		line = trim(line);

		if (line.empty() || line[0] == '#')
			continue;

		if (line == "}")
			break;

		if (line.find("listen") == 0)
			parseListenDirective(line, server);
		else if (line.find("host") == 0)          // <-- NOUVELLE DIRECTIVE
			parseHostDirective(line, server);
		else if (line.find("root") == 0)
			parseRootDirective(line, server);
		else if (line.find("index") == 0)
			parseIndexDirective(line, server);
		else if (line.find("error_page") == 0)
			parseErrorPageDirective(line, server);
		else if (line.find("client_max_body_size") == 0)
			parseClientMaxBodySizeDirective(line, server);
		else if (line.find("autoindex") == 0)
			parseServerAutoindexDirective(line, server);
		else if (line.find("location") == 0)
			parseLocationDirective(in, line, server);
		else
			throw std::runtime_error("Unknown directive inside server block: " + line);
	}
}

/*
    listen 127.0.0.1:8080;
    listen 8080;
*/
void Config::parseListenDirective(const std::string &line, ServerConfig &server)
{
	std::istringstream iss(line);
	std::string keyword;
	std::string value;

	if (!(iss >> keyword))
		throw std::runtime_error("Invalid listen directive (missing keyword)");

	if (keyword != "listen")
		throw std::runtime_error("Invalid listen directive (wrong keyword)");

	if (!(iss >> value))
		throw std::runtime_error("Invalid listen directive (missing value)");

	if (value[value.size() - 1] != ';')
	{
		std::string semi;
		if (!(iss >> semi) || semi != ";")
			throw std::runtime_error("Invalid listen directive (missing ';')");
	}
	else
		value.erase(value.size() - 1);

	value = trim(value);

	std::string host;
	std::string portStr;

	std::size_t colonPos = value.find(':');
	if (colonPos == std::string::npos)
	{
		host = "0.0.0.0";
		portStr = value;
	}
	else
	{
		host = value.substr(0, colonPos);
		portStr = value.substr(colonPos + 1);
	}

	int port = std::atoi(portStr.c_str());
	if (port <= 0 || port > 65535)
		throw std::runtime_error("Invalid port in listen directive: " + portStr);

	server.host = host;  // IP d'écoute par défaut
	server.port = port;
}

/*
    host example.com;
    host test.com;
    (virtual host HTTP - utilisé pour matcher le header Host:)
*/
void Config::parseHostDirective(const std::string &line, ServerConfig &server)
{
	std::istringstream iss(line);
	std::string keyword;
	std::string value;

	if (!(iss >> keyword))
		throw std::runtime_error("Invalid host directive (missing keyword)");

	if (keyword != "host")
		throw std::runtime_error("Invalid host directive (wrong keyword)");

	if (!(iss >> value))
		throw std::runtime_error("Invalid host directive (missing value)");

	if (value[value.size() - 1] != ';')
	{
		std::string semi;
		if (!(iss >> semi) || semi != ";")
			throw std::runtime_error("Invalid host directive (missing ';')");
	}
	else
		value.erase(value.size() - 1);

	value = trim(value);

	if (value.empty())
		throw std::runtime_error("Invalid host directive (empty value)");

	// Ici on écrase le host défini par listen()
	// pour qu'il devienne le hostname HTTP (ex: "example.com").
	server.host = value;
}

void Config::parseRootDirective(const std::string &line, ServerConfig &server)
{
	std::istringstream iss(line);
	std::string keyword;
	std::string value;

	if (!(iss >> keyword))
		throw std::runtime_error("Invalid root directive (missing keyword)");

	if (keyword != "root")
		throw std::runtime_error("Invalid root directive (wrong keyword)");

	if (!(iss >> value))
		throw std::runtime_error("Invalid root directive (missing value)");

	if (value[value.size() - 1] != ';')
	{
		std::string semi;
		if (!(iss >> semi) || semi != ";")
			throw std::runtime_error("Invalid root directive (missing ';')");
	}
	else
		value.erase(value.size() - 1);

	value = trim(value);

	if (value.empty())
		throw std::runtime_error("Invalid root directive (empty value)");

	server.root = value;
}

void Config::parseIndexDirective(const std::string &line, ServerConfig &server)
{
	std::istringstream iss(line);
	std::string keyword;
	std::string value;

	if (!(iss >> keyword))
		throw std::runtime_error("Invalid index directive (missing keyword)");

	if (keyword != "index")
		throw std::runtime_error("Invalid index directive (wrong keyword)");

	if (!(iss >> value))
		throw std::runtime_error("Invalid index directive (missing value)");

	if (value[value.size() - 1] != ';')
	{
		std::string semi;
		if (!(iss >> semi) || semi != ";")
			throw std::runtime_error("Invalid index directive (missing ';')");
	}
	else
		value.erase(value.size() - 1);

	value = trim(value);

	if (value.empty())
		throw std::runtime_error("Invalid index directive (empty value)");

	server.index = value;
}

void Config::parseErrorPageDirective(const std::string &line, ServerConfig &server)
{
	std::istringstream iss(line);
	std::string keyword;
	std::string codeStr;
	std::string path;

	if (!(iss >> keyword))
		throw std::runtime_error("Invalid error_page directive (missing keyword)");

	if (keyword != "error_page")
		throw std::runtime_error("Invalid error_page directive (wrong keyword)");

	if (!(iss >> codeStr))
		throw std::runtime_error("Invalid error_page directive (missing code)");

	if (!(iss >> path))
		throw std::runtime_error("Invalid error_page directive (missing path)");

	if (path[path.size() - 1] != ';')
	{
		std::string semi;
		if (!(iss >> semi) || semi != ";")
			throw std::runtime_error("Invalid error_page directive (missing ';')");
	}
	else
		path.erase(path.size() - 1);

	path = trim(path);

	int code = std::atoi(codeStr.c_str());
	if (code < 100 || code > 599)
		throw std::runtime_error("Invalid HTTP status code in error_page: " + codeStr);

	if (path.empty())
		throw std::runtime_error("Invalid error_page directive (empty path)");

	server.errorPages[code] = path;
}

void Config::parseClientMaxBodySizeDirective(const std::string &line, ServerConfig &server)
{
	std::istringstream iss(line);
	std::string keyword;
	std::string value;

	if (!(iss >> keyword))
		throw std::runtime_error("Invalid client_max_body_size directive (missing keyword)");

	if (keyword != "client_max_body_size")
		throw std::runtime_error("Invalid client_max_body_size directive (wrong keyword)");

	if (!(iss >> value))
		throw std::runtime_error("Invalid client_max_body_size directive (missing value)");

	if (value[value.size() - 1] != ';')
	{
		std::string semi;
		if (!(iss >> semi) || semi != ";")
			throw std::runtime_error("Invalid client_max_body_size directive (missing ';')");
	}
	else
		value.erase(value.size() - 1);

	value = trim(value);

	if (value.empty())
		throw std::runtime_error("Invalid client_max_body_size directive (empty value)");

	unsigned long tmp = 0;
	{
		std::istringstream valStream(value);
		if (!(valStream >> tmp) || !valStream.eof())
			throw std::runtime_error("Invalid client_max_body_size value: " + value);
	}

	if (tmp == 0)
		throw std::runtime_error("client_max_body_size must be > 0");

	server.clientMaxBodySize = static_cast<std::size_t>(tmp);
}

void Config::parseServerAutoindexDirective(const std::string &line, ServerConfig &server)
{
	std::istringstream iss(line);
	std::string keyword;
	std::string value;

	if (!(iss >> keyword))
		throw std::runtime_error("Invalid autoindex directive (missing keyword)");

	if (keyword != "autoindex")
		throw std::runtime_error("Invalid autoindex directive (wrong keyword)");

	if (!(iss >> value))
		throw std::runtime_error("Invalid autoindex directive (missing value)");

	if (value[value.size() - 1] != ';')
	{
		std::string semi;
		if (!(iss >> semi) || semi != ";")
			throw std::runtime_error("Invalid autoindex directive (missing ';')");
	}
	else
		value.erase(value.size() - 1);

	value = trim(value);

	if (value == "on")
		server.autoindex = true;
	else if (value == "off")
		server.autoindex = false;
	else
		throw std::runtime_error("Invalid autoindex value (expected 'on' or 'off'): " + value);
}

/*
    location /path { ... }
*/
void Config::parseLocationDirective(std::istream &in,
                                    const std::string &line,
                                    ServerConfig &server)
{
	std::istringstream iss(line);
	std::string keyword;
	std::string pathToken;

	if (!(iss >> keyword >> pathToken))
		throw std::runtime_error("Invalid location directive (missing keyword or path)");

	if (keyword != "location")
		throw std::runtime_error("Invalid location directive (wrong keyword)");

	bool hasBrace = false;
	if (!pathToken.empty() && pathToken[pathToken.size() - 1] == '{')
	{
		hasBrace = true;
		pathToken.erase(pathToken.size() - 1);
	}

	std::string path = trim(pathToken);

	if (!hasBrace)
	{
		std::string brace;
		if (!(iss >> brace) || brace != "{")
			throw std::runtime_error("Invalid location directive (missing '{')");
	}

	if (path.empty())
		throw std::runtime_error("Invalid location directive (empty path)");

	if (path[0] != '/')
		path = "/" + path;

	if (path.size() > 1 && path[path.size() - 1] == '/')
		path.erase(path.size() - 1);

	LocationConfig loc;
	loc.path = path;

	parseLocationBlock(in, loc, server);

	server.locations.push_back(loc);
}

/*
    parseLocationBlock()

    Gère :
        root
        index
        methods
        autoindex
        redirect
        upload_store
        cgi
*/
void Config::parseLocationBlock(std::istream &in,
                                LocationConfig &loc,
                                ServerConfig &server)
{
	(void)server; // pour l'instant on ne l'utilise pas directement ici

	std::string line;

	while (std::getline(in, line))
	{
		line = trim(line);

		if (line.empty() || line[0] == '#')
			continue;

		if (line == "}")
			break;

		if (line.find("root") == 0)
		{
			std::istringstream iss(line);
			std::string keyword;
			std::string value;

			if (!(iss >> keyword))
				throw std::runtime_error("Invalid root directive in location (missing keyword)");

			if (keyword != "root")
				throw std::runtime_error("Invalid root directive in location (wrong keyword)");

			if (!(iss >> value))
				throw std::runtime_error("Invalid root directive in location (missing value)");

			if (value[value.size() - 1] != ';')
			{
				std::string semi;
				if (!(iss >> semi) || semi != ";")
					throw std::runtime_error("Invalid root directive in location (missing ';')");
			}
			else
				value.erase(value.size() - 1);

			value = trim(value);
			if (value.empty())
				throw std::runtime_error("Invalid root directive in location (empty value)");

			loc.root = value;
		}
		else if (line.find("index") == 0)
		{
			std::istringstream iss(line);
			std::string keyword;
			std::string value;

			if (!(iss >> keyword))
				throw std::runtime_error("Invalid index directive in location (missing keyword)");

			if (keyword != "index")
				throw std::runtime_error("Invalid index directive in location (wrong keyword)");

			if (!(iss >> value))
				throw std::runtime_error("Invalid index directive in location (missing value)");

			if (value[value.size() - 1] != ';')
			{
				std::string semi;
				if (!(iss >> semi) || semi != ";")
					throw std::runtime_error("Invalid index directive in location (missing ';')");
			}
			else
				value.erase(value.size() - 1);

			value = trim(value);
			if (value.empty())
				throw std::runtime_error("Invalid index directive in location (empty value)");

			loc.index = value;
		}
		else if (line.find("methods") == 0)
		{
			std::istringstream iss(line);
			std::string keyword;

			if (!(iss >> keyword))
				throw std::runtime_error("Invalid methods directive in location (missing keyword)");

			if (keyword != "methods")
				throw std::runtime_error("Invalid methods directive in location (wrong keyword)");

			std::vector<std::string> methods;
			std::string token;

			while (iss >> token)
			{
				if (!token.empty() && token[token.size() - 1] == ';')
				{
					token.erase(token.size() - 1);
					token = trim(token);
					if (!token.empty())
						methods.push_back(token);
					break;
				}
				else
				{
					methods.push_back(token);
				}
			}

			if (methods.empty())
				throw std::runtime_error("Invalid methods directive in location (no methods)");

			for (std::size_t i = 0; i < methods.size(); ++i)
			{
				std::string m = methods[i];

				for (std::size_t j = 0; j < m.size(); ++j)
				{
					if (m[j] >= 'a' && m[j] <= 'z')
						m[j] = static_cast<char>(m[j] - 'a' + 'A');
				}

				if (m != "GET" && m != "POST" && m != "DELETE")
					throw std::runtime_error("Invalid HTTP method in methods directive: " + m);

				loc.allowedMethods.insert(m);
			}
		}
		else if (line.find("autoindex") == 0)
		{
			std::istringstream iss(line);
			std::string keyword;
			std::string value;

			if (!(iss >> keyword))
				throw std::runtime_error("Invalid autoindex directive in location (missing keyword)");

			if (keyword != "autoindex")
				throw std::runtime_error("Invalid autoindex directive in location (wrong keyword)");

			if (!(iss >> value))
				throw std::runtime_error("Invalid autoindex directive in location (missing value)");

			if (value[value.size() - 1] != ';')
			{
				std::string semi;
				if (!(iss >> semi) || semi != ";")
					throw std::runtime_error("Invalid autoindex directive in location (missing ';')");
			}
			else
				value.erase(value.size() - 1);

			value = trim(value);

			if (value == "on")
			{
				loc.autoindex = true;
				loc.autoindexSet = true;
			}
			else if (value == "off")
			{
				loc.autoindex = false;
				loc.autoindexSet = true;
			}
			else
				throw std::runtime_error("Invalid autoindex value in location (expected 'on' or 'off'): " + value);
		}
		else if (line.find("redirect") == 0)
		{
			std::istringstream iss(line);
			std::string keyword;

			if (!(iss >> keyword))
				throw std::runtime_error("Invalid redirect directive in location (missing keyword)");

			if (keyword != "redirect")
				throw std::runtime_error("Invalid redirect directive in location (wrong keyword)");

			std::vector<std::string> tokens;
			std::string token;

			while (iss >> token)
			{
				if (!token.empty() && token[token.size() - 1] == ';')
				{
					token.erase(token.size() - 1);
					token = trim(token);
					if (!token.empty())
						tokens.push_back(token);
					break;
				}
				else
				{
					tokens.push_back(token);
				}
			}

			if (tokens.empty())
				throw std::runtime_error("Invalid redirect directive in location (no arguments)");

			int         code = 302;
			std::string url;

			if (tokens.size() == 1)
			{
				url = tokens[0];
			}
			else if (tokens.size() == 2)
			{
				const std::string &codeStr = tokens[0];
				const std::string &urlStr  = tokens[1];

				int tmp = std::atoi(codeStr.c_str());
				if (tmp < 300 || tmp > 399)
					throw std::runtime_error("Invalid redirect HTTP code (must be 3xx): " + codeStr);

				code = tmp;
				url  = urlStr;
			}
			else
			{
				throw std::runtime_error("Invalid redirect directive in location (too many arguments)");
			}

			url = trim(url);
			if (url.empty())
				throw std::runtime_error("Invalid redirect directive in location (empty URL)");

			loc.redirectSet  = true;
			loc.redirectCode = code;
			loc.redirectUrl  = url;
		}
		else if (line.find("upload_store") == 0)
		{
			std::istringstream iss(line);
			std::string keyword;
			std::string value;

			if (!(iss >> keyword))
				throw std::runtime_error("Invalid upload_store directive in location (missing keyword)");

			if (keyword != "upload_store")
				throw std::runtime_error("Invalid upload_store directive in location (wrong keyword)");

			if (!(iss >> value))
				throw std::runtime_error("Invalid upload_store directive in location (missing value)");

			if (value[value.size() - 1] != ';')
			{
				std::string semi;
				if (!(iss >> semi) || semi != ";")
					throw std::runtime_error("Invalid upload_store directive in location (missing ';')");
			}
			else
				value.erase(value.size() - 1);

			value = trim(value);
			if (value.empty())
				throw std::runtime_error("Invalid upload_store directive in location (empty value)");

			loc.uploadStoreSet = true;
			loc.uploadStore    = value;
		}
		else if (line.find("cgi") == 0)
		{
			/*
			    cgi .py /usr/bin/python3;
			*/
			std::istringstream iss(line);
			std::string keyword;
			std::string ext;
			std::string path;

			if (!(iss >> keyword))
				throw std::runtime_error("Invalid cgi directive in location (missing keyword)");

			if (keyword != "cgi")
				throw std::runtime_error("Invalid cgi directive in location (wrong keyword)");

			if (!(iss >> ext))
				throw std::runtime_error("Invalid cgi directive in location (missing extension)");

			if (!(iss >> path))
				throw std::runtime_error("Invalid cgi directive in location (missing path)");

			if (path[path.size() - 1] != ';')
			{
				std::string semi;
				if (!(iss >> semi) || semi != ";")
					throw std::runtime_error("Invalid cgi directive in location (missing ';')");
			}
			else
				path.erase(path.size() - 1);

			ext  = trim(ext);
			path = trim(path);

			if (ext.empty() || path.empty())
				throw std::runtime_error("Invalid cgi directive in location (empty ext or path)");

			loc.cgiEnabled   = true;
			loc.cgiExtension = ext;
			loc.cgiPath      = path;
		}
		else
		{
			throw std::runtime_error("Unknown directive inside location block: " + line);
		}
	}
}

const std::vector<ServerConfig> &Config::getServers() const
{
	return _servers;
}

