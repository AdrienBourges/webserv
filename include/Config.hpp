/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/11/29 by you                       #+#    #+#             */
/*   Updated: 2025/11/29 by you                       ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIG_HPP
# define CONFIG_HPP

/*
    Config.hpp

    Ce fichier décrit les structures de configuration utilisées par le serveur :
      - ServerConfig : configuration globale d'un "server"
      - LocationConfig : configuration spécifique à une "location"

    Ce qu'on gère actuellement :
      - plusieurs blocs "server { ... }" dans le fichier de config
      - listen host:port (1 listen par server)
      - root, index
      - error_page
      - client_max_body_size
      - autoindex (server + location)
      - methods (GET / POST / DELETE) par location
      - redirect (3xx) par location
      - upload_store (dossier d'upload) par location
      - cgi .ext /path/to/interpreter; par location

    + host <hostname>;   (ajouté pour les virtual hosts HTTP)
*/

# include <string>
# include <istream>
# include <map>
# include <vector>
# include <set>
# include <cstddef>

/*
    LocationConfig

    Représente un bloc :

        location /path {
            root   ./www/sub;
            index  sub.html;
            methods GET POST;
            autoindex on;
            redirect 301 /new-path/;
            upload_store ./www/uploads;
            cgi .py /usr/bin/python3;
        }
*/

struct LocationConfig
{
	std::string              path;
	std::string              root;
	std::string              index;
	std::set<std::string>    allowedMethods;

	bool                     autoindex;
	bool                     autoindexSet;

	bool                     redirectSet;
	int                      redirectCode;
	std::string              redirectUrl;

	bool                     uploadStoreSet;
	std::string              uploadStore;

	bool                     cgiEnabled;
	std::string              cgiExtension;
	std::string              cgiPath;

	LocationConfig()
		: path("/"),
		  root(),
		  index(),
		  allowedMethods(),
		  autoindex(false),
		  autoindexSet(false),
		  redirectSet(false),
		  redirectCode(0),
		  redirectUrl(),
		  uploadStoreSet(false),
		  uploadStore(),
		  cgiEnabled(false),
		  cgiExtension(),
		  cgiPath()
	{}
};

/*
    ServerConfig

    Représente un bloc :

        server {
            listen 127.0.0.1:8080;
            host example.com;          # (optionnel) nom de vhost HTTP
            root ./www;
            index index.html;
            error_page 404 /404.html;
            client_max_body_size 1000000;
            autoindex off;

            location / { ... }
            location /upload { ... }
            location /cgi { ... }
        }

    On aura plusieurs ServerConfig dans le programme.
*/

struct ServerConfig
{
	std::string                 host;       // IP de listen OU hostname HTTP (si host <name>; utilisé)
	int                         port;
	std::string                 root;
	std::string                 index;
	std::map<int, std::string>  errorPages;
	std::size_t                 clientMaxBodySize;

	bool                        autoindex;

	std::vector<LocationConfig> locations;

	ServerConfig()
		: host("0.0.0.0"),
		  port(8080),
		  root("./www"),
		  index("index.html"),
		  errorPages(),
		  clientMaxBodySize(1024 * 1024),
		  autoindex(false),
		  locations()
	{}
};

class Config
{
public:
	Config();
	~Config();

	// Charge le fichier de configuration (ex: config/default.conf)
	// et remplit _servers avec 1 ou plusieurs ServerConfig.
	void load(const std::string &path);

	// Retourne la liste des serveurs configurés.
	const std::vector<ServerConfig> &getServers() const;

private:
	std::vector<ServerConfig> _servers;

	std::string trim(const std::string &s) const;

	void parseServerBlock(std::istream &in, ServerConfig &server);

	void parseListenDirective(const std::string &line, ServerConfig &server);
	void parseHostDirective(const std::string &line, ServerConfig &server);  // <-- AJOUT
	void parseRootDirective(const std::string &line, ServerConfig &server);
	void parseIndexDirective(const std::string &line, ServerConfig &server);
	void parseErrorPageDirective(const std::string &line, ServerConfig &server);
	void parseClientMaxBodySizeDirective(const std::string &line, ServerConfig &server);
	void parseServerAutoindexDirective(const std::string &line, ServerConfig &server);

	// location
	void parseLocationDirective(std::istream &in,
	                            const std::string &line,
	                            ServerConfig &server);
	void parseLocationBlock(std::istream &in,
	                        LocationConfig &loc,
	                        ServerConfig &server);
};

#endif // CONFIG_HPP

