#ifndef WEBSERVER_HPP
# define WEBSERVER_HPP

# include <vector>
# include <map>
# include <set>
# include <string>
# include <poll.h>
# include <cerrno>
# include <ctime>  // std::time_t

# include "Config.hpp"
# include "HttpRequest.hpp"
# include "HttpResponse.hpp"

/*
 * ClientState :
 *  - représente l'état d'une connexion cliente
 *  - on y garde : le server cible, la requête en cours,
 *    le buffer lu, le buffer à écrire, etc.
 *  - gère aussi le chunked et un timeout (lastActivity).
 */
struct ClientState
{
	const ServerConfig *server;      // serveur associé à cette connexion
	HttpRequest         request;     // requête HTTP en cours
	bool                headersComplete;
	bool                requestHandled;
	std::size_t         contentLength; // pour les bodies "normaux" (Content-Length)
	std::string         readBuffer;    // octets reçus non encore traités
	std::string         writeBuffer;   // réponse à envoyer

	// --- Gestion du Transfer-Encoding: chunked ---
	bool                isChunked;        // true si on a "Transfer-Encoding: chunked"
	std::size_t         currentChunkSize; // taille du chunk qu'on est en train de lire
	std::string         chunkDecodedBody; // body reconstruit après déchunk

	// --- Timeout ---
	std::time_t         lastActivity;     // dernière activité (lecture/écriture)

	ClientState();
};

class WebServer
{
public:
	WebServer(const std::vector<ServerConfig> &servers);
	~WebServer();

	void run();

private:
	WebServer(const WebServer &);
	WebServer &operator=(const WebServer &);

	void initListeningSockets();
	void handleNewConnection(std::size_t index);
	void handleClientRead(std::size_t index);
	void handleClientWrite(std::size_t index);
	void removeClient(std::size_t index);

	const LocationConfig *findLocationForTarget(const ServerConfig &server,
	                                            const std::string &target) const;
	bool isMethodAllowed(const LocationConfig *loc,
	                     const std::string &method) const;
	std::string buildAllowHeader(const LocationConfig *loc) const;
	std::string generateAutoindexPage(const std::string &dirPath,
	                                  const std::string &urlPath) const;

	void buildHttpResponse(const ServerConfig &server,
	                       const HttpRequest &request,
	                       HttpResponse &response);

	std::string getMimeType(const std::string &path) const;

	void setErrorResponse(const ServerConfig &server,
	                      HttpResponse &response,
	                      int code,
	                      const std::string &reason);

	// --- NOUVEAU ---
	// Sélectionne le bon server (virtual host) en fonction du header Host:
	// parmi tous les ServerConfig qui écoutent sur le même port.
	const ServerConfig *selectServerForRequest(const HttpRequest &request,
	                                           const ServerConfig &defaultServer) const;

private:
	std::vector<ServerConfig>           _servers;
	std::vector<struct pollfd>          _pollFds;
	std::map<int, ClientState>          _clients;

	// Pour chaque fd d'écoute, on garde un "server par défaut" pour ce port.
	std::map<int, const ServerConfig *> _listenFdToServer;
};

#endif

