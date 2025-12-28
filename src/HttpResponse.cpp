/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+        */
/*   Created: 2025/11/28 by you                       #+#    #+#             */
/*   Updated: 2025/11/28 by you                       ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/HttpResponse.hpp"

#include <sstream> // std::ostringstream

HttpResponse::HttpResponse()
	: _statusCode(200), _reasonPhrase("OK"), _headers(), _body()
{
}

HttpResponse::~HttpResponse()
{
}

void HttpResponse::setStatus(int code, const std::string &reason)
{
	_statusCode = code;
	_reasonPhrase = reason;
}

void HttpResponse::setBody(const std::string &body)
{
	_body = body;
}

void HttpResponse::setHeader(const std::string &name, const std::string &value)
{
	_headers[name] = value;
}

int HttpResponse::getStatusCode() const
{
	return _statusCode;
}

std::string HttpResponse::toString() const
{
	std::ostringstream oss;

	// Status line
	oss << "HTTP/1.1 " << _statusCode << " " << _reasonPhrase << "\r\n";

	bool hasContentLength = false;
	bool hasServer = false;

	// Headers définis par l'utilisateur
	std::map<std::string, std::string>::const_iterator it = _headers.begin();
	for (; it != _headers.end(); ++it)
	{
		oss << it->first << ": " << it->second << "\r\n";
		if (it->first == "Content-Length")
			hasContentLength = true;
		else if (it->first == "Server")
			hasServer = true;
	}

	// Header "Server" par défaut si l'utilisateur ne l'a pas mis
	if (!hasServer)
		oss << "Server: webserv/0.1\r\n";

	// Header Content-Length automatique si non fourni
	if (!hasContentLength)
		oss << "Content-Length: " << _body.size() << "\r\n";

	// Ligne vide qui sépare headers et body
	oss << "\r\n";

	// Corps de la réponse
	oss << _body;

	return oss.str();
}

