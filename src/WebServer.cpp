/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   WebServer.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/11/24 by you                       #+#    #+#             */
/*   Updated: 2025/12/03 by you                       ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "WebServer.hpp"

#include <iostream>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <cstdio>      // std::remove
#include <sys/stat.h>  // stat, S_ISDIR
#include <dirent.h>    // opendir, readdir, closedir
#include <unistd.h>    // pipe, fork, dup2, execve, close, read, write, chdir
#include <sys/types.h> // pid_t, ssize_t
#include <sys/wait.h>  // waitpid, WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <cerrno>
#include <map>
#include <vector>
#include <ctime>       // std::time
#include <poll.h>      // poll()
#include <signal.h>    // kill, SIGKILL

/*
 * Petit namespace anonyme pour les fonctions internes à ce fichier.
 */
namespace
{
	// Sentinelle pour "aucun chunk en cours"
	static const std::size_t NO_CHUNK_SIZE = static_cast<std::size_t>(-1);

	// Timeout client : 30 secondes d'inactivité
	static const int CLIENT_TIMEOUT_SECONDS = 30;

	// Timeout pour un CGI (en secondes)
	static const int CGI_TIMEOUT_SECONDS = 30;

	// Trim de base (enlève espaces / tab / \r / \n en début et fin de chaîne)
	static std::string trimString(const std::string &s)
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

	// Récupère la partie après '?' dans une cible : "/path?foo=bar" -> "foo=bar"
	static std::string extractQueryString(const std::string &target)
	{
		std::size_t pos = target.find('?');
		if (pos == std::string::npos)
			return std::string();
		return target.substr(pos + 1);
	}

	// Vérifie si path se termine par ext (ex: ".py")
	static bool hasExtension(const std::string &path, const std::string &ext)
	{
		if (ext.empty())
			return false;
		if (path.size() < ext.size())
			return false;
		return (path.compare(path.size() - ext.size(), ext.size(), ext) == 0);
	}

	/*
	 * Résout un chemin fichier à partir d'un server, d'une location et d'un target.
	 */
	static bool resolvePathForCgi(const ServerConfig &server,
	                              const LocationConfig *loc,
	                              const std::string &target,
	                              std::string &outPath)
	{
		// On enlève la query string (après '?')
		std::string pathTarget = target;
		std::size_t qPos = pathTarget.find('?');
		if (qPos != std::string::npos)
			pathTarget.erase(qPos);

		// Doit commencer par '/'
		if (pathTarget.empty() || pathTarget[0] != '/')
			return false;

		// Sécurité basique : on refuse les ".."
		if (pathTarget.find("..") != std::string::npos)
			return false;

		// On part du root du serveur, ou du root de la location si défini
		std::string root = server.root;
		if (loc && !loc->root.empty())
			root = loc->root;

		std::string suffix;
		if (loc)
		{
			std::string locPath = loc->path;
			if (locPath.size() > 1 && locPath[locPath.size() - 1] == '/')
				locPath.erase(locPath.size() - 1);

			if (pathTarget.size() >= locPath.size())
				suffix = pathTarget.substr(locPath.size());
			else
				suffix.clear();
		}
		else
		{
			suffix = pathTarget;
		}

		std::string path = root;

		if (!suffix.empty())
		{    
			const bool rootEndsSlash = !path.empty() && path[path.size() - 1] == '/';
    		const bool sufStartsSlash = !suffix.empty() && suffix[0] == '/';

    		if (rootEndsSlash && sufStartsSlash)
        		path += suffix.substr(1);        // avoid double //
    		else if (!rootEndsSlash && !sufStartsSlash)
        		path += "/" + suffix;            // add missing /
    		else
        		path += suffix; 
		}

		outPath = path;
		return true;
	}

	/*
	 * decodeChunkedBody()
	 *
	 *  - buffer : contient les octets de la requête après les headers,
	 *             au format chunked.
	 *  - outBody : on y accumule le body "normal" (déchunké).
	 *  - finished : mis à true quand on a reçu le chunk final (taille 0 + trailers).
	 *  - currentChunkSize : taille du chunk en cours de lecture.
	 *  - maxSize : client_max_body_size (0 => pas de limite).
	 *  - tooLarge : mis à true si on dépasse maxSize.
	 */
	static bool decodeChunkedBody(std::string &buffer,
	                              std::string &outBody,
	                              bool &finished,
	                              std::size_t &currentChunkSize,
	                              std::size_t maxSize,
	                              bool &tooLarge)
	{
		finished = false;
		tooLarge = false;

		while (true)
		{
			// 1) Si on attend la taille du prochain chunk
			if (currentChunkSize == NO_CHUNK_SIZE)
			{
				std::size_t pos = buffer.find("\r\n");
				if (pos == std::string::npos)
				{
					// Ligne de taille pas complète
					return true; // pas d'erreur, juste besoin de plus de données
				}

				std::string sizeLine = buffer.substr(0, pos);
				buffer.erase(0, pos + 2); // on enlève "sizeLine\r\n"

				// Gestion éventuelle d'extensions : "A;foo=bar"
				std::size_t semi = sizeLine.find(';');
				if (semi != std::string::npos)
					sizeLine.erase(semi);

				sizeLine = trimString(sizeLine);
				if (sizeLine.empty())
					return false; // format invalide

				std::size_t chunkSize = 0;
				{
					std::istringstream iss(sizeLine);
					iss >> std::hex >> chunkSize;
					if (!iss || !iss.eof())
						return false; // pas un entier hexa correct
				}

				if (chunkSize == 0)
				{
					// Dernier chunk : il reste les trailers + CRLF final

					// Cas sans trailer : buffer commence par "\r\n"
					if (buffer.size() < 2)
					{
						// On attend au moins le CRLF final
						return true;
					}

					if (buffer[0] == '\r' && buffer[1] == '\n')
					{
						// Pas de trailers : simple "0\r\n\r\n"
						buffer.erase(0, 2);
						finished = true;
						return true;
					}

					// Cas avec trailers : on cherche la ligne vide qui termine les trailers
					std::size_t trailerEnd = buffer.find("\r\n\r\n");
					if (trailerEnd == std::string::npos)
					{
						// trailers incomplets
						return true;
					}

					buffer.erase(0, trailerEnd + 4);
					finished = true;
					return true;
				}

				// On a une taille de chunk > 0, on va lire les données
				currentChunkSize = chunkSize;
			}

			// 2) Ici, currentChunkSize > 0 : on attend chunkSize octets + "\r\n"
			if (buffer.size() < currentChunkSize + 2)
			{
				// pas encore assez de données pour lire ce chunk
				return true;
			}

			// On lit le chunk
			outBody.append(buffer.c_str(), currentChunkSize);

			if (maxSize > 0 && outBody.size() > maxSize)
			{
				tooLarge = true;
				return false;
			}

			// Vérification du CRLF de fin de chunk
			if (buffer[currentChunkSize] != '\r' ||
			    buffer[currentChunkSize + 1] != '\n')
			{
				return false; // format invalide
			}

			// On consomme chunk + CRLF
			buffer.erase(0, currentChunkSize + 2);

			// Et on revient à l'état "en attente d'une nouvelle ligne de taille"
			currentChunkSize = NO_CHUNK_SIZE;
		}
	}

	/*
	 * executeCgi()
	 *
	 * - lance le CGI via fork + execve
	 * - envoie le body (POST) sur stdin du CGI
	 * - lit stdout du CGI AVEC UN TIMEOUT (CGI_TIMEOUT_SECONDS)
	 * - si le CGI se termine avec un code != 0, on considère que c'est une erreur
	 *
	 * - IMPORTANT :
	 *   - on met CONTENT_LENGTH = taille réelle du body qu'on envoie
	 *   - on essaie de déchunker request.getBody() si jamais il ressemble
	 *     encore à du chunked (fallback robuste).
	 */
	static bool executeCgi(const HttpRequest &request,
	                       const ServerConfig &serverCfg,
	                       const LocationConfig *loc,
	                       const std::string &scriptPath,
	                       std::string &outBody,
	                       std::map<std::string, std::string> &outHeaders,
	                       int &outStatusCode,
	                       std::string &outReason)
	{
		outBody.clear();
		outHeaders.clear();
		outStatusCode = 200;
		outReason = "OK";

		// -------------------------------
		// 1) Préparer le body pour le CGI
		// -------------------------------
		std::string bodyForCgi = request.getBody();

		// Fallback : si, pour une raison X, le body est encore au format chunked,
		// on essaie de le déchunker ici (sans casser les bodies normaux).
		if (!bodyForCgi.empty())
		{
			std::string buffer = bodyForCgi;
			std::string decoded;
			bool finished = false;
			bool tooLarge = false;
			std::size_t currentChunkSize = NO_CHUNK_SIZE;

			bool ok = decodeChunkedBody(buffer,
			                            decoded,
			                            finished,
			                            currentChunkSize,
			                            0,        // pas de limite ici (déjà gérée avant)
			                            tooLarge);

			if (ok && finished && !tooLarge)
			{
				// Ça ressemble à du vrai chunked -> on garde la version déchunkée
				bodyForCgi = decoded;
			}
		}

		// -------------------------------
		// 2) Création des pipes + fork
		// -------------------------------
		int inPipe[2];
		int outPipe[2];

		if (pipe(inPipe) < 0)
			return false;
		if (pipe(outPipe) < 0)
		{
			close(inPipe[0]);
			close(inPipe[1]);
			return false;
		}

		pid_t pid = fork();
		if (pid < 0)
		{
			close(inPipe[0]);
			close(inPipe[1]);
			close(outPipe[0]);
			close(outPipe[1]);
			return false;
		}

		if (pid == 0)
		{
			// ===== Enfant : exécution du CGI =====

			// On redirige stdin/stdout vers nos pipes
			if (dup2(inPipe[0], STDIN_FILENO) < 0)
				_exit(1);
			if (dup2(outPipe[1], STDOUT_FILENO) < 0)
				_exit(1);

			close(inPipe[0]);
			close(inPipe[1]);
			close(outPipe[0]);
			close(outPipe[1]);

			// --- IMPORTANT : se placer dans le dossier du script (chdir) ---
			std::string scriptDir;
			std::string scriptName;
			std::size_t slashPos = scriptPath.rfind('/');
			if (slashPos != std::string::npos)
			{
				scriptDir  = scriptPath.substr(0, slashPos);
				scriptName = scriptPath.substr(slashPos + 1);
				if (scriptDir.empty())
					scriptDir = ".";
			}
			else
			{
				scriptDir  = ".";
				scriptName = scriptPath;
			}

			if (chdir(scriptDir.c_str()) < 0)
				_exit(1);

			// -----------------------
			// Construction de l'env CGI
			// -----------------------
			std::vector<std::string> env;

			env.push_back("GATEWAY_INTERFACE=CGI/1.1");
			env.push_back("SERVER_PROTOCOL=HTTP/1.1");
			env.push_back("SERVER_SOFTWARE=webserv/0.1");

			env.push_back("REQUEST_METHOD=" + request.getMethod());
			env.push_back("QUERY_STRING=" + extractQueryString(request.getTarget()));

			env.push_back("SCRIPT_FILENAME=" + scriptPath);
			env.push_back("SCRIPT_NAME=" + scriptPath);

			env.push_back("SERVER_NAME=" + serverCfg.host);
			{
				std::ostringstream oss;
				oss << serverCfg.port;
				env.push_back("SERVER_PORT=" + oss.str());
			}

			// Content-Type
			std::string contentType = request.getHeader("Content-Type");
			if (!contentType.empty())
				env.push_back("CONTENT_TYPE=" + contentType);

			// CONTENT_LENGTH = taille réelle du body envoyé au CGI (POST uniquement)
			if (request.getMethod() == "POST")
			{
				std::ostringstream oss;
				oss << bodyForCgi.size();
				env.push_back("CONTENT_LENGTH=" + oss.str());
			}

			// HTTP_HOST
			std::string hostHeader = request.getHeader("Host");
			if (!hostHeader.empty())
				env.push_back("HTTP_HOST=" + hostHeader);

			// Conversion vector<string> -> char*[]
			std::vector<char *> envp;
			for (std::size_t i = 0; i < env.size(); ++i)
				envp.push_back(const_cast<char *>(env[i].c_str()));
			envp.push_back(0);

			// argv : [interpreter, scriptName, NULL]
			std::vector<char *> argv;
			std::string interpreter;
			if (loc)
			{
				interpreter = loc->cgiPath;
				argv.push_back(const_cast<char *>(interpreter.c_str()));
				argv.push_back(const_cast<char *>(scriptName.c_str()));
			}
			else
			{
				interpreter = "./" + scriptName;
				argv.push_back(const_cast<char *>(interpreter.c_str()));
			}
			argv.push_back(0);

			const char *execPath = interpreter.c_str();
			execve(execPath, &argv[0], &envp[0]);

			// Si on arrive ici, execve a échoué
			_exit(1);
		}

		// ===== Parent : communique avec le CGI =====
		close(inPipe[0]);
		close(outPipe[1]);

		// 3) Écriture du body dans stdin du CGI
		if (!bodyForCgi.empty() && request.getMethod() == "POST")
		{
			std::size_t total   = bodyForCgi.size();
			std::size_t written = 0;

			while (written < total)
			{
				ssize_t n = write(inPipe[1],
				                  bodyForCgi.c_str() + written,
				                  total - written);
				if (n <= 0)
					break;
				written += static_cast<std::size_t>(n);
			}
		}
		close(inPipe[1]);

		// 4) Lecture de la sortie du CGI (stdout) avec timeout
		std::string rawOutput;
		char buf[1024];

		std::time_t startTime = std::time(0);

		while (true)
		{
			std::time_t now = std::time(0);
			int elapsed   = static_cast<int>(now - startTime);
			int remaining = CGI_TIMEOUT_SECONDS - elapsed;

			if (remaining <= 0)
			{
				// Timeout CGI
				std::cerr << "CGI timeout (" << CGI_TIMEOUT_SECONDS
				          << "s) for script: " << scriptPath << std::endl;
				kill(pid, SIGKILL);
				int statusKill;
				waitpid(pid, &statusKill, 0);
				close(outPipe[0]);
				return false;
			}

			struct pollfd pfd;
			pfd.fd = outPipe[0];
			pfd.events = POLLIN | POLLHUP | POLLERR;
			pfd.revents = 0;

			int pret = poll(&pfd, 1, remaining * 1000);
			if (pret < 0)
			{
				if (errno == EINTR)
					continue;

				std::cerr << "Error: poll() on CGI pipe failed: "
				          << std::strerror(errno) << std::endl;
				int statusErr;
				waitpid(pid, &statusErr, 0);
				close(outPipe[0]);
				return false;
			}

			if (pret == 0)
			{
				// Sécurité supplémentaire
				std::cerr << "CGI timeout (" << CGI_TIMEOUT_SECONDS
				          << "s) for script: " << scriptPath << std::endl;
				kill(pid, SIGKILL);
				int statusKill;
				waitpid(pid, &statusKill, 0);
				close(outPipe[0]);
				return false;
			}

			if (pfd.revents & POLLIN)
			{
				ssize_t n = read(outPipe[0], buf, sizeof(buf));
				if (n < 0)
				{
					if (errno == EINTR)
						continue;

					std::cerr << "Error: read() from CGI pipe failed: "
					          << std::strerror(errno) << std::endl;
					int statusErr;
					waitpid(pid, &statusErr, 0);
					close(outPipe[0]);
					return false;
				}
				if (n == 0)
				{
					// EOF
					break;
				}

				rawOutput.append(buf, static_cast<std::size_t>(n));
				continue;
			}

			if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
			{
				// On essaie de vider ce qui reste éventuellement
				while (true)
				{
					ssize_t n = read(outPipe[0], buf, sizeof(buf));
					if (n <= 0)
						break;
					rawOutput.append(buf, static_cast<std::size_t>(n));
				}
				break;
			}
		}

		close(outPipe[0]);

		int status = 0;
		if (waitpid(pid, &status, 0) < 0)
		{
			std::cerr << "Error: waitpid() on CGI failed: "
			          << std::strerror(errno) << std::endl;
			return false;
		}

		// Si le CGI s'est terminé anormalement (signal, ou exit code != 0)
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		{
			std::cerr << "CGI script exited abnormally: " << scriptPath;
			if (WIFEXITED(status))
			{
				std::cerr << " (exit code " << WEXITSTATUS(status) << ")";
			}
			else if (WIFSIGNALED(status))
			{
				std::cerr << " (signal " << WTERMSIG(status) << ")";
			}
			std::cerr << std::endl;
			return false;
		}

		if (rawOutput.empty())
			return false;

		// 5) Séparation headers / body
		std::size_t pos = rawOutput.find("\r\n\r\n");
		std::size_t sepLen = 4;
		if (pos == std::string::npos)
		{
			pos = rawOutput.find("\n\n");
			sepLen = 2;
		}

		if (pos == std::string::npos)
		{
			// Pas de headers : tout est body
			outBody = rawOutput;
			return true;
		}

		std::string headerPart = rawOutput.substr(0, pos);
		outBody = rawOutput.substr(pos + sepLen);

		std::istringstream iss(headerPart);
		std::string line;

		while (std::getline(iss, line))
		{
			if (!line.empty() && line[line.size() - 1] == '\r')
				line.erase(line.size() - 1);

			line = trimString(line);
			if (line.empty())
				continue;

			std::size_t colon = line.find(':');
			if (colon == std::string::npos)
				continue;

			std::string name  = trimString(line.substr(0, colon));
			std::string value = trimString(line.substr(colon + 1));

			if (name.empty())
				continue;

			if (name == "Status")
			{
				// "Status: 404 Not Found"
				std::istringstream st(value);
				int code = 0;
				std::string reason;
				if (st >> code)
				{
					std::getline(st, reason);
					reason = trimString(reason);
					if (code >= 100 && code <= 599)
					{
						outStatusCode = code;
						if (!reason.empty())
							outReason = reason;
					}
				}
			}
			else
			{
				outHeaders[name] = value;
			}
		}

		return true;
	}
} // namespace


/*
 * Implémentation de ClientState
 */

ClientState::ClientState()
	: server(NULL),
	  request(),
	  headersComplete(false),
	  requestHandled(false),
	  contentLength(0),
	  readBuffer(),
	  writeBuffer(),
	  isChunked(false),
	  currentChunkSize(NO_CHUNK_SIZE),
	  chunkDecodedBody(),
	  lastActivity(0)
{
}

/*
 * Classe WebServer
 */

WebServer::WebServer(const std::vector<ServerConfig> &servers)
	: _servers(servers),
	  _pollFds(),
	  _clients(),
	  _listenFdToServer()
{
	initListeningSockets();
}

WebServer::~WebServer()
{
	for (std::size_t i = 0; i < _pollFds.size(); ++i)
	{
		if (_pollFds[i].fd >= 0)
			close(_pollFds[i].fd);
	}
}

/*
 * initListeningSockets()
 *
 *  - IMPORTANT pour multi-hostnames:
 *    On crée UN socket d'écoute par port, même s'il y a plusieurs server{}
 *    qui écoutent sur ce port.
 *    Le premier server{} devient le "default server" pour ce port.
 */
void WebServer::initListeningSockets()
{
	std::map<int, bool> portUsed; // port -> déjà un socket créé ?

	for (std::size_t i = 0; i < _servers.size(); ++i)
	{
		const ServerConfig &cfg = _servers[i];

		if (portUsed[cfg.port])
		{
			// Il y a déjà un socket sur ce port, on ne recrée pas un deuxième.
			continue;
		}

		int listenFd = socket(AF_INET, SOCK_STREAM, 0);
		if (listenFd < 0)
		{
			std::cerr << "Error: socket() failed: "
			          << std::strerror(errno) << std::endl;
			throw std::runtime_error("socket() failed");
		}

		int opt = 1;
		if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt,
		               sizeof(opt)) < 0)
		{
			std::cerr << "Error: setsockopt(SO_REUSEADDR) failed: "
			          << std::strerror(errno) << std::endl;
			close(listenFd);
			throw std::runtime_error("setsockopt() failed");
		}

		struct sockaddr_in addr;
		std::memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY); // on écoute sur toutes les IP
		addr.sin_port = htons(static_cast<uint16_t>(cfg.port));

		if (bind(listenFd,
		         reinterpret_cast<struct sockaddr *>(&addr),
		         sizeof(addr)) < 0)
		{
			std::cerr << "Error: bind() failed on port " << cfg.port
			          << ": " << std::strerror(errno) << std::endl;
			close(listenFd);
			throw std::runtime_error("bind() failed");
		}

		if (listen(listenFd, 128) < 0)
		{
			std::cerr << "Error: listen() failed on port " << cfg.port
			          << ": " << std::strerror(errno) << std::endl;
			close(listenFd);
			throw std::runtime_error("listen() failed");
		}

		int flags = fcntl(listenFd, F_GETFL, 0);
		if (flags < 0 || fcntl(listenFd, F_SETFL, flags | O_NONBLOCK) < 0)
		{
			std::cerr << "Error: fcntl(O_NONBLOCK) failed on port "
			          << cfg.port << ": " << std::strerror(errno)
			          << std::endl;
			close(listenFd);
			throw std::runtime_error("fcntl() failed");
		}

		struct pollfd pfd;
		pfd.fd = listenFd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		_pollFds.push_back(pfd);

		// Ce server devient le "default" pour ce port.
		_listenFdToServer[listenFd] = &(_servers[i]);
		portUsed[cfg.port] = true;

		std::cout << "WebServer listening on port " << cfg.port
		          << " (default server host = " << cfg.host << ")\n";
	}
}

/*
 * handleNewConnection()
 */
void WebServer::handleNewConnection(std::size_t index)
{
	if (index >= _pollFds.size())
		return;

	int listenFd = _pollFds[index].fd;
	std::map<int, const ServerConfig *>::iterator sit =
	    _listenFdToServer.find(listenFd);

	if (sit == _listenFdToServer.end())
		return;

	const ServerConfig *server = sit->second;

	while (true)
	{
		struct sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(clientAddr);

		int clientFd = accept(listenFd,
			reinterpret_cast<struct sockaddr *>(&clientAddr),
			&clientLen);

		if (clientFd < 0)
			break; // plus de client à accepter (ou erreur non bloquante)

		int flags = fcntl(clientFd, F_GETFL, 0);
		if (flags < 0 || fcntl(clientFd, F_SETFL, flags | O_NONBLOCK) < 0)
		{
			std::cerr << "Error: fcntl(O_NONBLOCK) on client failed: "
			          << std::strerror(errno) << std::endl;
			close(clientFd);
			continue;
		}

		struct pollfd clientPfd;
		clientPfd.fd = clientFd;
		clientPfd.events = POLLIN;
		clientPfd.revents = 0;

		_pollFds.push_back(clientPfd);

		ClientState state;
		state.server = server;              // default server pour ce port
		state.lastActivity = std::time(0);  // maintenant
		_clients[clientFd] = state;

		std::cout << "New client on port " << server->port
		          << ", fd = " << clientFd << std::endl;
	}
}

/*
 * handleClientRead()
 */
void WebServer::handleClientRead(std::size_t index)
{
	if (index >= _pollFds.size())
		return;

	int fd = _pollFds[index].fd;
	char buffer[1024];

	int bytesRead = recv(fd, buffer, sizeof(buffer), 0);

	if (bytesRead < 0)
	{
		std::cerr << "Error: recv() failed on fd " << fd
		          << ": " << std::strerror(errno) << std::endl;
		removeClient(index);
		return;
	}
	else if (bytesRead == 0)
	{
		std::cout << "Client disconnected, fd = " << fd << std::endl;
		removeClient(index);
		return;
	}

	std::map<int, ClientState>::iterator it = _clients.find(fd);
	if (it == _clients.end())
	{
		removeClient(index);
		return;
	}

	ClientState &state = it->second;
	if (!state.server)
	{
		removeClient(index);
		return;
	}

	state.lastActivity = std::time(0); // on vient de recevoir des données
	state.readBuffer.append(buffer, bytesRead);

	// On boucle tant qu'on n'a pas traité la requête
	while (!state.requestHandled)
	{
		// 1) On attend d'avoir les headers complets ("\r\n\r\n")
		if (!state.headersComplete)
		{
			std::size_t headerEnd = state.readBuffer.find("\r\n\r\n");
			if (headerEnd == std::string::npos)
			{
				// Pas encore tout reçu
				break;
			}

			std::string headerPart = state.readBuffer.substr(0, headerEnd + 4);

			// Parsing des headers (HttpRequest::parse)
			if (!state.request.parse(headerPart))
			{
				HttpResponse response;
				setErrorResponse(*(state.server), response, 400, "Bad Request");

				state.writeBuffer = response.toString();
				state.requestHandled = true;
				state.headersComplete = true;
				state.readBuffer.clear();
				_pollFds[index].events |= POLLOUT;
				break;
			}

			// --- Sélection du bon "virtual host" via Host: ---
			state.server = selectServerForRequest(state.request, *(state.server));

			// --- Gestion du Transfer-Encoding: chunked ---
			state.isChunked = false;
			state.currentChunkSize = NO_CHUNK_SIZE;
			state.chunkDecodedBody.clear();

			std::string te = state.request.getHeader("Transfer-Encoding");
			if (!te.empty())
			{
				std::string lowerTe;
				for (std::size_t i = 0; i < te.size(); ++i)
				{
					char c = te[i];
					if (c >= 'A' && c <= 'Z')
						c = static_cast<char>(c - 'A' + 'a');
					lowerTe.push_back(c);
				}
				if (lowerTe.find("chunked") != std::string::npos)
					state.isChunked = true;
			}

			if (state.isChunked)
			{
				// RFC: normalement pas de Content-Length quand c'est chunked.
				std::string cl = state.request.getHeader("Content-Length");
				if (!cl.empty())
				{
					HttpResponse response;
					setErrorResponse(*(state.server), response, 400, "Bad Request");

					state.writeBuffer = response.toString();
					state.requestHandled = true;
					state.headersComplete = true;
					state.readBuffer.clear();
					_pollFds[index].events |= POLLOUT;
					break;
				}
				state.contentLength = 0; // pas utilisé en chunked
			}
			else
			{
				// Récupération du Content-Length (taille du body attendu)
				std::string cl = state.request.getHeader("Content-Length");
				std::size_t len = 0;
				if (!cl.empty())
				{
					std::istringstream iss(cl);
					if (!(iss >> len) || !iss.eof())
					{
						HttpResponse response;
						setErrorResponse(*(state.server), response, 400, "Bad Request");

						state.writeBuffer = response.toString();
						state.requestHandled = true;
						state.headersComplete = true;
						state.readBuffer.clear();
						_pollFds[index].events |= POLLOUT;
						break;
					}
				}
				state.contentLength = len;

				// Vérification client_max_body_size
				if (state.server->clientMaxBodySize > 0 &&
				    state.contentLength > state.server->clientMaxBodySize)
				{
					HttpResponse response;
					setErrorResponse(*(state.server), response, 413, "Payload Too Large");

					state.writeBuffer = response.toString();
					state.requestHandled = true;
					state.headersComplete = true;
					state.readBuffer.clear();
					_pollFds[index].events |= POLLOUT;
					break;
				}
			}

			state.headersComplete = true;

			// On supprime la partie headers du buffer
			state.readBuffer.erase(0, headerEnd + 4);
		}

		// 2) Gestion du body

		if (state.isChunked)
		{
			// On essaie de décoder ce qu'on a dans readBuffer.
			bool finished = false;
			bool tooLarge = false;

			if (!decodeChunkedBody(state.readBuffer,
			                       state.chunkDecodedBody,
			                       finished,
			                       state.currentChunkSize,
			                       state.server->clientMaxBodySize,
			                       tooLarge))
			{
				HttpResponse response;
				if (tooLarge)
					setErrorResponse(*(state.server), response, 413, "Payload Too Large");
				else
					setErrorResponse(*(state.server), response, 400, "Bad Request");

				state.writeBuffer = response.toString();
				state.requestHandled = true;
				state.readBuffer.clear();
				_pollFds[index].events |= POLLOUT;
				break;
			}

			if (!finished)
			{
				// Pas encore reçu tout le chunked body
				break;
			}

			// On a tout le body déchunké
			state.request.setBody(state.chunkDecodedBody);
		}
		else
		{
			// Body "classique" basé sur Content-Length
			if (state.readBuffer.size() < state.contentLength)
			{
				// On n'a pas encore tout le body
				break;
			}

			std::string body;
			if (state.contentLength > 0)
			{
				body = state.readBuffer.substr(0, state.contentLength);
				state.readBuffer.erase(0, state.contentLength);
			}
			else
				body.clear();

			state.request.setBody(body);
		}

		// 3) On construit la réponse HTTP en fonction de la requête
		HttpResponse response;
		buildHttpResponse(*(state.server), state.request, response);

		state.writeBuffer = response.toString();
		state.requestHandled = true;
		_pollFds[index].events |= POLLOUT;

		break;
	}
}

/*
 * handleClientWrite()
 */
void WebServer::handleClientWrite(std::size_t index)
{
	if (index >= _pollFds.size())
		return;

	int fd = _pollFds[index].fd;

	std::map<int, ClientState>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return;

	ClientState &state = it->second;

	if (state.writeBuffer.empty())
	{
		removeClient(index);
		return;
	}

	int bytesSent = send(fd, state.writeBuffer.c_str(),
	                     state.writeBuffer.size(), 0);

	if (bytesSent < 0)
	{
		std::cerr << "Error: send() failed on fd " << fd
		          << ": " << std::strerror(errno) << std::endl;
		removeClient(index);
	}
	else
	{
		state.lastActivity = std::time(0); // activité d'écriture
		state.writeBuffer.erase(0, bytesSent);

		if (state.writeBuffer.empty())
			removeClient(index);
	}
}

/*
 * removeClient()
 */
void WebServer::removeClient(std::size_t index)
{
	if (index >= _pollFds.size())
		return;

	int fd = _pollFds[index].fd;

	_clients.erase(fd);
	close(fd);

	// On remplace ce pollfd par le dernier pour ne pas avoir de "trou"
	_pollFds[index] = _pollFds.back();
	_pollFds.pop_back();

	std::cout << "Closed client fd " << fd << std::endl;
}

/*
 * findLocationForTarget()
 */
const LocationConfig *WebServer::findLocationForTarget(const ServerConfig &server,
                                                       const std::string &target) const
{
	const LocationConfig *best = 0;
	std::size_t bestLen = 0;

	for (std::size_t i = 0; i < server.locations.size(); ++i)
	{
		const LocationConfig &loc = server.locations[i];
		const std::string &p = loc.path;

		if (p.empty())
			continue;

		if (p.size() <= target.size() &&
		    target.compare(0, p.size(), p) == 0)
		{
			if (p.size() > bestLen)
			{
				best = &(server.locations[i]);
				bestLen = p.size();
			}
		}
	}

	return best;
}

bool WebServer::isMethodAllowed(const LocationConfig *loc,
                                const std::string &method) const
{
	if (!loc || loc->allowedMethods.empty())
	{
		// Si aucune méthode n'est spécifiée, on autorise GET/POST/DELETE
		return (method == "GET" || method == "POST" || method == "DELETE");
	}

	std::set<std::string>::const_iterator it = loc->allowedMethods.find(method);
	return (it != loc->allowedMethods.end());
}

std::string WebServer::buildAllowHeader(const LocationConfig *loc) const
{
	if (loc && !loc->allowedMethods.empty())
	{
		std::string result;
		for (std::set<std::string>::const_iterator it = loc->allowedMethods.begin();
		     it != loc->allowedMethods.end();
		     ++it)
		{
			if (!result.empty())
				result += ", ";
			result += *it;
		}
		return result;
	}
	return "GET, POST, DELETE";
}

std::string WebServer::generateAutoindexPage(const std::string &dirPath,
                                             const std::string &urlPath) const
{
	std::ostringstream oss;

	oss << "<!DOCTYPE html>\n"
	    << "<html>\n"
	    << "<head>\n"
	    << "  <meta charset=\"utf-8\">\n"
	    << "  <title>Index of " << urlPath << "</title>\n"
	    << "</head>\n"
	    << "<body>\n"
	    << "  <h1>Index of " << urlPath << "</h1>\n"
	    << "  <ul>\n";

	DIR *dir = opendir(dirPath.c_str());
	if (!dir)
	{
		oss << "    <li>Cannot open directory</li>\n";
	}
	else
	{
		struct dirent *entry;
		while ((entry = readdir(dir)) != NULL)
		{
			std::string name = entry->d_name;
			if (name == "." || name == "..")
				continue;

			std::string href = urlPath;
			if (!href.empty() && href[href.size() - 1] != '/')
				href += "/";

			href += name;

			oss << "    <li><a href=\"" << href << "\">" << name << "</a></li>\n";
		}
		closedir(dir);
	}

	oss << "  </ul>\n"
	    << "</body>\n"
	    << "</html>\n";

	return oss.str();
}

/*
 * Sélection du bon server en fonction de Host:
 *
 *  - defaultServer : le server associé au port sur lequel on a accepté la connexion
 *  - On lit le header "Host:", on enlève le port (ex: "example.com:8080" → "example.com")
 *  - On cherche dans _servers un ServerConfig qui a le même port et un host qui matche.
 *  - Si rien ne matche, on garde defaultServer.
 */
const ServerConfig *WebServer::selectServerForRequest(const HttpRequest &request,
                                                      const ServerConfig &defaultServer) const
{
	std::string hostHeader = request.getHeader("Host");
	if (hostHeader.empty())
		return &defaultServer;

	// Extraire host sans le port éventuel
	std::string hostOnly = hostHeader;
	std::size_t colon = hostOnly.find(':');
	if (colon != std::string::npos)
		hostOnly.erase(colon);

	hostOnly = trimString(hostOnly);
	if (hostOnly.empty())
		return &defaultServer;

	// lower-case pour comparaison insensible à la casse
	std::string hostLower;
	for (std::size_t i = 0; i < hostOnly.size(); ++i)
	{
		char c = hostOnly[i];
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
		hostLower.push_back(c);
	}

	const ServerConfig *best = &defaultServer;

	for (std::size_t i = 0; i < _servers.size(); ++i)
	{
		const ServerConfig &cand = _servers[i];

		// Même port ?
		if (cand.port != defaultServer.port)
			continue;

		std::string cfgHost = trimString(cand.host);
		if (cfgHost.empty())
			continue;

		std::string cfgLower;
		for (std::size_t j = 0; j < cfgHost.size(); ++j)
		{
			char c = cfgHost[j];
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
			cfgLower.push_back(c);
		}

		if (cfgLower == hostLower)
		{
			best = &cand;
			break;
		}
	}

	return best;
}

/*
 * buildHttpResponse()
 */
void WebServer::buildHttpResponse(const ServerConfig &server,
                                  const HttpRequest &request,
                                  HttpResponse &response)
{
	const std::string &method = request.getMethod();
	const std::string &target = request.getTarget();

	// Méthode globale autorisée ?
	if (method != "GET" && method != "POST" && method != "DELETE")
	{
		setErrorResponse(server, response, 405, "Method Not Allowed");
		response.setHeader("Allow", "GET, POST, DELETE");
		return;
	}

	// On cherche la meilleure location pour ce target
	const LocationConfig *loc = findLocationForTarget(server, target);

	// Méthode autorisée dans cette location ?
	if (!isMethodAllowed(loc, method))
	{
		setErrorResponse(server, response, 405, "Method Not Allowed");
		response.setHeader("Allow", buildAllowHeader(loc));
		return;
	}

	// Gestion des redirections (301, 302, ...)
	if (loc && loc->redirectSet)
	{
		int code = loc->redirectCode;
		std::string reason;

		switch (code)
		{
			case 301: reason = "Moved Permanently"; break;
			case 302: reason = "Found"; break;
			case 303: reason = "See Other"; break;
			case 307: reason = "Temporary Redirect"; break;
			case 308: reason = "Permanent Redirect"; break;
			default:  reason = "Redirect"; break;
		}

		response.setStatus(code, reason);
		response.setHeader("Location", loc->redirectUrl);
		response.setHeader("Connection", "close");
		response.setHeader("Content-Type", "text/html");

		std::ostringstream body;
		body << "<!DOCTYPE html>\n"
		     << "<html>\n"
		     << "<head><meta charset=\"utf-8\"><title>"
		     << code << " " << reason << "</title></head>\n"
		     << "<body>\n"
		     << "<h1>" << code << " " << reason << "</h1>\n"
		     << "<p>Resource has moved to <a href=\""
		     << loc->redirectUrl << "\">" << loc->redirectUrl << "</a>.</p>\n"
		     << "</body>\n"
		     << "</html>\n";

		response.setBody(body.str());
		return;
	}

	// ===================== GET =====================
	if (method == "GET")
	{
		std::string path;
		if (!resolvePathForCgi(server, loc, target, path))
		{
			setErrorResponse(server, response, 400, "Bad Request");
			return;
		}

		// On a aussi besoin de savoir si on est sur un dossier pour index/autoindex
		std::string pathTarget = target;
		std::size_t qPos = pathTarget.find('?');
		if (qPos != std::string::npos)
			pathTarget.erase(qPos);

		std::string root   = server.root;
		std::string index  = server.index;
		bool        aiFlag = server.autoindex;

		if (loc)
		{
			if (!loc->root.empty())
				root = loc->root;
			if (!loc->index.empty())
				index = loc->index;
			if (loc->autoindexSet)
				aiFlag = loc->autoindex;
		}

		// --- Cas dossier (on tente index, puis autoindex) ---
		struct stat st;
		if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
		{
			std::string dirPath = path;
			if (dirPath.empty() || dirPath[dirPath.size() - 1] != '/')
				dirPath += "/";

			std::string indexPath = dirPath + index;

			std::ifstream indexFile(indexPath.c_str(),
			                        std::ios::in | std::ios::binary);
			if (indexFile)
			{
				std::ostringstream oss;
				oss << indexFile.rdbuf();
				std::string body = oss.str();

				response.setStatus(200, "OK");
				response.setHeader("Content-Type", getMimeType(indexPath));
				response.setHeader("Connection", "close");
				response.setBody(body);
				return;
			}

			if (!aiFlag)
			{
				setErrorResponse(server, response, 403, "Forbidden");
				return;
			}

			// autoindex
			std::string urlPath = pathTarget;
			if (urlPath.empty())
				urlPath = "/";
			if (urlPath[urlPath.size() - 1] != '/')
				urlPath += "/";

			std::string body = generateAutoindexPage(dirPath, urlPath);

			response.setStatus(200, "OK");
			response.setHeader("Content-Type", "text/html");
			response.setHeader("Connection", "close");
			response.setBody(body);
			return;
		}

		// --- CGI (GET) ---
		if (loc && loc->cgiEnabled && hasExtension(path, loc->cgiExtension))
		{
			// On vérifie que le script existe
			std::ifstream scriptTest(path.c_str(),
			                         std::ios::in | std::ios::binary);
			if (!scriptTest)
			{
				setErrorResponse(server, response, 404, "Not Found");
				return;
			}
			scriptTest.close();

			std::string cgiBody;
			std::map<std::string, std::string> cgiHeaders;
			int cgiStatus;
			std::string cgiReason;

			if (!executeCgi(request, server, loc, path,
			                cgiBody, cgiHeaders, cgiStatus, cgiReason))
			{
				setErrorResponse(server, response, 500, "Internal Server Error");
				return;
			}

			response.setStatus(cgiStatus, cgiReason);

			bool hasContentType = false;

			for (std::map<std::string, std::string>::iterator it = cgiHeaders.begin();
			     it != cgiHeaders.end();
			     ++it)
			{
				const std::string &name  = it->first;
				const std::string &value = it->second;

				response.setHeader(name, value);

				std::string lower;
				for (std::size_t i = 0; i < name.size(); ++i)
				{
					char c = name[i];
					if (c >= 'A' && c <= 'Z')
						c = static_cast<char>(c - 'A' + 'a');
					lower.push_back(c);
				}
				if (lower == "content-type")
					hasContentType = true;
			}

			if (!hasContentType)
				response.setHeader("Content-Type", "text/html");

			response.setHeader("Connection", "close");
			response.setBody(cgiBody);
			return;
		}

		// --- Fichier statique ---
		std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
		if (!file)
		{
			setErrorResponse(server, response, 404, "Not Found");
			return;
		}

		std::ostringstream oss;
		oss << file.rdbuf();
		std::string body = oss.str();

		response.setStatus(200, "OK");
		response.setHeader("Content-Type", getMimeType(path));
		response.setHeader("Connection", "close");
		response.setBody(body);
		return;
	}

	// ===================== POST =====================
	if (method == "POST")
	{
		// 0) CGI POST (si la location est configurée pour CGI)
		if (loc && loc->cgiEnabled)
		{
			std::string path;
			if (!resolvePathForCgi(server, loc, target, path))
			{
				setErrorResponse(server, response, 400, "Bad Request");
				return;
			}

			// On ne traite comme CGI que si le fichier a la bonne extension
			if (hasExtension(path, loc->cgiExtension))
			{
				// Vérifier que le script existe
				std::ifstream scriptTest(path.c_str(),
				                         std::ios::in | std::ios::binary);
				if (!scriptTest)
				{
					setErrorResponse(server, response, 404, "Not Found");
					return;
				}
				scriptTest.close();

				std::string                         cgiBody;
				std::map<std::string, std::string>  cgiHeaders;
				int                                 cgiStatus;
				std::string                         cgiReason;

				if (!executeCgi(request, server, loc, path,
				                cgiBody, cgiHeaders, cgiStatus, cgiReason))
				{
					setErrorResponse(server, response, 500, "Internal Server Error");
					return;
				}

				response.setStatus(cgiStatus, cgiReason);

				bool hasContentType = false;

				for (std::map<std::string, std::string>::iterator it = cgiHeaders.begin();
				     it != cgiHeaders.end();
				     ++it)
				{
					const std::string &name  = it->first;
					const std::string &value = it->second;

					response.setHeader(name, value);

					std::string lower;
					for (std::size_t i = 0; i < name.size(); ++i)
					{
						char c = name[i];
						if (c >= 'A' && c <= 'Z')
							c = static_cast<char>(c - 'A' + 'a');
						lower.push_back(c);
					}
					if (lower == "content-type")
						hasContentType = true;
				}

				if (!hasContentType)
					response.setHeader("Content-Type", "text/html");

				response.setHeader("Connection", "close");
				response.setBody(cgiBody);
				return;
			}
		}

		// 1) Upload (si upload_store configuré)
		if (loc && loc->uploadStoreSet)
		{
			if (target.empty() || target[0] != '/')
			{
				setErrorResponse(server, response, 400, "Bad Request");
				return;
			}
			if (target.find("..") != std::string::npos)
			{
				setErrorResponse(server, response, 403, "Forbidden");
				return;
			}

			std::string suffix;
			{
				std::string locPath = loc->path;
				if (locPath.size() > 1 && locPath[locPath.size() - 1] == '/')
					locPath.erase(locPath.size() - 1);

				if (target.size() >= locPath.size())
					suffix = target.substr(locPath.size());
				else
					suffix.clear();
			}

			if (suffix.empty() || suffix == "/")
			{
				setErrorResponse(server, response, 400, "Bad Request");
				return;
			}

			if (!suffix.empty() && suffix[0] == '/')
				suffix.erase(0, 1);

			if (suffix.empty() || suffix.find('/') != std::string::npos)
			{
				setErrorResponse(server, response, 400, "Bad Request");
				return;
			}

			std::string fileName = suffix;

			std::string path = loc->uploadStore;
			if (!path.empty() && path[path.size() - 1] != '/')
				path += "/";
			path += fileName;

			std::ofstream out(path.c_str(),
			                  std::ios::out | std::ios::binary | std::ios::trunc);
			if (!out)
			{
				setErrorResponse(server, response, 500, "Internal Server Error");
				return;
			}

			out << request.getBody();
			out.close();

			response.setStatus(201, "Created");
			response.setHeader("Content-Type", "text/plain");
			response.setHeader("Connection", "close");

			std::ostringstream body;
			body << "File uploaded as " << fileName << "\r\n";
			response.setBody(body.str());
			return;
		}

		// 2) Handler POST "générique" pour toutes les autres routes POST valides
		// (qui ne sont ni un upload ni un CGI)
		response.setStatus(200, "OK");
		response.setHeader("Content-Type", "text/plain");
		response.setHeader("Connection", "close");

		std::ostringstream oss;
		oss << "You sent a POST request to " << target << "\r\n";
		oss << "Body length: " << request.getBody().size() << " bytes\r\n";
		if (!request.getBody().empty())
		{
			oss << "\r\n";
			oss << request.getBody() << "\r\n";
		}

		response.setBody(oss.str());
		return;
	}

	// ===================== DELETE =====================
	if (method == "DELETE")
	{
		if (target.empty() || target[0] != '/')
		{
			setErrorResponse(server, response, 400, "Bad Request");
			return;
		}

		if (target.find("..") != std::string::npos)
		{
			setErrorResponse(server, response, 403, "Forbidden");
			return;
		}

		if (target == "/")
		{
			setErrorResponse(server, response, 403, "Forbidden");
			return;
		}

		std::string root = server.root;
		if (loc && !loc->root.empty())
			root = loc->root;

		std::string path;
		if (loc)
		{
			std::string locPath = loc->path;
			if (locPath.size() > 1 && locPath[locPath.size() - 1] == '/')
				locPath.erase(locPath.size() - 1);

			std::string suffix = target.substr(locPath.size());

			if (!root.empty() &&
			    root[root.size() - 1] == '/' &&
			    suffix.size() > 0 &&
			    suffix[0] == '/')
				path = root + suffix.substr(1);
			else
				path = root + suffix;
		}
		else
		{
			if (!root.empty() &&
			    root[root.size() - 1] == '/' &&
			    target.size() > 0 &&
			    target[0] == '/')
				path = root + target.substr(1);
			else
				path = root + target;
		}

		// On vérifie que le fichier existe
		{
			std::ifstream test(path.c_str(), std::ios::in | std::ios::binary);
			if (!test)
			{
				setErrorResponse(server, response, 404, "Not Found");
				return;
			}
		}

		if (std::remove(path.c_str()) != 0)
		{
			setErrorResponse(server, response, 500, "Internal Server Error");
			return;
		}

		response.setStatus(200, "OK");
		response.setHeader("Content-Type", "text/plain");
		response.setHeader("Connection", "close");
		response.setBody("File deleted.\r\n");
		return;
	}

	// Ne devrait pas arriver (on a déjà filtré les méthodes)
	setErrorResponse(server, response, 405, "Method Not Allowed");
	response.setHeader("Allow", "GET, POST, DELETE");
}

std::string WebServer::getMimeType(const std::string &path) const
{
	std::size_t dot = path.rfind('.');
	if (dot == std::string::npos)
		return "application/octet-stream";

	std::string ext = path.substr(dot + 1);

	if (ext == "html" || ext == "htm")
		return "text/html";
	if (ext == "txt")
		return "text/plain";
	if (ext == "css")
		return "text/css";
	if (ext == "js")
		return "application/javascript";

	return "application/octet-stream";
}

void WebServer::setErrorResponse(const ServerConfig &server,
                                 HttpResponse &response,
                                 int code,
                                 const std::string &reason)
{
	response.setStatus(code, reason);
	response.setHeader("Connection", "close");

	// On essaie une error_page personnalisée si définie
	std::map<int, std::string>::const_iterator it =
	    server.errorPages.find(code);
	if (it != server.errorPages.end())
	{
		std::string rel = it->second;

		if (!rel.empty() && rel[0] != '/')
			rel = "/" + rel;

		if (rel.find("..") == std::string::npos)
		{
			std::string path;
			if (!server.root.empty() &&
			    server.root[server.root.size() - 1] == '/' &&
			    rel.size() > 0 &&
			    rel[0] == '/')
			{
				path = server.root + rel.substr(1);
			}
			else
			{
				path = server.root + rel;
			}

			std::ifstream file(path.c_str(),
			                   std::ios::in | std::ios::binary);
			if (file)
			{
				std::ostringstream oss;
				oss << file.rdbuf();
				std::string body = oss.str();

				response.setHeader("Content-Type", getMimeType(path));
				response.setBody(body);
				return;
			}
		}
	}

	// Sinon, fallback : message texte simple
	response.setHeader("Content-Type", "text/plain");
	std::ostringstream oss;
	oss << code << " " << reason << "\r\n";
	response.setBody(oss.str());
}

/*
 * run()
 *
 *  - poll() avec un timeout (1s)
 *  - à chaque tour, on ferme les clients inactifs depuis plus de
 *    CLIENT_TIMEOUT_SECONDS.
 */
void WebServer::run()
{
	while (true)
	{
		if (_pollFds.empty())
			continue;

		int timeoutMs = 1000; // 1 seconde
		int ret = poll(&_pollFds[0], _pollFds.size(), timeoutMs);

		if (ret < 0)
		{
			std::cerr << "Error: poll() failed: "
			          << std::strerror(errno) << std::endl;
			break;
		}

		std::size_t nfds = _pollFds.size();

		// 1) Timeout clients inactifs
		std::time_t now = std::time(0);
		for (std::size_t i = 0; i < nfds; ++i)
		{
			int fd = _pollFds[i].fd;

			// On ne timeoute pas les sockets d'écoute
			if (_listenFdToServer.find(fd) != _listenFdToServer.end())
				continue;

			std::map<int, ClientState>::iterator it = _clients.find(fd);
			if (it == _clients.end())
				continue;

			ClientState &state = it->second;
			if (state.lastActivity != 0 &&
			    now - state.lastActivity > CLIENT_TIMEOUT_SECONDS)
			{
				std::cout << "Client fd " << fd
				          << " timed out, closing." << std::endl;
				removeClient(i);
				--i;
				nfds = _pollFds.size();
			}
		}

		// 2) Gestion des événements I/O
		nfds = _pollFds.size();

		for (std::size_t i = 0; i < nfds; ++i)
		{
			if (_pollFds[i].revents == 0)
				continue;

			// Socket d'écoute ?
			if (_listenFdToServer.find(_pollFds[i].fd) != _listenFdToServer.end())
			{
				if (_pollFds[i].revents & POLLIN)
					handleNewConnection(i);
			}
			else
			{
				// Erreurs / fermeture
				if (_pollFds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
				{
					removeClient(i);
					--i;
					nfds = _pollFds.size();
					continue;
				}

				// Lecture
				if (i < _pollFds.size() && (_pollFds[i].revents & POLLIN))
				{
					handleClientRead(i);
					if (i >= _pollFds.size())
					{
						--i;
						nfds = _pollFds.size();
						continue;
					}
				}

				// Écriture
				if (i < _pollFds.size() && (_pollFds[i].revents & POLLOUT))
				{
					handleClientWrite(i);
					if (i >= _pollFds.size())
					{
						--i;
						nfds = _pollFds.size();
						continue;
					}
				}
			}
		}
	}
}

