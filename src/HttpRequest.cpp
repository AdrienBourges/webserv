#include "HttpRequest.hpp"

#include <cstddef>
#include <iostream>
#include <sstream>

namespace
{
	std::string trim(const std::string &s)
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

	static std::string toLower(const std::string &s)
	{
		std::string out;
		out.reserve(s.size());
		for (std::size_t i = 0; i < s.size(); ++i)
		{
			char c = s[i];
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
			out.push_back(c);
		}
		return out;
	}
}

HttpRequest::HttpRequest()
	: _method(), _target(), _version(), _headers(), _body()
{
}

HttpRequest::~HttpRequest()
{
}

bool HttpRequest::parse(const std::string &raw)
{
	_method.clear();
	_target.clear();
	_version.clear();
	_headers.clear();
	_body.clear(); // body sera rempli plus tard par le serveur

	// Trouver la première ligne
	std::size_t pos = raw.find("\r\n");
	if (pos == std::string::npos)
		return false;

	std::string requestLine = raw.substr(0, pos);

	// Parse request line (exactement 3 tokens)
	{
		std::istringstream iss(requestLine);
		std::string method, target, version, extra;

		if (!(iss >> method >> target >> version))
			return false;
		if (iss >> extra)
			return false;

		if (method.empty() || target.empty() || version.empty())
			return false;

		// Support HTTP strict (au minimum 1.1). Tu peux garder 1.0 aussi.
		if (version != "HTTP/1.1" && version != "HTTP/1.0")
			return false;

		_method = method;
		_target = target;
		_version = version;
	}

	// Parsing des headers
	std::size_t lineStart = pos + 2; // sauter "\r\n"
	bool hostSeen = false;

	while (lineStart < raw.size())
	{
		std::size_t lineEnd = raw.find("\r\n", lineStart);
		if (lineEnd == std::string::npos)
			return false; // headers incomplets

		if (lineEnd == lineStart)
		{
			// Ligne vide -> fin des headers
			lineStart = lineEnd + 2;
			break;
		}

		std::string line = raw.substr(lineStart, lineEnd - lineStart);

		// Refuser l'obs-fold (ligne qui commence par espace/tab)
		if (!line.empty() && (line[0] == ' ' || line[0] == '\t'))
			return false;

		std::size_t colonPos = line.find(':');
		if (colonPos == std::string::npos)
			return false; // <-- IMPORTANT: header invalide => 400

		std::string name = trim(line.substr(0, colonPos));
		std::string value = trim(line.substr(colonPos + 1));

		if (name.empty())
			return false;

		// header-name ne doit pas contenir d'espaces/tabs
		if (name.find_first_of(" \t") != std::string::npos)
			return false;

		std::string lowerName = toLower(name);

		if (lowerName == "host")
		{
			if (hostSeen)
				return false; // Host dupliqué
			hostSeen = true;
		}

		_headers[lowerName] = value;

		lineStart = lineEnd + 2;
	}

	// Host obligatoire en HTTP/1.1
	if (_version == "HTTP/1.1" && !hostSeen)
		return false;

	return true;
}

const std::string &HttpRequest::getMethod() const
{
	return _method;
}

const std::string &HttpRequest::getTarget() const
{
	return _target;
}

const std::string &HttpRequest::getVersion() const
{
	return _version;
}

const std::map<std::string, std::string> &HttpRequest::getHeaders() const
{
	return _headers;
}

bool HttpRequest::hasHeader(const std::string &name) const
{
	std::string key = toLower(name);
	return _headers.find(key) != _headers.end();
}

std::string HttpRequest::getHeader(const std::string &name) const
{
	std::string key = toLower(name);
	std::map<std::string, std::string>::const_iterator it = _headers.find(key);
	if (it == _headers.end())
		return std::string();
	return it->second;
}

void HttpRequest::setBody(const std::string &body)
{
	_body = body;
}

const std::string &HttpRequest::getBody() const
{
	return _body;
}

